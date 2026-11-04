/* Run the real POSIX mixing loop with a deterministic 48kHz device/clock. */
#include <SDL2/SDL.h>
#include <time.h>
#include <unistd.h>
static int test_clock(clockid_t,struct timespec*);
static int test_sleep(useconds_t);
static void test_delay(Uint32);
static Uint32 test_queued(SDL_AudioDeviceID);
static int test_submit(SDL_AudioDeviceID,const void*,Uint32);
static SDL_AudioStatus test_status(SDL_AudioDeviceID);
#define clock_gettime test_clock
#define usleep test_sleep
#define SDL_Delay test_delay
#define SDL_GetQueuedAudioSize test_queued
#define SDL_QueueAudio test_submit
#define SDL_GetAudioDeviceStatus test_status
#define main ring_fixture_main
#include "test_audio_ring.c"
#undef main
#undef clock_gettime
#undef usleep
#undef SDL_Delay
#undef SDL_GetQueuedAudioSize
#undef SDL_QueueAudio
#undef SDL_GetAudioDeviceStatus

static uint64_t now_ns,end_ns,frame_fraction,queued_frames,largest_queue,events;
static uint64_t submit_at,submit_burst,largest_burst;
static int device,pause_once,pause_done,disconnect;
static int playing(void) { return device && !(disconnect && now_ns>=3000000000ULL && now_ns<5000000000ULL); }
static void advance(uint64_t ns)
{
    if(playing()) {
        uint64_t total=ns*48000ULL+frame_fraction;
        uint64_t consumed=total/1000000000ULL;
        frame_fraction=total%1000000000ULL;
        queued_frames=consumed<queued_frames?queued_frames-consumed:0;
    }
    now_ns+=ns;
    if(pause_once && !pause_done && now_ns>=2000000000ULL) {
        pause_done=1; advance(250000000ULL);
    }
    if(now_ns>=end_ns) s_mix_thread_running=0;
}
static int test_clock(clockid_t id,struct timespec* ts)
{ (void)id; ts->tv_sec=now_ns/1000000000ULL; ts->tv_nsec=now_ns%1000000000ULL; return 0; }
static int test_sleep(useconds_t us) { advance((uint64_t)us*1000);return 0; }
static void test_delay(Uint32 ms) { advance((uint64_t)ms*1000000); }
static Uint32 test_queued(SDL_AudioDeviceID id) { (void)id;return queued_frames*8; }
static SDL_AudioStatus test_status(SDL_AudioDeviceID id) { (void)id;return playing()?SDL_AUDIO_PLAYING:SDL_AUDIO_STOPPED; }
static int test_submit(SDL_AudioDeviceID id,const void* data,Uint32 len)
{
    (void)id;(void)data; queued_frames+=len/8;
    if(queued_frames>largest_queue) largest_queue=queued_frames;
    if(submit_at==now_ns) submit_burst++; else {submit_at=now_ns;submit_burst=1;}
    if(submit_burst>largest_burst) largest_burst=submit_burst;
    return 0;
}
int sys_event_queue_push_by_id(uint32_t q,uint64_t src,uint64_t a,uint64_t b,uint64_t c)
{ (void)q;(void)src;(void)a;(void)b;(void)c;events++;return 0; }
uint32_t sys_event_find_queue_by_key(uint64_t key) { (void)key;return 1; }
uint32_t sys_event_queue_create_direct(uint64_t key,int32_t size) { (void)key;(void)size;return 1; }

static uint64_t run(int have_device,int pause,int loss,unsigned seconds,int paced)
{
    setenv("PS3_AUDIO_PACING",paced?"1":"0",1);
    now_ns=frame_fraction=queued_frames=largest_queue=events=submit_burst=largest_burst=0;
    submit_at=UINT64_MAX; end_ns=(uint64_t)seconds*1000000000ULL;
    device=have_device; pause_once=pause; pause_done=0;disconnect=loss;
    s_sdl_audio_dev=have_device?1:0;
    memset(s_ports,0,sizeof s_ports); memset(s_notify_queues,0,sizeof s_notify_queues);
    s_notify_queues[0].in_use=1; s_notify_queues[0].key=1;
    AudioPortSlot* p=&s_ports[0];p->in_use=p->running=1;p->param.nChannel=2;p->param.nBlock=32;p->param.level=1;
    p->buffer=calloc(32*256*2,sizeof(float));assert(p->buffer);
    s_mix_thread_running=1;audio_mix_thread_func(NULL);
    assert(p->read_index==events);free(p->buffer);
    printf("device=%d pause=%d loss=%d paced=%d blocks=%llu max_queue=%llu max_burst=%llu\n",
        have_device,pause,loss,paced,(unsigned long long)events,(unsigned long long)largest_queue,(unsigned long long)largest_burst);
    return events;
}
int main(void)
{
    mutex_init(&s_audio_mutex);
    uint64_t n=run(1,0,0,60,1);
    assert(n>=11249 && n<=11255 && largest_queue<5*256);
    n=run(0,0,0,60,1);assert(n>=11249 && n<=11251);
    n=run(0,1,0,10,1);assert(n>=1825 && n<=1832); /* missed time is not replayed */
    n=run(1,1,0,10,1);assert(n>=1825 && n<=1836 && largest_queue<5*256 && largest_burst<=5);
    n=run(1,0,1,10,1);assert(n>=1868 && n<=1885 && largest_queue<5*256);
    n=run(1,0,0,60,0);assert(n>11900 && largest_queue>150000); /* original bug */
    mutex_destroy(&s_audio_mutex);
    puts("PASS: device clock, bounded queue, missing/stopped device, pause recovery, guest events; original backlog reproduced");
}
