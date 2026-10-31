#ifndef SECURE_DDS_H
#define SECURE_DDS_H

#include <dds/dds.h>

/*
 * Creates a DDS participant with DDS Security enabled, so all traffic is
 * authenticated and AES-GCM encrypted. role is "server" or "client", and picks
 * the identity and permissions in security/generated/. This fails closed:
 * with missing or invalid certificates there is no participant at all, never an
 * unencrypted one.
 */
dds_entity_t secure_create_participant(const char *role);

#endif
