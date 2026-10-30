/* Verify variety, no duplicates, and variable-length recipes. */
#include "GameRules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    int seen_length[5]={0}, seen_ingredient[INGREDIENT_COUNT]={0};
    int first_recipe[RECIPE_MAX_INGREDIENTS]={0};int first_len=0, distinct=0;
    for(unsigned seed=1;seed<=500;seed++){
        GameRules g;game_init_seed(&g,seed);
        assert(g.recipe_len>=2 && g.recipe_len<=4);
        seen_length[g.recipe_len]++;
        int mask=0;
        for(int j=0;j<g.recipe_len;j++){
            int item=g.recipe[j];
            assert(item>=0 && item<INGREDIENT_COUNT);
            assert(!(mask&(1<<item)));mask|=1<<item;
            seen_ingredient[item]++;
        }
        for(int j=g.recipe_len;j<RECIPE_MAX_INGREDIENTS;j++)assert(g.recipe[j]==-1);
        if(seed==1){memcpy(first_recipe,g.recipe,sizeof(first_recipe));first_len=g.recipe_len;}
        else if(first_len!=g.recipe_len || memcmp(first_recipe,g.recipe,sizeof(first_recipe)))distinct++;
    }
    for(int len=2;len<=4;len++)assert(seen_length[len]>0);
    for(int i=0;i<INGREDIENT_COUNT;i++)assert(seen_ingredient[i]>0);
    assert(distinct>400);
    printf("PASS: 500 random seeds, 10 ingredient variety, lengths 2=%d 3=%d 4=%d\n",
           seen_length[2],seen_length[3],seen_length[4]);
    return 0;
}
