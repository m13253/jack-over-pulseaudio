#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#define nChannels 2

#define CallOrNull(func, var) { if((var)) { (func)((var)); (var) = NULL; } }

struct thread_connect_arg {
    jack_port_t *a;
    jack_port_t *b;
    int connect;
    int type;
};

pthread_cond_t DataReadyPlay;
pthread_mutex_t DataReadyPlayMutex;
jack_ringbuffer_t *volatile BufferPlay[nChannels] = {NULL};
jack_port_t *JackPortPlay[nChannels];
pa_simple *PulseAudio = NULL;
pa_sample_spec PulseSample = {
    .format = PA_SAMPLE_FLOAT32,
    .rate = 48000, /* jack_get_sample_rate */
    .channels = nChannels
};
jack_client_t *Jack = NULL;
size_t JackPortNameSize = 0; /* jack_port_name_size */
volatile jack_nframes_t JackBufferSize = 0; /* jack_get_buffer_size() */
volatile int isJackQuitting = 0;

void cleanup() {
    unsigned int i;
    CallOrNull(pa_simple_free, PulseAudio);
    CallOrNull(jack_client_close, Jack);
    for(i = 0; i<nChannels; i++)
        CallOrNull(jack_ringbuffer_free, BufferPlay[i]);
}

void JackOnError(const char *desc) {
    fprintf(stderr, "JACK error: %s\n", desc);
}

void JackOnShutdown(void *arg) {
    fputs("JACK server is shutting down, quitting.\n", stderr);
    isJackQuitting = 1;
    pthread_cond_broadcast(&DataReadyPlay);
}

int JackOnBufferSize(jack_nframes_t nframes, void *arg) {
    jack_ringbuffer_t *tmpBufferPlay1, *tmpBufferPlay2;
    unsigned int i;
    if(nframes == JackBufferSize)
        return 0;
    JackBufferSize = nframes;
    fprintf(stderr, "Buffer size is %lu samples.\n", (unsigned long int) nframes);
    for(i = 0; i<nChannels; i++) {
        tmpBufferPlay1 = jack_ringbuffer_create(JackBufferSize*sizeof (float)*2+2);
        if(!tmpBufferPlay1) {
            fputs("Failed to allocate buffer.\n", stderr);
            isJackQuitting = 1;
            pthread_cond_broadcast(&DataReadyPlay);
            return 1;
        }
        tmpBufferPlay2 = BufferPlay[i];
        BufferPlay[i] = tmpBufferPlay1;
        if(tmpBufferPlay2)
            jack_ringbuffer_free(tmpBufferPlay2);
    }
    return 0;
}

void *DoJackConnect(void *_ThreadArg) {
#define ThreadArg ((struct thread_connect_arg *) _ThreadArg)
    char *TmpPortName = malloc(JackPortNameSize*sizeof (char));
    snprintf(TmpPortName, JackPortNameSize, "%s:%s", jack_get_client_name(Jack), jack_port_short_name(ThreadArg->b));
    if(ThreadArg->connect)
        jack_connect(Jack, jack_port_name(ThreadArg->a), TmpPortName);
    else {
        jack_port_t *TmpPort = jack_port_by_name(Jack, TmpPortName);
        const char *PortAName = jack_port_name(ThreadArg->a);
        if(TmpPort && jack_port_connected_to(TmpPort, PortAName))
            jack_disconnect(Jack, PortAName, TmpPortName);
    }
    CallOrNull(free, _ThreadArg);
    return NULL;
#undef ThreadArg
}

void JackOnConnect(jack_port_id_t a, jack_port_id_t b, int connect, void *arg) {
    jack_port_t *porta = jack_port_by_id(Jack, a),
                *portb = jack_port_by_id(Jack, b);
    fprintf(stderr, "Connect: %s %s %s\n", jack_port_name(porta), connect ? "==>" : "=X=", jack_port_name(portb));
    if((jack_port_flags(portb) & (JackPortIsPhysical|JackPortIsInput))==(JackPortIsPhysical|JackPortIsInput)) {
        pthread_t ThreadConnect;
        struct thread_connect_arg *ThreadArg = malloc(sizeof (struct thread_connect_arg));
        ThreadArg->a = porta;
        ThreadArg->b = portb;
        ThreadArg->connect = connect;
        ThreadArg->type = 0;
        pthread_create(&ThreadConnect, NULL, DoJackConnect, ThreadArg);
    }
}

int JackOnProcess(jack_nframes_t nframes, void *arg) {
    static float *buf[nChannels] = {NULL};
    size_t BytesWritten;
    unsigned int i;
    for(i = 0; i<nChannels; i++)
        buf[i] = jack_port_get_buffer(JackPortPlay[i], nframes);
    for(i = 0; i<nChannels; i++) {
        BytesWritten = jack_ringbuffer_write(BufferPlay[i], (void *) buf[i], nframes*sizeof (float));
        if(BytesWritten!=nframes*sizeof (float))
            fprintf(stderr, "Playback buffer overflow: %zu!=%zu\n", BytesWritten, nframes*sizeof (float));
    }
    pthread_cond_broadcast(&DataReadyPlay);
    return 0;
}

int main(int argc, char *argv[]) {
    int ErrorCode;
    unsigned int i;

    ErrorCode = pthread_cond_init(&DataReadyPlay, NULL);
    if(ErrorCode) {
        fputs("Failed on pthread_cond_init.\n", stderr);
        return 1;
    }
    ErrorCode = pthread_mutex_init(&DataReadyPlayMutex, NULL);
    if(ErrorCode) {
        fputs("Failed on pthread_mutex_init.\n", stderr);
        return 1;
    }
    Jack = jack_client_open("JACK over PulseAudio", JackNoStartServer, NULL);
    if(!Jack) {
        fputs("Failed to connect to JACK. Run 'jackd -d dummy' first.\n", stderr);
        cleanup();
        return 1;
    }
    isJackQuitting = 0;
    jack_set_error_function(JackOnError);
    jack_on_shutdown(Jack, JackOnShutdown, NULL);
    PulseSample.rate = jack_get_sample_rate(Jack);
    fprintf(stderr, "Sample rate is %"PRIu32"Hz.\n", (uint32_t) PulseSample.rate);
    jack_set_buffer_size_callback(Jack, JackOnBufferSize, NULL);
    JackOnBufferSize(jack_get_buffer_size(Jack), (void *) 1);
    JackPortNameSize = jack_port_name_size();
    jack_set_port_connect_callback(Jack, JackOnConnect, NULL);
    jack_set_process_callback(Jack, JackOnProcess, NULL);
    {
        char TmpPortName[20];
        for(i = 0; i<nChannels; i++) {
            snprintf(TmpPortName, 20, "playback_%u", i+1);
            JackPortPlay[i] = jack_port_register(Jack, TmpPortName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        }
    }
    PulseAudio = pa_simple_new(
            /* server */ NULL, 
            /* name */ "Jack over PulseAudio",
            /* dir */ PA_STREAM_PLAYBACK,
            /* dev */ NULL,
            /* stream_name */ "Jack Audio Connection Kit output",
            /* ss */ &PulseSample,
            /* map */ NULL,
            /* attr */ NULL,
            /* error */ &ErrorCode
    );
    if(!PulseAudio) {
        fprintf(stderr, "Cannot open a stream for playback: %s\n", pa_strerror(ErrorCode));
        cleanup();
        return 1;
    }
    ErrorCode = jack_activate(Jack);
    if(ErrorCode) {
        fputs("Failed to activate JACK client.\n", stderr);
        cleanup();
        return 1;
    }
    pthread_mutex_lock(&DataReadyPlayMutex);
    while(!isJackQuitting) {
        static size_t PulseBufferSize = 0;
        static float *PulseBufferPlay[nChannels] = {NULL};
        static float *PulseBufferPlayMix = NULL;
        static size_t PulseBufferPlayReadCount[nChannels] = {0};
        uintptr_t j;
        if(JackBufferSize!=PulseBufferSize) { /* Update buffer size */
            PulseBufferSize = JackBufferSize;
            CallOrNull(free, PulseBufferPlayMix);
            PulseBufferPlayMix = malloc(PulseBufferSize*sizeof (float)*nChannels*2);
            for(i = 0; i<nChannels; i++) {
                CallOrNull(free, PulseBufferPlay[i]);
                PulseBufferPlay[i] = malloc(PulseBufferSize*sizeof (float));
            }
        }
        pthread_cond_wait(&DataReadyPlay, &DataReadyPlayMutex);
        if(isJackQuitting)
            break;
        for(i = 0; i<nChannels; i++) {
            PulseBufferPlayReadCount[i] = jack_ringbuffer_read(BufferPlay[i], (void *) PulseBufferPlay[i], PulseBufferSize*sizeof (float))/sizeof (float);
            if(PulseBufferPlayReadCount[i]!=PulseBufferSize)
                fprintf(stderr, "Playback buffer underflow: %zu!=%zu\n", PulseBufferPlayReadCount[i], PulseBufferSize);
        }
        memset(PulseBufferPlayMix, 0, nChannels*sizeof (float));
        for(i = 0; i<nChannels; i++)
            for(j = 0; j<PulseBufferPlayReadCount[i]; j++)
                PulseBufferPlayMix[j*nChannels+i] = PulseBufferPlay[i][j];
        pa_simple_write(PulseAudio, PulseBufferPlayMix, PulseBufferSize*nChannels*sizeof (float), NULL);
    }
    pthread_mutex_unlock(&DataReadyPlayMutex);
    cleanup();
    return 0;
}
