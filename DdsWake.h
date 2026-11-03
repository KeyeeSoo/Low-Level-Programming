#ifndef DDS_WAKE_H
#define DDS_WAKE_H
#include <dds/dds.h>
#include <stdbool.h>
/* All children belong to participant and are deleted with it. */
static inline dds_entity_t wake_create(dds_entity_t participant,const dds_entity_t *readers,size_t count){
    dds_entity_t ws=dds_create_waitset(participant);
    if(ws<0)return ws;
    dds_return_t rc=dds_waitset_attach(ws,ws,0);
    if(rc<0){dds_delete(ws);return rc;}
    for(size_t i=0;i<count;i++){
        dds_entity_t condition=dds_create_readcondition(readers[i],DDS_ANY_STATE);
        if(condition<0){dds_delete(ws);return condition;}
        rc=dds_waitset_attach(ws,condition,(dds_attach_t)(i+1));
        if(rc<0){dds_delete(ws);return rc;}
    }
    return ws;
}
static inline void wake_notify(void *context){
    dds_entity_t ws=*(dds_entity_t *)context;
    if(ws>0)(void)dds_waitset_set_trigger(ws,true);
}
#endif
