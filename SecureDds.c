#define _POSIX_C_SOURCE 200809L
#include "SecureDds.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Set by build.sh: distributions install the security plugins in different places. */
#ifndef DDS_SECURITY_LIB_DIR
#define DDS_SECURITY_LIB_DIR "/usr/lib/x86_64-linux-gnu/libddsc0debian"
#endif
#define SECURITY_DIR "security/generated"

static void set_file_property(dds_qos_t *qos, const char *name, const char *cwd, const char *file)
{
    char value[1024];
    snprintf(value, sizeof(value), "file:%s/" SECURITY_DIR "/%s", cwd, file);
    dds_qset_prop(qos, name, value);
}

dds_entity_t secure_create_participant(const char *role)
{
    char cwd[768], identity[64], key[64];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        return DDS_RETCODE_ERROR;
    }
    snprintf(identity, sizeof(identity), "%s.pem", role);
    snprintf(key, sizeof(key), "%s.key", role);
    if (access(SECURITY_DIR "/ca.pem", R_OK) != 0)
        fprintf(stderr, "Missing " SECURITY_DIR ". Run build.sh (or run.bat) from the project folder.\n");
    dds_qos_t *qos = dds_create_qos();
    if (qos == NULL)
        return DDS_RETCODE_OUT_OF_RESOURCES;
    dds_qset_prop(qos, "dds.sec.auth.library.path", DDS_SECURITY_LIB_DIR "/libdds_security_auth.so");
    dds_qset_prop(qos, "dds.sec.auth.library.init", "init_authentication");
    dds_qset_prop(qos, "dds.sec.auth.library.finalize", "finalize_authentication");
    dds_qset_prop(qos, "dds.sec.crypto.library.path", DDS_SECURITY_LIB_DIR "/libdds_security_crypto.so");
    dds_qset_prop(qos, "dds.sec.crypto.library.init", "init_crypto");
    dds_qset_prop(qos, "dds.sec.crypto.library.finalize", "finalize_crypto");
    dds_qset_prop(qos, "dds.sec.access.library.path", DDS_SECURITY_LIB_DIR "/libdds_security_ac.so");
    dds_qset_prop(qos, "dds.sec.access.library.init", "init_access_control");
    dds_qset_prop(qos, "dds.sec.access.library.finalize", "finalize_access_control");
    set_file_property(qos, "dds.sec.auth.identity_ca", cwd, "ca.pem");
    set_file_property(qos, "dds.sec.auth.identity_certificate", cwd, identity);
    set_file_property(qos, "dds.sec.auth.private_key", cwd, key);
    set_file_property(qos, "dds.sec.access.permissions_ca", cwd, "ca.pem");
    set_file_property(qos, "dds.sec.access.governance", cwd, "governance.p7s");
    set_file_property(qos, "dds.sec.access.permissions", cwd, "permissions.p7s");
    dds_entity_t participant = dds_create_participant(DDS_DOMAIN_DEFAULT, qos, NULL);
    dds_delete_qos(qos);
    return participant;
}
