
/*
 * JackEngine.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once

#include <jack/jack.h>
#include <jack/midiport.h>

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <stdexcept>

#include "IDspEngine.hpp"

/****************************************************************
        JACK Implementation for IDspEngine
            engine backend for Luma LV2 host
****************************************************************/

class JackMidiBuffer final : public IMidiBuffer {
    void* buf = nullptr;

public:
    void set_buffer(void* b) { buf = b; }

    void clear() override {
        jack_midi_clear_buffer(buf);
    }

    uint32_t event_count() const override {
        return jack_midi_get_event_count(buf);
    }

    bool get_event(uint32_t index, MidiEvent& ev) override {
        jack_midi_event_t j;
        if (jack_midi_event_get(&j, buf, index))
            return false;

        ev.frame = j.time;
        ev.data  = j.buffer;
        ev.size  = j.size;
        return true;
    }

    bool write_event(uint32_t frame, const uint8_t* data, uint32_t size) override {
        return jack_midi_event_write(buf, frame, data, size) == 0;
    }
};

class JackAudioPort final : public IAudioPort {
    jack_port_t* port = nullptr;

public:
    explicit JackAudioPort(jack_port_t* p) : port(p) {}

    float* audio_buffer(uint32_t nframes) override {
        return static_cast<float*>(jack_port_get_buffer(port, nframes));
    }
};

class JackMidiPort final : public IMidiPort {
    jack_port_t* port = nullptr;
    JackMidiBuffer midi;

public:
    explicit JackMidiPort(jack_port_t* p) : port(p) {}

    IMidiBuffer* midi_buffer(uint32_t nframes) override {
        midi.set_buffer(jack_port_get_buffer(port, nframes));
        return &midi;
    }
};

class JackEngine final : public IDspEngine {
    jack_client_t* client = nullptr;
    ProcessCallback callback = nullptr;
    void* userdata = nullptr;

    static int jack_process(jack_nframes_t n, void* arg) {
        auto* self = static_cast<JackEngine*>(arg);
        if (self->callback) self->callback(n, self->userdata);
        return 0;
    }

public:
    ~JackEngine() override {
        close();
    }

    bool open(const std::string& name) override {
        if (client) return false;
        client = jack_client_open(name.c_str(), JackNullOption, nullptr);
        if (!client) return false;
        jack_set_process_callback(client, jack_process, this);
        return true;
    }

    bool activate() override {
        return client && jack_activate(client) == 0;
    }

    void deactivate() override {
        if (client) jack_deactivate(client);
    }

    void close() override {
        if (!client) return;

        deactivate();
        jack_client_close(client);
        client = nullptr;
    }

    uint32_t sample_rate() const override {
        return client ? jack_get_sample_rate(client) : 0;
    }

    uint32_t buffer_size() const override {
        return client ? jack_get_buffer_size(client) : 0;
    }

    void set_process_callback(ProcessCallback cb, void* userdata) override {
        callback = cb;
        this->userdata = userdata;
    }

    std::unique_ptr<IDspPort> create_audio_port(
            const std::string& name, bool input) override {

        auto flags = input ? JackPortIsInput : JackPortIsOutput;
        jack_port_t* p = jack_port_register(client, name.c_str(),
                                JACK_DEFAULT_AUDIO_TYPE, flags, 0);

        if (!p)
            throw std::runtime_error("audio port register failed");

        return std::make_unique<JackAudioPort>(p);
    }

    std::unique_ptr<IDspPort> create_midi_port(
            const std::string& name, bool input) override {

        auto flags = input ? JackPortIsInput : JackPortIsOutput;
        jack_port_t* p = jack_port_register(client, name.c_str(),
                                JACK_DEFAULT_MIDI_TYPE, flags, 0);

        if (!p)
            throw std::runtime_error("midi port register failed");

        return std::make_unique<JackMidiPort>(p);
    }
};
