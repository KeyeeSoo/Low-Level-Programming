/* Authoritative two-player DDS server. Start before the clients.
   Builds with generated GameNet.c / GameNet.h from GameNet.idl. */
#include <dds/dds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "GameNet.h"
#include "GameLogic.h"

static dds_entity_t topic(dds_entity_t p, const dds_topic_descriptor_t *desc,
                          const char *name) {
    dds_entity_t t=dds_create_topic(p,desc,name,NULL,NULL);
    if(t<0) {fprintf(stderr,"Could not create DDS topic %s\n",name);exit(1);}
    return t;
}
static void snapshot(PotionSnapshot *s, const GameWorld *w) {
    memset(s,0,sizeof(*s));
    s->revision=w->revision;
    s->joined1=w->joined[0];s->joined2=w->joined[1];
    s->p1x=(int)w->x[0];s->p1y=(int)w->y[0];
    s->p2x=(int)w->x[1];s->p2y=(int)w->y[1];
    s->held1=w->held[0];s->held2=w->held[1];
    s->next_ingredient=w->next_ingredient;s->phase=w->phase;
    s->active_player=w->active_player;
    s->rune_mistakes=w->rune_mistakes;s->session=w->session;
}
int main(void) {
    dds_entity_t p=dds_create_participant(DDS_DOMAIN_DEFAULT,NULL,NULL);
    if(p<0){fprintf(stderr,"Cannot start DDS participant\n");return 1;}
    dds_entity_t tj=topic(p,&PotionJoin_desc,"PotionJoin");
    dds_entity_t tw=topic(p,&PotionWelcome_desc,"PotionWelcome");
    dds_entity_t tc=topic(p,&PotionCommand_desc,"PotionCommand");
    dds_entity_t ts=topic(p,&PotionSnapshot_desc,"PotionSnapshot");
    dds_entity_t jr=dds_create_reader(p,tj,NULL,NULL);
    dds_entity_t cw=dds_create_reader(p,tc,NULL,NULL);
    dds_entity_t ww=dds_create_writer(p,tw,NULL,NULL);
    dds_entity_t sw=dds_create_writer(p,ts,NULL,NULL);
    if(jr<0||cw<0||ww<0||sw<0){fprintf(stderr,"Cannot create DDS endpoints\n");return 1;}
    GameWorld world; game_init(&world);
    printf("Gingerbread Curse server started. Waiting for two DDS clients...\n");
    uint64_t previous=dds_time(), last_broadcast=0;
    for(;;) {
        PotionJoin join={0};
        void *jo[1]={&join};dds_sample_info_t ji[1];dds_return_t r;
        while((r=dds_take(jr,jo,ji,1,1))>0) {
            if(!ji[0].valid_data)continue;
            int id=game_join(&world,join.token);
            if(id) {
                PotionWelcome welcome={0};
                welcome.token=join.token;welcome.player=id;
                dds_write(ww,&welcome);
            }
        }
        PotionCommand cmd={0};
        void *co[1]={&cmd};dds_sample_info_t ci[1];
        while((r=dds_take(cw,co,ci,1,1))>0) {
            if(!ci[0].valid_data)continue;
            int old_phase=world.phase, old_next=world.next_ingredient;
            int old_mistakes=world.rune_mistakes;
            game_command(&world,cmd.player,cmd.token,
                         cmd.action,cmd.dx,cmd.dy,cmd.session);
            if(world.phase!=old_phase||world.next_ingredient!=old_next||
               world.rune_mistakes!=old_mistakes)
                printf("phase=%d ingredient=%d mistakes=%d\n",world.phase,
                       world.next_ingredient,world.rune_mistakes);
        }
        uint64_t now=dds_time();
        game_tick(&world,(float)(now-previous)/1000000000.0f);
        previous=now;
        if(now-last_broadcast>=DDS_MSECS(50)) {
            PotionSnapshot s;snapshot(&s,&world);
            dds_write(sw,&s);
            last_broadcast=now;
        }
        dds_sleepfor(DDS_MSECS(5));
    }
    return 0;
}
