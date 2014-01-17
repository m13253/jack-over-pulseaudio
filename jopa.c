#include <stdio.h>
#include <pthread.h>
#include <jack/jack.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#define CallOrNull(func, var) { if((var)) { (func)((var)); (var) = NULL; } }

pa_simple *PulseAudio = NULL;
pa_sample_spec PulseSample = {
    .format = PA_SAMPLE_FLOAT32,
    .rate = 48000,
    .channels = 2
};

void cleanup() {
    CallOrNull(pa_simple_free, PulseAudio);
}

int main(int argc, char *argv[]) {
    int ErrorCode;

    if(!(PulseAudio = pa_simple_new(
            /* server */ NULL, 
            /* name */ "Jack over PulseAudio",
            /* dir */ PA_STREAM_PLAYBACK,
            /* dev */ NULL,
            /* stream_name */ "Jack Audio Connection Kit output",
            /* ss */ &PulseSample,
            /* map */ NULL,
            /* attr */ NULL,
            /* error */ &ErrorCode
    ))) {
        fprintf(stderr, "Cannot open a stream for playback.\n");
    }
    cleanup();
    return 0;
}
