#include <stdio.h>
#include <pthread.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#define CallOrNull(func, var) { if((var)) { (func)((var)); (var) = NULL; } }

pthread_cond_t DataReadyPlay;
pthread_mutex_t DataReadyPlayMutex;
pa_simple *PulseAudio = NULL;
pa_sample_spec PulseSample = {
    .format = PA_SAMPLE_FLOAT32,
    .rate = 48000, /* jack_get_sample_rate */
    .channels = 2
};
jack_client_t *Jack = NULL;
int JackPortNameSize = 0; /* jack_port_name_size */
jack_nframes_t JackBufferSize = 0; /* jack_get_buffer_size() */
int isJackQuitting = 0;

void cleanup() {
    CallOrNull(pa_simple_free, PulseAudio);
    CallOrNull(jack_client_close, Jack);
}

void JackOnError(const char *desc) {
    fprintf(stderr, "JACK error: %s\n", desc);
    isJackQuitting = 1;
    pthread_cond_broadcast(&DataReadyPlay);
}

void JackOnShutdown(void *arg) {
    fputs("JACK server is shutting down, quitting.\n", stderr);
    isJackQuitting = 1;
    pthread_cond_broadcast(&DataReadyPlay);
}

int JackOnBufferSize(jack_nframes_t nframes, void *arg) {
    JackBufferSize = nframes;
    fprintf(stderr, "Buffer size is %lu samples.\n", nframes);
    return 0;
}

void JackOnConnect(jack_port_id_t a, jack_port_id_t b, int connect, void *arg) {
}

int JackOnProcess(jack_nframes_t nframes, void *arg) {
    return 0;
}

int main(int argc, char *argv[]) {
    int ErrorCode;

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
    jack_set_process_callback(Jack, JackOnProcess, NULL);
    jack_set_port_connect_callback(Jack, JackOnConnect, NULL);
    jack_set_buffer_size_callback(Jack, JackOnBufferSize, NULL);
    JackOnBufferSize(jack_get_buffer_size(Jack), NULL);
    JackPortNameSize = jack_port_name_size();
    PulseSample.rate = jack_get_sample_rate(Jack);
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
    fprintf(stderr, "Sample rate is %luHz.\n", (unsigned long int) PulseSample.rate);
    ErrorCode = jack_activate(Jack);
    if(ErrorCode) {
        fputs("Failed to activate JACK client.\n", stderr);
        cleanup();
        return 1;
    }
    pthread_mutex_lock(&DataReadyPlayMutex);
    for(;;) {
        pthread_cond_wait(&DataReadyPlay, &DataReadyPlayMutex);
    }
    cleanup();
    return 0;
}
