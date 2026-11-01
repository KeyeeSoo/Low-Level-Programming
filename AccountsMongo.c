/* MongoDB account store: the same fields and format as a web-app "users"
 * collection, in its own database. Used only from the server's auth thread;
 * a mongoc client is not thread-safe, and the network wait must not touch the game. */
#define _POSIX_C_SOURCE 200809L
#include "Accounts.h"

#include <mongoc/mongoc.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MONGO_DATABASE "gingerbread"
#define MONGO_COLLECTION "users"
#define MONGO_TIMEOUT_MS 5000
#define MONGO_DUPLICATE_KEY 11000

typedef struct {
    AccountStore base;
    mongoc_client_t *client;
    mongoc_collection_t *users;
} MongoStore;

static int mongo_lookup(AccountStore *store, const char *username_key, AccountRecord *out)
{
    MongoStore *self = (MongoStore *)store;
    bson_t *filter = BCON_NEW("username_key", BCON_UTF8(username_key));
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(self->users, filter, NULL, NULL);
    const bson_t *doc;
    bson_error_t error;
    int result = 0;
    if (mongoc_cursor_next(cursor, &doc)) {
        bson_iter_t it;
        memset(out, 0, sizeof(*out));
        snprintf(out->username_key, sizeof(out->username_key), "%s", username_key);
        if (bson_iter_init_find(&it, doc, "username") && BSON_ITER_HOLDS_UTF8(&it))
            snprintf(out->username, sizeof(out->username), "%s", bson_iter_utf8(&it, NULL));
        if (bson_iter_init_find(&it, doc, "salt") && BSON_ITER_HOLDS_UTF8(&it))
            snprintf(out->salt, sizeof(out->salt), "%s", bson_iter_utf8(&it, NULL));
        if (bson_iter_init_find(&it, doc, "password_hash") && BSON_ITER_HOLDS_UTF8(&it))
            snprintf(out->password_hash, sizeof(out->password_hash), "%s", bson_iter_utf8(&it, NULL));
        result = 1;
    }
    if (mongoc_cursor_error(cursor, &error)) {
        fprintf(stderr, "[accounts] MongoDB find failed: %s\n", error.message);
        result = -1;
    }
    mongoc_cursor_destroy(cursor);
    bson_destroy(filter);
    return result;
}

static int mongo_insert(AccountStore *store, const AccountRecord *record)
{
    MongoStore *self = (MongoStore *)store;
    bson_error_t error;
    bson_t *doc = BCON_NEW("username", BCON_UTF8(record->username),
                           "username_key", BCON_UTF8(record->username_key),
                           "password_format", BCON_UTF8("pbkdf2-sha256-v1"),
                           "salt", BCON_UTF8(record->salt),
                           "password_hash", BCON_UTF8(record->password_hash),
                           "created_at", BCON_DATE_TIME((int64_t)time(NULL) * 1000));
    int result = 1;
    if (!mongoc_collection_insert_one(self->users, doc, NULL, NULL, &error)) {
        /* The unique index settles two players registering one name at once. */
        result = -1;
        if (error.code == MONGO_DUPLICATE_KEY)
            result = 0;
        if (result < 0)
            fprintf(stderr, "[accounts] MongoDB insert failed: %s\n", error.message);
    }
    bson_destroy(doc);
    return result;
}

static void mongo_close(AccountStore *store)
{
    MongoStore *self = (MongoStore *)store;
    mongoc_collection_destroy(self->users);
    mongoc_client_destroy(self->client);
    free(self);
    mongoc_cleanup();
}

static int ensure_unique_index(mongoc_collection_t *users)
{
    /* A raw createIndexes command: mongoc_index_model_t is missing from the
     * older libmongoc that Ubuntu 22.04 installs. */
    bson_t *command = BCON_NEW("createIndexes", BCON_UTF8(MONGO_COLLECTION),
                               "indexes", "[", "{",
                                   "key", "{", "username_key", BCON_INT32(1), "}",
                                   "name", BCON_UTF8("username_key_1"),
                                   "unique", BCON_BOOL(true),
                               "}", "]");
    bson_error_t error;
    int ok = mongoc_collection_write_command_with_opts(users, command, NULL, NULL, &error);
    if (!ok)
        fprintf(stderr, "[accounts] MongoDB index failed: %s\n", error.message);
    bson_destroy(command);
    return ok;
}

AccountStore *accounts_open_mongo(const char *uri_text)
{
    bson_error_t error;
    mongoc_init();
    mongoc_uri_t *uri = mongoc_uri_new_with_error(uri_text, &error);
    if (uri == NULL) {
        /* error.message can quote the URI, which holds the password: don't print it. */
        fprintf(stderr, "[accounts] MONGODB_URI in .env is not a valid connection string\n");
        mongoc_cleanup();
        return NULL;
    }
    mongoc_uri_set_option_as_int32(uri, MONGOC_URI_SERVERSELECTIONTIMEOUTMS, MONGO_TIMEOUT_MS);
    mongoc_uri_set_option_as_int32(uri, MONGOC_URI_CONNECTTIMEOUTMS, MONGO_TIMEOUT_MS);
    mongoc_client_t *client = mongoc_client_new_from_uri(uri);
    mongoc_uri_destroy(uri);
    if (client == NULL) {
        mongoc_cleanup();
        return NULL;
    }
    mongoc_client_set_appname(client, "gingerbread-server");
    mongoc_collection_t *users = mongoc_client_get_collection(client, MONGO_DATABASE, MONGO_COLLECTION);
    /* Creating the index doubles as a connection and write-permission check. */
    MongoStore *self = NULL;
    if (ensure_unique_index(users))
        self = (MongoStore *)calloc(1, sizeof(*self));
    if (self == NULL) {
        mongoc_collection_destroy(users);
        mongoc_client_destroy(client);
        mongoc_cleanup();
        return NULL;
    }
    self->base.description = "MongoDB";
    self->base.lookup = mongo_lookup;
    self->base.insert = mongo_insert;
    self->base.close = mongo_close;
    self->client = client;
    self->users = users;
    return &self->base;
}
