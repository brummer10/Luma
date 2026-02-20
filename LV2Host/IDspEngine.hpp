
/*
 * IDspEngine.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <memory>
#include <functional>
#include <string>
#include <vector>

/****************************************************************
    IDspEngine.hpp - define a engine backend for Luma LV2 host

****************************************************************/


struct MidiEvent {
    uint32_t frame;
    const uint8_t* data;
    uint32_t size;
};

class IMidiBuffer {
public:
    virtual ~IMidiBuffer() = default;

    virtual void clear() = 0;
    virtual uint32_t event_count() const = 0;
    virtual bool get_event(uint32_t index, MidiEvent& ev) = 0;
    virtual bool write_event(
        uint32_t frame,
        const uint8_t* data,
        uint32_t size) = 0;
};

class IDspPort {
public:
    virtual ~IDspPort() = default;
};

class IAudioPort : public IDspPort {
public:
    virtual float* audio_buffer(uint32_t nframes) = 0;
};

class IMidiPort : public IDspPort {
public:
    virtual IMidiBuffer* midi_buffer(uint32_t nframes) = 0;
};

class IDspEngine {
public:
    using ProcessCallback = void (*)(uint32_t nframes, void* userdata);

    virtual ~IDspEngine() = default;

    virtual bool open(const std::string& name) = 0;
    virtual bool activate() = 0;
    virtual void deactivate() = 0;
    virtual void close() = 0;

    virtual uint32_t sample_rate() const = 0;
    virtual uint32_t buffer_size() const = 0;

    virtual void set_process_callback(ProcessCallback cb, void* userdata) = 0;

    virtual std::unique_ptr<IDspPort>
        create_audio_port(const std::string& name, bool input) = 0;

    virtual std::unique_ptr<IDspPort>
        create_midi_port(const std::string& name, bool input) = 0;
};
