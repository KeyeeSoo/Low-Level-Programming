#define _POSIX_C_SOURCE 200809L
#include "AuthWorker.h"
#include "Accounts.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define AUTH_QUEUE_CAP 16
#define AUTH_RECENT_CAP 16
#define ENV_FILE ".env"
#define LOCAL_ACCOUNTS_FILE "accounts.db"

/* Clients resend until answered, so a repeated request must return the original
 * answer: otherwise a retried "create account" would report the name as taken. */
typedef struct {
    int nonce, result, token;
    char name[USERNAME_MAX + 1];
} RecentAnswer;

static struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    AuthJob jobs[AUTH_QUEUE_CAP];
    unsigned read, write;
    int stop;
    ServerBridge *bridge;
    RecentAnswer recent[AUTH_RECENT_CAP];
    unsigned recent_next;
} worker = {.mutex = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER};

static AccountStore *open_store(void)
{
    char uri[768];
    AccountStore *store = NULL;
    if (accounts_read_env(ENV_FILE, "MONGODB_URI", uri, sizeof(uri))) {
        store = accounts_open_mongo(uri);
        OPENSSL_cleanse(uri, sizeof(uri));
        if (store == NULL)
            fprintf(stderr, "[auth] Could not reach MongoDB. Check your internet and the Atlas "
                            "IP Access List. Using the local file instead.\n");
    }
    if (store == NULL)
        store = accounts_open_file(LOCAL_ACCOUNTS_FILE);
    return store;
}

static void answer(const AuthJob *job, int result, int token, const char *name)
{
    ServerEvent event = {.type = SERVER_AUTHED, .nonce = job->nonce, .action = result, .token = token,
                         .guest = job->mode == AUTH_MODE_GUEST};
    snprintf(event.name, sizeof(event.name), "%s", name);
    if (!server_bridge_push_event(worker.bridge, event))
        fprintf(stderr, "[auth] event queue full; the client will retry\n");
}

/* A guest skips the password, but still gets a server-issued name and session
 * token. Nothing is stored, so the name lasts only for this game. */
static int guest_login(char name[USERNAME_MAX + 1])
{
    unsigned number = 0;
    if (RAND_bytes((unsigned char *)&number, sizeof(number)) != 1)
        return AUTH_STORE_ERROR;
    snprintf(name, USERNAME_MAX + 1, "Guest_%04u", number % 10000u);
    return AUTH_OK;
}

static void process_job(AccountStore *store, AuthJob *job)
{
    for (unsigned i = 0; i < AUTH_RECENT_CAP; i++) {
        const RecentAnswer *seen = &worker.recent[i];
        if (seen->nonce != 0 && seen->nonce == job->nonce) {
            answer(job, seen->result, seen->token, seen->name);
            return;
        }
    }
    char name[USERNAME_MAX + 1] = "";
    int result = AUTH_STORE_ERROR, token = 0;
    if (job->mode == AUTH_MODE_GUEST) {
        result = guest_login(name);
    } else if (store != NULL) {
        if (job->mode == AUTH_MODE_REGISTER)
            result = account_register(store, job->username, job->password, name);
        else
            result = account_login(store, job->username, job->password, name);
    }
    if (result == AUTH_OK) {
        /* The session token proves identity for the rest of the game. It comes
         * from a CSPRNG, unlike the old token built from the process ID and time. */
        do {
            if (RAND_bytes((unsigned char *)&token, sizeof(token)) != 1) {
                result = AUTH_STORE_ERROR;
                token = 1;
                break;
            }
            token &= 0x7fffffff;
        } while (token == 0);
        if (result != AUTH_OK)
            token = 0;
    }
    if (job->mode == AUTH_MODE_REGISTER)
        printf("[auth] register '%s': result %d\n", job->username, result);
    else if (job->mode == AUTH_MODE_GUEST)
        printf("[auth] guest '%s': result %d\n", name, result);
    else
        printf("[auth] login '%s': result %d\n", job->username, result);
    RecentAnswer *slot = &worker.recent[worker.recent_next];
    worker.recent_next = (worker.recent_next + 1) % AUTH_RECENT_CAP;
    *slot = (RecentAnswer){.nonce = job->nonce, .result = result, .token = token};
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    answer(job, result, token, name);
}

static void *auth_thread(void *unused)
{
    (void)unused;
    AccountStore *store = open_store();
    if (store != NULL)
        printf("[auth] Accounts are stored in: %s\n", store->description);
    else
        printf("[auth] No account storage (out of memory)\n");
    for (;;) {
        AuthJob job;
        pthread_mutex_lock(&worker.mutex);
        while (!worker.stop && worker.read == worker.write)
            pthread_cond_wait(&worker.wake, &worker.mutex);
        if (worker.stop) {
            pthread_mutex_unlock(&worker.mutex);
            break;
        }
        job = worker.jobs[worker.read];
        OPENSSL_cleanse(&worker.jobs[worker.read], sizeof(AuthJob));
        worker.read = (worker.read + 1) % AUTH_QUEUE_CAP;
        pthread_mutex_unlock(&worker.mutex);
        process_job(store, &job); /* Never hold the mutex across slow work. */
        OPENSSL_cleanse(&job, sizeof(job));
    }
    if (store != NULL)
        store->close(store);
    return NULL;
}

int auth_worker_start(ServerBridge *bridge)
{
    worker.bridge = bridge;
    return pthread_create(&worker.thread, NULL, auth_thread, NULL) == 0;
}

int auth_worker_submit(const AuthJob *job)
{
    int ok = 0;
    pthread_mutex_lock(&worker.mutex);
    unsigned next = (worker.write + 1) % AUTH_QUEUE_CAP;
    if (next != worker.read) {
        worker.jobs[worker.write] = *job;
        worker.write = next;
        ok = 1;
        pthread_cond_signal(&worker.wake);
    }
    pthread_mutex_unlock(&worker.mutex);
    return ok;
}

void auth_worker_stop(void)
{
    pthread_mutex_lock(&worker.mutex);
    worker.stop = 1;
    pthread_cond_signal(&worker.wake);
    pthread_mutex_unlock(&worker.mutex);
    pthread_join(worker.thread, NULL);
}
