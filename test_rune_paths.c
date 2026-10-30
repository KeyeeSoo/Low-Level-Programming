#include "Trace.h"
#include <math.h>
#include <stdio.h>
#include <assert.h>
#define PI 3.14159265358979323846f
static TraceSession t;
static void follow(void) {
    trace_mouse_down(&t,t.target[0].x,t.target[0].y);
    assert(t.status==TRACE_DRAWING);
    for(int i=1;i<t.target_count;i++)
        trace_mouse_move(&t,t.target[i].x,t.target[i].y);
    trace_mouse_up(&t,t.target[t.target_count-1].x,t.target[t.target_count-1].y);
    assert(t.status==TRACE_SUCCESS);
    printf("Passed: %d points, accuracy %.0f%%\n",t.target_count,
           trace_accuracy(&t)*100);
}
int main(void) {
    TracePoint pentagram[5];
    int order[5]={0,2,4,1,3};
    for(int i=0;i<5;i++) {
        float a=-PI/2+2*PI*order[i]/5;
        pentagram[i]=(TracePoint){650+132*cosf(a),370+132*sinf(a)};
    }
    assert(trace_set_path(&t,pentagram,5,1));
    follow();
    TracePoint stirring[200];
    for(int i=0;i<200;i++) {
        float a=-PI/2+2*PI*i/200;
        stirring[i]=(TracePoint){650+102*cosf(a),370+102*sinf(a)};
    }
    assert(trace_set_path(&t,stirring,200,1));
    follow();
    return 0;
}
