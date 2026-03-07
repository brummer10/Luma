
/*
 * InternalEngine.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include <functional>

#include "IDspEngine.hpp"

/****************************************************************
    Port Direction
****************************************************************/
enum class PortDirection {
    Input,
    Output
};

/****************************************************************
    Internal Audio Port
                        - provide no buffer
                        - point to external buffer
****************************************************************/
class InternalAudioPort final : public IAudioPort {

    PortDirection dir;
    float* buffer = nullptr;

public:
    explicit InternalAudioPort(PortDirection d)
        : dir(d) {}

    void set_buffer(float* buf) {
        buffer = buf;
    }

    PortDirection direction() const override {
        return dir;
    }

    float* audio_buffer(uint32_t) override {
        return buffer;
    }
};

/****************************************************************
    Internal MIDI Port
****************************************************************/
class InternalMidiPort final : public IMidiPort {

    PortDirection dir;
    IMidiBuffer* buffer = nullptr;

public:
    explicit InternalMidiPort(PortDirection d)
        : dir(d) {}

    void set_buffer(IMidiBuffer* buf) {
        buffer = buf;
    }

    PortDirection direction() const override {
        return dir;
    }

    IMidiBuffer* midi_buffer(uint32_t) override {
        return buffer;
    }
};

/****************************************************************
    Internal Engine
****************************************************************/
class InternalEngine final : public IDspEngine {

    ProcessCallback callback = nullptr;
    void* userdata = nullptr;

    uint32_t samplerate = 48000;
    uint32_t buffersize = 512;

    std::vector<InternalAudioPort*> audio_ports;
    std::vector<InternalMidiPort*>  midi_ports;

public:

    ~InternalEngine() override = default;

    bool open(const std::string&) override { return true; }
    bool activate() override { return true; }
    void deactivate() override {}
    void close() override {}

    uint32_t sample_rate() const override { return samplerate; }
    uint32_t buffer_size() const override { return buffersize; }

    void set_process_callback(ProcessCallback cb, void* userdata_) override {
        callback = cb;
        userdata = userdata_;
    }

    std::unique_ptr<IDspPort> create_audio_port(const std::string&, bool input) override {
        auto port = std::make_unique<InternalAudioPort>(
            input ? PortDirection::Input : PortDirection::Output);
        audio_ports.push_back(port.get());
        return port;
    }

    std::unique_ptr<IDspPort> create_midi_port(const std::string&, bool input) override {
        auto port = std::make_unique<InternalMidiPort>(
            input ? PortDirection::Input
                  : PortDirection::Output);

        midi_ports.push_back(port.get());
        return port;
    }

    /****************************************************************
        External Buffer Injection
    ****************************************************************/
    void set_audio_buffer(size_t index, float* buffer) {
        if (index < audio_ports.size())
            audio_ports[index]->set_buffer(buffer);
    }

    void set_midi_buffer(size_t index, IMidiBuffer* buffer) {
        if (index < midi_ports.size())
            midi_ports[index]->set_buffer(buffer);
    }

    /****************************************************************
        Manual Processing Call
    ****************************************************************/
    void process(uint32_t nframes) {
        if (callback)
            callback(nframes, userdata);
    }

    void set_sample_rate(uint32_t sr) { samplerate = sr; }
    void set_buffer_size(uint32_t bs) { buffersize = bs; }
};
