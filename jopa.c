// vim: et ts=4 sw=4
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pulse/simple.h>
#include <jack/jack.h>

/////////////////////// Buffer ///////////////////////
typedef struct Buffer {
    size_t BufferSize;
    float *volatile Buffers[2];
    pthread_mutex_t Mutex;
    unsigned char CurrentBuffer;
    unsigned char FilledBufferCount;
} Buffer;

Buffer *NewBuffer(size_t BufferSize)
{
    Buffer *pBuf = malloc(sizeof(Buffer));
    pBuf->BufferSize = BufferSize;
    pBuf->CurrentBuffer = 0;
    pBuf->FilledBufferCount = 0;
    if (pthread_mutex_init(&pBuf->Mutex, NULL)) {
        fputs("Error: Cannot allocate mutex.\n", stderr);
        return NULL;
    }
    pBuf->Buffers[0] = malloc(BufferSize*sizeof **pBuf->Buffers);
    if(!pBuf->Buffers[0]) {
        fputs("Error: Cannot allocate memory.\n", stderr);
        pthread_mutex_destroy(&pBuf->Mutex);
        free(pBuf);
        return NULL;
    }
    pBuf->Buffers[1] = malloc(BufferSize*sizeof **pBuf->Buffers);
    if(!pBuf->Buffers[1]) {
        fputs("Error: Cannot allocate memory.\n", stderr);
        pthread_mutex_destroy(&pBuf->Mutex);
        free(pBuf->Buffers[0]);
        free(pBuf);
        return NULL;
    }
    return pBuf;
}

void DeleteBuffer(Buffer *pBuf)
{
    free(pBuf->Buffers[0]);
    free(pBuf->Buffers[1]);
    pthread_mutex_destroy(&pBuf->Mutex);
    free(pBuf);
}

inline void LockBuffer(Buffer *pBuf)
{
    pthread_mutex_lock(&pBuf->Mutex);
}

inline void UnlockBuffer(Buffer *pBuf)
{
    pthread_mutex_unlock(&pBuf->Mutex);
}

inline void SetBufferFilled(Buffer *pBuf)
{
    pBuf->FilledBufferCount++;
}

inline size_t GetBufferSize(Buffer *pBuf)
{
    return pBuf->BufferSize;
}

inline float *volatile GetUnusedBuffer(Buffer *pBuf)
{
    if(pBuf->FilledBufferCount == 2)
        return NULL;
    float *volatile UnusedBuffer = NULL;
    LockBuffer(pBuf);
    UnusedBuffer = pBuf->Buffers[pBuf->CurrentBuffer];
    pBuf->CurrentBuffer = !pBuf->CurrentBuffer;
    UnlockBuffer(pBuf);
    return UnusedBuffer;
}

inline float *volatile GetUsedBuffer(Buffer *pBuf)
{
    if(!pBuf->FilledBufferCount)
        return NULL;
    float *volatile UsedBuffer = NULL;
    LockBuffer(pBuf);
    if(pBuf->FilledBufferCount == 1)
        UsedBuffer = pBuf->Buffers[!pBuf->CurrentBuffer];
    else //if(pBuf->FilledBufferCount == 2)
        UsedBuffer = pBuf->Buffers[ pBuf->CurrentBuffer];
    pBuf->FilledBufferCount--;
    UnlockBuffer(pBuf);
    return UsedBuffer;
}

inline void WriteBufferToPulse(Buffer *pBuf, pa_simple *hPulse)
{
    float *volatile TmpBuffer = GetUsedBuffer(pBuf);
    if(TmpBuffer)
        pa_simple_write(hPulse, TmpBuffer, GetBufferSize(pBuf)*sizeof **pBuf->Buffers, NULL);
}

//////////////////// End of Buffer //////////////////////

jack_port_t *JackPorts[2];
jack_client_t *hJack = NULL;
pa_simple *hPulse = NULL;
pa_sample_spec PulseSample = {
    .format   = PA_SAMPLE_FLOAT32,
    .rate     = 48000, /* <== jack_get_sample_rate() */
    .channels = 2
};
int PortNameSize = 0; /* <== jack_port_name_size() */
char *TmpPortName = NULL;
Buffer *pOutputBuffer;

void cleanup()
{
    if(hJack) {
        jack_client_close(hJack);
        hJack = NULL;
    }
    if(hPulse) {
        pa_simple_free(hPulse);
        hPulse = NULL;
    }
    if(TmpPortName) {
        free(TmpPortName);
        TmpPortName = NULL;
    }
    if(pOutputBuffer) {
        DeleteBuffer(pOutputBuffer);
        pOutputBuffer = NULL;
    }
}

void JackOnError(const char *desc)
{
    fprintf(stderr, "JACK error: %s\n", desc);
}

void JackOnShutdown(void *arg)
{
    cleanup();
    exit(0);
}

int JackOnBufferSize(jack_nframes_t nframes, void *arg)
{
    if (pOutputBuffer)
        DeleteBuffer(pOutputBuffer);
    pOutputBuffer = NewBuffer(nframes<<1);
    if (!pOutputBuffer) {
        cleanup();
        exit(1);
    }
    fprintf(stderr, "Buffer size is %lu*2 channels*%lu bytes.\n", (unsigned long) nframes, (unsigned long) sizeof **pOutputBuffer->Buffers/*FIXME*/);
    return 0;
}

int JackOnProcess(jack_nframes_t nframes, void *arg)
{
    static float *L=NULL, *R=NULL;
    static jack_nframes_t i;
    static float *OutputBufferNext;
    OutputBufferNext = GetUnusedBuffer(pOutputBuffer);
    if (!OutputBufferNext) {
        fputs("Warning: PulseAudio got stuck!\n", stderr);
        return 0;
    }
    L=jack_port_get_buffer(JackPorts[0], nframes);
    R=jack_port_get_buffer(JackPorts[1], nframes);
    for(i=0; i<nframes && (i<<1)<GetBufferSize(pOutputBuffer); ++i) {
        OutputBufferNext[i<<1]=L[i];
        OutputBufferNext[(i<<1)|1]=R[i];
    }
    SetBufferFilled(pOutputBuffer);
    return 0;
}

void JackOnConnect(jack_port_id_t a, jack_port_id_t b, int connect, void *arg)
{
    const char *portaName = jack_port_name(jack_port_by_id(hJack, a));
    jack_port_t *portb = jack_port_by_id(hJack, b);
    fprintf(stderr, "Connect: %s %s %s\n", portaName, connect?"==>":"=X=", jack_port_name(portb));
    if((jack_port_flags(portb)&(JackPortIsPhysical|JackPortIsInput))==(JackPortIsPhysical|JackPortIsInput)) {
        snprintf(TmpPortName, PortNameSize, "%s:%s", jack_get_client_name(hJack), jack_port_short_name(portb));
        if(connect)
            jack_connect(hJack, portaName, TmpPortName);
        else if(jack_port_connected_to(jack_port_by_name(hJack, TmpPortName), portaName))
            jack_disconnect(hJack, portaName, TmpPortName);
    }
}

int main()
{
    hJack=jack_client_open("JACK over PulseAudio", JackNoStartServer, NULL);
    if(!hJack) {
        fputs("Failed to connect JACK. Is jackd running?\n", stderr);
        cleanup();
        return 1;
    }
    jack_set_error_function(JackOnError);
    jack_on_shutdown(hJack, JackOnShutdown, NULL);
    jack_set_buffer_size_callback(hJack, JackOnBufferSize, NULL);
    jack_set_process_callback(hJack, JackOnProcess, NULL);
    jack_set_port_connect_callback(hJack, JackOnConnect, NULL);
    PulseSample.rate=jack_get_sample_rate(hJack);
    hPulse=pa_simple_new(NULL, "JACK over PulseAudio", PA_STREAM_PLAYBACK, NULL, "jopa", &PulseSample, NULL, NULL, NULL);
    if(!hPulse) {
        fputs("Failed to connect PulseAudio. Is PulseAudio server running?\n", stderr);
        cleanup();
        return 1;
    }
    fprintf(stderr, "Sample rate is %luHz.\n", (unsigned long int) PulseSample.rate);
    PortNameSize=jack_port_name_size();
    TmpPortName=malloc(PortNameSize*sizeof *TmpPortName);
    if(!TmpPortName) {
        fputs("Error: Cannot allocate memory.\n", stderr);
        cleanup();
        return 1;
    }
    {
        unsigned int i;
        for(i=0; i<sizeof JackPorts/sizeof JackPorts[0]; ++i) {
            snprintf(TmpPortName, PortNameSize, "playback_%u", i+1);
            JackPorts[i]=jack_port_register(hJack, TmpPortName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        }
    }
    if(jack_activate(hJack)) {
        fputs("Failed to activate JACK client.\n", stderr);
        cleanup();
        return 1;
    }
    for(;;)
        WriteBufferToPulse(pOutputBuffer, hPulse);
}
