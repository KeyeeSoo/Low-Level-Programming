#ifndef AUTH_WORKER_H
#define AUTH_WORKER_H

#include "GameRules.h"
#include "ServerBridge.h"

/*
 * A dedicated server thread for logins. Password hashing is deliberately slow and
 * MongoDB waits on the network: on the main thread either would freeze the game,
 * and on the DDS thread they would delay every player's traffic.
 *
 *   DDS thread -> auth_worker_submit() -> auth thread -> bridge event -> main thread
 */
typedef struct {
    int nonce;
    int mode;
    char username[USERNAME_MAX + 1];
    char password[PASSWORD_MAX + 1];
} AuthJob;

int auth_worker_start(ServerBridge *bridge);
int auth_worker_submit(const AuthJob *job); /* Copies the job; returns 0 if full. */
void auth_worker_stop(void);

#endif
