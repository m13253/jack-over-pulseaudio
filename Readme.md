JACK Audio Connection Kit over PulseAudio (jopa)
================================================

This program bridges JACK Audio Connection Kit to PulseAudio, but sacrifices audio latency.

Usage
-----

```
$ make
$ jackd -d dummy &
$ ./jopa &
```

For fluent playback, it is recommended to set JACK buffer size to no less than 1024 frames/sec.

For better sound quality, it is recommend to set PulseAudio sample format the same as JACK (by default, 48000 Hz, 32-bit float).
