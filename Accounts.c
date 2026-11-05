// Login rules, password hashing and the local-file account store
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

/* Slow on purpose: makes guessing passwords from a stolen store expensive,
 * runs on the server's auth thread so it can never stall the game loop */
#define PBKDF2_ITERATIONS 200000
// random salt per account, so two same passwords still hash differently
#define SALT_BYTES 16
// SHA-256 gives 32 bytes
#define HASH_BYTES 32

// turn raw bytes into a hex string so they can be saved as text
static void to_hex(const unsigned char *bytes, int count, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < count; i++) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[count * 2] = '\0';
}

// hash the password with its salt using PBKDF2-SHA256
static int hash_password(const char *password, const char *salt_hex, char *hash_hex)
{
    unsigned char hash[HASH_BYTES];
    if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                          (const unsigned char *)salt_hex, (int)strlen(salt_hex),
                          PBKDF2_ITERATIONS, EVP_sha256(), HASH_BYTES, hash) != 1)
        return 0;
    to_hex(hash, HASH_BYTES, hash_hex);
    // wipe the raw hash from memory
    OPENSSL_cleanse(hash, sizeof(hash));
    return 1;
}

// Letters, digits, _ and -, so a name is always safe in a file, a query or on screen
static int valid_username(const char *username, char *key)
{
    size_t length = strlen(username);
    if (length < USERNAME_MIN || length > USERNAME_MAX)
        return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)username[i];
        if (!isalnum(c) && c != '_' && c != '-')
            return 0;
        // lowercase copy so "Bob" and "bob" count as the same name
        key[i] = (char)tolower(c);
    }
    key[length] = '\0';
    return 1;
}

// only checks the length, any characters are fine
static int valid_password(const char *password)
{
    size_t length = strlen(password);
    return length >= PASSWORD_MIN && length <= PASSWORD_MAX;
}

// sign up: check the input, make a random salt, hash and save the account
int account_register(AccountStore *store, const char *username, const char *password,
                     char display_name[USERNAME_MAX + 1])
{
    AccountRecord record;
    unsigned char salt[SALT_BYTES];
    memset(&record, 0, sizeof(record));
    if (!valid_username(username, record.username_key) || !valid_password(password))
        return AUTH_INVALID_INPUT;
    // make a fresh random salt from OpenSSL
    if (RAND_bytes(salt, SALT_BYTES) != 1)
        return AUTH_STORE_ERROR;
    // we only ever store the salt and the hash, never the password itself
    to_hex(salt, SALT_BYTES, record.salt);
    if (!hash_password(password, record.salt, record.password_hash))
        return AUTH_STORE_ERROR;
    snprintf(record.username, sizeof(record.username), "%s", username);
    // 1 = saved, 0 = name already taken, -1 = storage error
    int inserted = store->insert(store, &record);
    if (inserted < 0)
        return AUTH_STORE_ERROR;
    if (inserted == 0)
        return AUTH_NAME_TAKEN;
    snprintf(display_name, USERNAME_MAX + 1, "%s", record.username);
    return AUTH_OK;
}

// log in: look up the account and check the password hash matches
int account_login(AccountStore *store, const char *username, const char *password,
                  char display_name[USERNAME_MAX + 1])
{
    AccountRecord record;
    char key[USERNAME_MAX + 1];
    char hash[ACCOUNT_HASH_HEX];
    /* One message for "no such user" and "wrong password": the response must
     * not reveal which usernames exist */
    if (!valid_username(username, key) || !valid_password(password))
        return AUTH_BAD_CREDENTIALS;
    // 1 = found, 0 = no such user, -1 = storage error
    int found = store->lookup(store, key, &record);
    if (found < 0)
        return AUTH_STORE_ERROR;
    if (found == 0) {
        // Spend the same time as a real check, so timing doesn't reveal it either
        hash_password(password, "00000000000000000000000000000000", hash);
        return AUTH_BAD_CREDENTIALS;
    }
    // hash what they typed with the saved salt, then compare
    if (!hash_password(password, record.salt, hash))
        return AUTH_STORE_ERROR;
    // Constant-time comparison: no early exit at the first differing byte
    if (strlen(record.password_hash) != HASH_BYTES * 2 ||
        CRYPTO_memcmp(hash, record.password_hash, HASH_BYTES * 2) != 0)
        return AUTH_BAD_CREDENTIALS;
    snprintf(display_name, USERNAME_MAX + 1, "%s", record.username);
    return AUTH_OK;
}

// read one KEY=value line from the .env file
int accounts_read_env(const char *path, const char *key, char *out, unsigned long size)
{
    FILE *file = fopen(path, "r");
    if (file == NULL)
        return 0;
    char line[1024];
    size_t key_length = strlen(key);
    int found = 0;
    while (!found && fgets(line, sizeof(line), file) != NULL) {
        // skip lines that are not our key
        if (strncmp(line, key, key_length) != 0 || line[key_length] != '=')
            continue;
        char *value = line + key_length + 1;
        // cut off the newline at the end
        value[strcspn(value, "\r\n")] = '\0';
        // Allow KEY="value" as well as KEY=value
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
    // the line could have a secret in it, so wipe it
    OPENSSL_cleanse(line, sizeof(line));
    return found;
}

// Local file: one "key:username:salt:hash" line per account
typedef struct {
    AccountStore base;
    char path[512];
} FileStore;

// find an account in the file by its lowercase name
static int file_lookup(AccountStore *store, const char *username_key, AccountRecord *out)
{
    FileStore *self = (FileStore *)store;
    FILE *file = fopen(self->path, "r");
    if (file == NULL)
        return 0; // no file yet just means no accounts yet
    char line[256];
    int found = 0;
    while (!found && fgets(line, sizeof(line), file) != NULL) {
        char *save = NULL;
        // split the line into its 4 parts
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

// add a new account line, unless the name is already taken
static int file_insert(AccountStore *store, const AccountRecord *record)
{
    FileStore *self = (FileStore *)store;
    AccountRecord existing;
    // name already taken
    if (file_lookup(store, record->username_key, &existing) == 1)
        return 0;
    // Only the auth thread writes this file, so check-then-append is safe
    // only our user can read or write the file
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

// set up the local file store, used when MongoDB is not available
AccountStore *accounts_open_file(const char *path)
{
    FileStore *self = (FileStore *)calloc(1, sizeof(*self));
    if (self == NULL)
        return NULL;
    // plug in the file versions of lookup, insert and close
    self->base.description = "local file";
    self->base.lookup = file_lookup;
    self->base.insert = file_insert;
    self->base.close = file_close;
    snprintf(self->path, sizeof(self->path), "%s", path);
    return &self->base;
}
