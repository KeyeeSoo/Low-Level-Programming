#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#include "GameRules.h"

/*
 * Player accounts. Passwords are never stored: only a random per-account salt
 * and the PBKDF2-HMAC-SHA256 hash of salt + password.
 *
 * Storage is behind a small interface so the login logic does not care where
 * accounts live: MongoDB when a .env file provides a connection string, or a
 * local file otherwise (teammates, the marker, or no internet connection).
 */
#define ACCOUNT_SALT_HEX 33 /* 16 bytes as hex, plus NUL */
#define ACCOUNT_HASH_HEX 65 /* 32 bytes as hex, plus NUL */

typedef struct {
    char username[USERNAME_MAX + 1]; /* As the player typed it. */
    char username_key[USERNAME_MAX + 1]; /* Lower case: names ignore case. */
    char salt[ACCOUNT_SALT_HEX];
    char password_hash[ACCOUNT_HASH_HEX];
} AccountRecord;

typedef struct AccountStore AccountStore;
struct AccountStore {
    const char *description;
    /* 1 = found, 0 = no such account, -1 = storage error. */
    int (*lookup)(AccountStore *store, const char *username_key, AccountRecord *out);
    /* 1 = inserted, 0 = name already taken, -1 = storage error. */
    int (*insert)(AccountStore *store, const AccountRecord *record);
    void (*close)(AccountStore *store);
};

/* Local-file backend. Returns NULL only when out of memory. */
AccountStore *accounts_open_file(const char *path);
/* MongoDB backend. Returns NULL if the server cannot be reached. */
AccountStore *accounts_open_mongo(const char *uri);
/* Reads one KEY=value from a .env style file into out. Returns 1 if found. */
int accounts_read_env(const char *path, const char *key, char *out, unsigned long size);

/* Both return an AuthResult. On AUTH_OK, display_name gets the canonical name. */
int account_register(AccountStore *store, const char *username, const char *password,
                     char display_name[USERNAME_MAX + 1]);
int account_login(AccountStore *store, const char *username, const char *password,
                  char display_name[USERNAME_MAX + 1]);

#endif
