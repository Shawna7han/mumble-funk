// Copyright The Mumble-Funk Contributors. BSD-style license: see LICENSE.
/* Independent scheduling model using existing BSD Speex; not Android/JNI.
 * Receive-state rules checked against Humla 4df8e7f3f45c9300864cca514e868032448c2baa.
 * No Humla source code copied. Timing is deterministic, device AudioTrack is not run. */
#include <stdio.h>
#include <math.h>
#include "speex/speex_jitter.h"
static JitterBuffer *create(int margin) {
    JitterBuffer *j=jitter_buffer_init(480); int value=margin*48;
    jitter_buffer_ctl(j,JITTER_BUFFER_SET_MARGIN,&value);return j;
}
static int run(int batch,int gap,int margin,int split,int duration) {
    JitterBuffer *j=0;int dying=0,played=0,filled=0,retired=-1,misses=0;float average=1;
    for(int ms=0;ms<duration+1600;ms+=10) {
        unsigned seq=0;char payload=0;
        if(ms<=duration && ms%20==0){seq=ms/10;payload=(ms==duration && split)?1:2;}
        if(ms>=duration+gap && ms<=duration+60+gap && (ms-duration-gap)%20==0){seq=(split?duration/10+102:duration/10+2)+(ms-duration-gap)/10;payload=ms==duration+60+gap?5:4;}
        if(payload){
            if(!j){j=create(margin);dying=0;filled=0;misses=0;}
            JitterBufferPacket p={0};p.data=&payload;p.len=1;p.span=960;p.timestamp=seq*480;
            jitter_buffer_put(j,&p);
        }
        if(!j || ms%batch)continue;
        if(dying){jitter_buffer_destroy(j);j=0;dying=0;retired=ms;continue;}
        /* Decode enough samples to satisfy one device output request. */
        while(filled<batch*48){
            int available=0;jitter_buffer_ctl(j,JITTER_BUFFER_GET_AVAILABLE_COUNT,&available);
            if(jitter_buffer_get_pointer_timestamp(j)==0 && available<(int)ceilf(average)) {
                if(++misses<20){filled+=480;continue;}
            }
            char data[8]={0};JitterBufferPacket p={0};p.data=data;p.len=sizeof(data);
            int rc=jitter_buffer_get(j,&p,480,0);int samples=480;
            if(rc==JITTER_BUFFER_OK){
                samples=960;misses=0;
                average=available>=average?available:average*.99f;
                if(data[0]>=4)++played;
                if(data[0]==1 || data[0]==5)dying=1;
                jitter_buffer_update_delay(j,0,0);
            }else {jitter_buffer_update_delay(j,&p,0);if(++misses>10)dying=1;}
            for(int n=0;n<samples/480;++n)jitter_buffer_tick(j);
            filled+=samples;
        }
        filled-=batch*48;
    }
    if(j)jitter_buffer_destroy(j);
    printf("duration_ms=%d split=%d batch_ms=%d gap_ms=%d margin_ms=%d close_played=%d/4 last_retire_ms=%d\n",duration,split,batch,gap,margin,played,retired);
    return played;
}
int main(void){
    int failures=0;int batches[]={10,20,40,60,120};int durations[]={2000,62000};
    /* Reproduce the old loss AND why a fixed 180-ms pause is not universal. */
    failures+=run(20,60,100,1,2000)!=0;
    failures+=run(120,180,100,1,2000)!=0;
    failures+=run(120,300,100,1,2000)!=4;
    for(unsigned d=0;d<sizeof(durations)/sizeof(*durations);++d)
        for(unsigned b=0;b<sizeof(batches)/sizeof(*batches);++b)
            failures+=run(batches[b],20,100,0,durations[d])!=4;
    puts("Scheduling model only: no Android AudioTrack/JNI or physical-device hearing test.");
    return failures?1:0;
}
