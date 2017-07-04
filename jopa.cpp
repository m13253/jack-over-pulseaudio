/*
    JACK-over-PulseAudio (jopa)
    Copyright (C) 2013-2017 StarBrilliant <m13253@hotmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <spawn.h>
#include <jack/control.h>
#include <jack/jack.h>

class JopaSession {

private:

    jack_client_t* jack_client = nullptr;

    static void jack_on_shutdown(void* arg);
    static void jack_on_process(jack_nframes_t nframes, void* arg);
    static void jack_on_buffer_size(jack_nframes_t nframes, void* arg);
    static void jack_on_sample_rate(jack_nframes_t nframes, void* arg);
    static void jack_on_port_connect(jack_port_id_t a, jack_port_id_t b, int connect, void* arg);

    pa_mainloop* pulse_mainloop = nullptr;
    pa_context* pulse_context = nullptr;

    static void pulse_on_context_state(pa_context* c, void* userdata);

public:

    void init();
    void run();
    ~JopaSession();

};

extern char** environ;

int main() {
    JopaSession* session = new JopaSession;
    session->init();
    int retval = session->run();
    delete session;
    return retval;
}

void JopaSession::init() {
    // Try to use the default JACK server
    jack_client = jack_client_open("JACK over PulseAudio", JackNoStartServer, nullptr);
    if(jack_client == nullptr) {
        // Create a new JACK server
        pid_t jack_server_pid;
        static char const* const jack_server_argv[] = {"jackd", "-T", "-d", "dummy", NULL};
        if(posix_spawnp(&jack_server_pid, "jackd", nullptr, nullptr, const_cast<char* const*>(jack_server_argv), environ) != 0) {
            throw std::runtime_error("Unable to start a JACK server");
        }
        // Try to use the newly started JACK server
        for(int i = 0; i < 5; ++i) {
            jack_client = jack_client_open("JACK over PulseAudio", JackNoStartServer, nullptr);
            if(jack_client != nullptr) {
                break;
            } else {
                sleep(1);
            }
        }
    }
    if(jack_client == nullptr) {
        throw std::runtime_error("Unable to connect to the JACK server");
    }

    // Create PulseAudio mainloop
    pulse_mainloop = pa_mainloop_new();
    if(pulse_mainloop == nullptr) {
        throw std::runtime_error("Unable to create a PulseAudio event loop");
    }
    // Create PulseAudio context
    pulse_context = pa_context_new();
    if(pulse_context == nullptr) {
        throw std::runtime_error("Unable to create a PulseAudio context");
    }
    // Connect to PulseAudio server
    //TODO: set callback
    if(pa_context_connect(pulse_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        throw std::runtime_error("Unable to connect to the PulseAudio server");
    }
}

JopaSession::~JopaSession() {
    if(pulse_context != nullptr) {
        pa_context_disconnect(pulse_context);
        pa_context_unref(pulse_context);
        pulse_context = nullptr;
    }
    if(pulse_mainloop != nullptr) {
        pa_mainloop_unref(pulse_mainloop);
        pulse_mainloop = nullptr;
    }
    if(jack_client != nullptr) {
        jack_client_close(jack_client);
        jack_client = nullptr;
    }
}

int JopaSession::run() {
    int retval;
    if(pa_mainloop_run(pulse_mainloop, &retval) < 0) {
        throw std::runtime_error("Unable to run PulseAudio event loop");
    }
    return retval;
}
