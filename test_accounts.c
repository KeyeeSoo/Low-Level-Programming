/* Offline tests for account rules, hashing and the local-file store. */
#include "Accounts.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TEST_FILE "test_accounts.tmp"

int main(void)
{
    char name[USERNAME_MAX + 1], line[512];
    unlink(TEST_FILE);
    AccountStore *store = accounts_open_file(TEST_FILE);
    assert(store != NULL);
    /* A login before registering fails, without saying the name is unknown. */
    assert(account_login(store, "Alice", "secret1", name) == AUTH_BAD_CREDENTIALS);
    assert(account_register(store, "Alice", "secret1", name) == AUTH_OK);
    assert(strcmp(name, "Alice") == 0);
    assert(account_login(store, "Alice", "secret1", name) == AUTH_OK);
    assert(account_login(store, "Alice", "wrong-pass", name) == AUTH_BAD_CREDENTIALS);
    /* Names ignore case for login and uniqueness, but keep their original look. */
    assert(account_login(store, "ALICE", "secret1", name) == AUTH_OK);
    assert(strcmp(name, "Alice") == 0);
    assert(account_register(store, "alice", "other-pass", name) == AUTH_NAME_TAKEN);
    /* Invalid input, including characters that could corrupt the file format. */
    assert(account_register(store, "ab", "secret1", name) == AUTH_INVALID_INPUT);
    assert(account_register(store, "seventeen_chars_x", "secret1", name) == AUTH_INVALID_INPUT);
    assert(account_register(store, "bad:name", "secret1", name) == AUTH_INVALID_INPUT);
    assert(account_register(store, "bad name", "secret1", name) == AUTH_INVALID_INPUT);
    assert(account_register(store, "Bobby", "abc", name) == AUTH_INVALID_INPUT);
    /* The same password on two accounts must hash differently (per-account salt). */
    assert(account_register(store, "Bobby", "secret1", name) == AUTH_OK);
    AccountRecord alice, bobby;
    assert(store->lookup(store, "alice", &alice) == 1);
    assert(store->lookup(store, "bobby", &bobby) == 1);
    assert(strcmp(alice.salt, bobby.salt) != 0);
    assert(strcmp(alice.password_hash, bobby.password_hash) != 0);
    /* The password itself must never reach the disk. */
    FILE *file = fopen(TEST_FILE, "r");
    assert(file != NULL);
    while (fgets(line, sizeof(line), file) != NULL)
        assert(strstr(line, "secret1") == NULL);
    fclose(file);
    store->close(store);
    /* Accounts survive reopening the store. */
    store = accounts_open_file(TEST_FILE);
    assert(account_login(store, "Bobby", "secret1", name) == AUTH_OK);
    store->close(store);
    unlink(TEST_FILE);
    puts("PASS: accounts - register, login, salted hashes, validation, persistence");
    return 0;
}
