/* Login rules, password hashing and the local-file account store. */
#define _POSIX_C_SOURCE 200809L
#include "Accounts.h"

#include <ctype.h>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Slow on purpose: makes guessing passwords from a stolen store expensive.
 * Runs on the server's auth thread, so it can never stall the game loop. */
#define PBKDF2_ITERATIONS 200000
#define SALT_BYTES 16
#define HASH_BYTES 32

static void to_hex(const unsigned char *bytes, int count, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < count; i++) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[count * 2] = '\0';
}

static int hash_password(const char *password, const char *salt_hex, char *hash_hex)
{
    unsigned char hash[HASH_BYTES];
    if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                          (const unsigned char *)salt_hex, (int)strlen(salt_hex),
                          PBKDF2_ITERATIONS, EVP_sha256(), HASH_BYTES, hash) != 1)
        return 0;
    to_hex(hash, HASH_BYTES, hash_hex);
    OPENSSL_cleanse(hash, sizeof(hash));
    return 1;
}

/* Letters, digits, _ and -, so a name is always safe in a file, a query or on screen. */
static int valid_username(const char *username, char *key)
{
    size_t length = strlen(username);
    if (length < USERNAME_MIN || length > USERNAME_MAX)
        return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)username[i];
        if (!isalnum(c) && c != '_' && c != '-')
            return 0;
        key[i] = (char)tolower(c);
    }
    key[length] = '\0';
    return 1;
}

static int valid_password(const char *password)
{
    size_t length = strlen(password);
    return length >= PASSWORD_MIN && length <= PASSWORD_MAX;
}

int account_register(AccountStore *store, const char *username, const char *password,
                     char display_name[USERNAME_MAX + 1])
{
    AccountRecord record;
    unsigned char salt[SALT_BYTES];
    memset(&record, 0, sizeof(record));
    if (!valid_username(username, record.username_key) || !valid_password(password))
        return AUTH_INVALID_INPUT;
    if (RAND_bytes(salt, SALT_BYTES) != 1)
        return AUTH_STORE_ERROR;
    to_hex(salt, SALT_BYTES, record.salt);
    if (!hash_password(password, record.salt, record.password_hash))
        return AUTH_STORE_ERROR;
    snprintf(record.username, sizeof(record.username), "%s", username);
    int inserted = store->insert(store, &record);
    if (inserted < 0)
        return AUTH_STORE_ERROR;
    if (inserted == 0)
        return AUTH_NAME_TAKEN;
    snprintf(display_name, USERNAME_MAX + 1, "%s", record.username);
    return AUTH_OK;
}

int account_login(AccountStore *store, const char *username, const char *password,
                  char display_name[USERNAME_MAX + 1])
{
    AccountRecord record;
    char key[USERNAME_MAX + 1];
    char hash[ACCOUNT_HASH_HEX];
    /* One message for "no such user" and "wrong password": the response must
     * not reveal which usernames exist. */
    if (!valid_username(username, key) || !valid_password(password))
        return AUTH_BAD_CREDENTIALS;
    int found = store->lookup(store, key, &record);
    if (found < 0)
        return AUTH_STORE_ERROR;
    if (found == 0) {
        /* Spend the same time as a real check, so timing doesn't reveal it either. */
        hash_password(password, "00000000000000000000000000000000", hash);
        return AUTH_BAD_CREDENTIALS;
    }
    if (!hash_password(password, record.salt, hash))
        return AUTH_STORE_ERROR;
    /* Constant-time comparison: no early exit at the first differing byte. */
    if (strlen(record.password_hash) != HASH_BYTES * 2 ||
        CRYPTO_memcmp(hash, record.password_hash, HASH_BYTES * 2) != 0)
        return AUTH_BAD_CREDENTIALS;
    snprintf(display_name, USERNAME_MAX + 1, "%s", record.username);
    return AUTH_OK;
}

int accounts_read_env(const char *path, const char *key, char *out, unsigned long size)
{
    FILE *file = fopen(path, "r");
    if (file == NULL)
        return 0;
    char line[1024];
    size_t key_length = strlen(key);
    int found = 0;
    while (!found && fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, key, key_length) != 0 || line[key_length] != '=')
            continue;
        char *value = line + key_length + 1;
        value[strcspn(value, "\r\n")] = '\0';
        /* Allow KEY="value" as well as KEY=value. */
        size_t length = strlen(value);
        if (length >= 2 && (value[0] == '"' || value[0] == '\'') && value[length - 1] == value[0]) {
            value[length - 1] = '\0';
            value++;
        }
        if (value[0] != '\0' && strlen(value) < size) {
            memcpy(out, value, strlen(value) + 1);
            found = 1;
        }
    }
    fclose(file);
    OPENSSL_cleanse(line, sizeof(line));
    return found;
}

/* ---- Local file: one "key:username:salt:hash" line per account --------- */
typedef struct {
    AccountStore base;
    char path[512];
} FileStore;

static int file_lookup(AccountStore *store, const char *username_key, AccountRecord *out)
{
    FileStore *self = (FileStore *)store;
    FILE *file = fopen(self->path, "r");
    if (file == NULL)
        return 0; /* No file yet just means no accounts yet. */
    char line[256];
    int found = 0;
    while (!found && fgets(line, sizeof(line), file) != NULL) {
        char *save = NULL;
        char *key = strtok_r(line, ":\r\n", &save);
        char *name = strtok_r(NULL, ":\r\n", &save);
        char *salt = strtok_r(NULL, ":\r\n", &save);
        char *hash = strtok_r(NULL, ":\r\n", &save);
        if (key == NULL || name == NULL || salt == NULL || hash == NULL ||
            strcmp(key, username_key) != 0)
            continue;
        memset(out, 0, sizeof(*out));
        snprintf(out->username_key, sizeof(out->username_key), "%s", key);
        snprintf(out->username, sizeof(out->username), "%s", name);
        snprintf(out->salt, sizeof(out->salt), "%s", salt);
        snprintf(out->password_hash, sizeof(out->password_hash), "%s", hash);
        found = 1;
    }
    fclose(file);
    return found;
}

static int file_insert(AccountStore *store, const AccountRecord *record)
{
    FileStore *self = (FileStore *)store;
    AccountRecord existing;
    if (file_lookup(store, record->username_key, &existing) == 1)
        return 0;
    /* Only the auth thread writes this file, so check-then-append is safe. */
    int fd = open(self->path, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (fd < 0)
        return -1;
    FILE *file = fdopen(fd, "a");
    if (file == NULL) {
        close(fd);
        return -1;
    }
    int ok = fprintf(file, "%s:%s:%s:%s\n", record->username_key, record->username,
                     record->salt, record->password_hash) > 0;
    if (fclose(file) == 0 && ok)
        return 1;
    return -1;
}

static void file_close(AccountStore *store)
{
    free(store);
}

AccountStore *accounts_open_file(const char *path)
{
    FileStore *self = (FileStore *)calloc(1, sizeof(*self));
    if (self == NULL)
        return NULL;
    self->base.description = "local file";
    self->base.lookup = file_lookup;
    self->base.insert = file_insert;
    self->base.close = file_close;
    snprintf(self->path, sizeof(self->path), "%s", path);
    return &self->base;
}
