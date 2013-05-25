#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pulse/simple.h>
#include <jack/jack.h>

jack_port_t *JackPorts[2];
jack_client_t *hJack = NULL;
pa_simple *hPulse = NULL;
pa_sample_spec PulseSample;
int PortNameSize = 0;
char *TmpPortName = NULL;
jack_nframes_t OutputBufferSize = 0;
float *OutputBuffer = NULL;

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
    if(TmpPortName)
    {
        free(TmpPortName);
        TmpPortName = NULL;
    }
    if(OutputBuffer)
    {
        free(OutputBuffer);
        OutputBuffer = NULL;
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
    if(OutputBuffer)
        free(OutputBuffer);
    OutputBufferSize=nframes<<1;
    OutputBuffer=malloc(OutputBufferSize*sizeof *OutputBuffer);
    if(!OutputBuffer) {
        fputs("Error: Cannot allocate memory.\n", stderr);
        cleanup();
        exit(1);
    }
    fprintf(stderr, "Buffer size is %lu*2 channels*%lu bytes.\n", (unsigned long int) nframes, (unsigned long int) sizeof *OutputBuffer);
    return 0;
}

int JackOnProcess(jack_nframes_t nframes, void *arg)
{
    static float *L=NULL, *R=NULL;
    static jack_nframes_t i;
    L=jack_port_get_buffer(JackPorts[0], nframes);
    R=jack_port_get_buffer(JackPorts[1], nframes);
    for(i=0; i<nframes && (i<<1)<OutputBufferSize; ++i) {
        OutputBuffer[i<<1]=L[i];
        OutputBuffer[(i<<1)|1]=R[i];
    }
    pa_simple_write(hPulse, OutputBuffer, OutputBufferSize*sizeof *OutputBuffer, NULL);
    return 0;
}

void JackOnConnect(jack_port_id_t a, jack_port_id_t b, int connect, void *arg)
{
    const char *portaName = jack_port_name(jack_port_by_id(hJack, a));
    jack_port_t *portb = jack_port_by_id(hJack, b);
    fprintf(stderr, "Connect: %s %s %s\n", portaName, connect?"==>":"=X=", jack_port_name(portb));
    if((jack_port_flags(portb)&(JackPortIsPhysical|JackPortIsInput))==(JackPortIsPhysical|JackPortIsInput))
    {
        snprintf(TmpPortName, PortNameSize, "%s:%s", jack_get_client_name(hJack), jack_port_short_name(portb));
        if(connect)
            jack_connect(hJack, portaName, TmpPortName);
        else if(jack_port_connected_to(jack_port_by_name(hJack, TmpPortName), portaName))
            jack_disconnect(hJack, portaName, TmpPortName);
    }
}

int main()
{
    hJack=jack_client_open("JACK over PulseAudio", 0, NULL);
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
    
    PulseSample.format   = PA_SAMPLE_FLOAT32;
    PulseSample.rate     = jack_get_sample_rate(hJack);
    PulseSample.channels = 2;
    hPulse=pa_simple_new(NULL, "JACK over PulseAudio", PA_STREAM_PLAYBACK, NULL, "jopa", &PulseSample, NULL, NULL, NULL);
    if(!hPulse) {
        fputs("Failed to connect PulseAudio. Is PulseAudio server running?\n", stderr);
        cleanup();
        return 1;
    }
    fprintf(stderr, "Sample rate is %luHz.\n", (unsigned long int) PulseSample.rate);
    /* JackOnBufferSize(jack_get_buffer_size(hJack), NULL);
       Seems it automatically calls this, no need to call it manually. */
    PortNameSize=jack_port_name_size();
    TmpPortName=malloc(PortNameSize*sizeof *TmpPortName);
    if(!TmpPortName)
    {
        fputs("Error: Cannot allocate memory.\n", stderr);
        cleanup();
        return 1;
    }
    {
    unsigned int i;
    for(i=0; i<sizeof JackPorts/sizeof JackPorts[0]; ++i)
    {
        snprintf(TmpPortName, PortNameSize, "playback_%u", i+1);
        JackPorts[i]=jack_port_register(hJack, TmpPortName, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    }
    }
    if(jack_activate(hJack)) {
        fputs("Failed to activate JACK client.\n", stderr);
        cleanup();
        return 1;
    }
    for(;;) sleep(1);
}
