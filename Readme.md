JACK Audio Connection Kit over PulseAudio (jopa)
================================================

This program bridges JACK Audio Connection Kit to PulseAudio, but sacrifices audio latency.

Usage
-----

```
$ make
$ jackd -d dummy -p 1024 &
$ ./jopa &
```

For fluent playback, it is recommended to set JACK buffer size to no less than 1024 frames/sec.

For better sound quality, it is recommend to set PulseAudio sample format the same as JACK (by default, 48000 Hz, 32-bit float).

Choppy sound
------------

- Make sure [realtime scheduling](http://jackaudio.org/faq/linux_rt_config.html) is working.

- Set JACK buffer size to a larger number.

- Change the value of `ringbuffer_fragments` (in the source code) to a larger number.
