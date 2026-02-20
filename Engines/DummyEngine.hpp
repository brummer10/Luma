
/*
 * DummyEngine.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once
#include "IDspEngine.hpp"

#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <semaphore>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <condition_variable>

/****************************************************************
        Dummy Implementation for IDspEngine
            engine for Luma LV2 host
****************************************************************/

inline void timespec_add_ns(timespec& t, int64_t ns) noexcept {
    t.tv_nsec += ns;
    while (t.tv_nsec >= 1000000000LL) {
        t.tv_nsec -= 1000000000LL;
        t.tv_sec  += 1;
    }
}

class DummyAudioPort : public IAudioPort {
public:
    explicit DummyAudioPort(uint32_t bs)
        : buffer_(bs, 0.0f) {}

    float* audio_buffer(uint32_t nframes) override {
        if (nframes > buffer_.size())
            buffer_.resize(nframes);
        std::memset(buffer_.data(), 0, nframes * sizeof(float));
        return buffer_.data();
    }

private:
    std::vector<float> buffer_;
};

struct DummyMidiEvent {
    uint32_t frame;
    std::vector<uint8_t> data;
};

class DummyMidiBuffer : public IMidiBuffer {
public:
    void clear() override { events_.clear(); }

    uint32_t event_count() const override {
        return (uint32_t)events_.size();
    }

    bool get_event(uint32_t i, MidiEvent& ev) override {
        if (i >= events_.size()) return false;

        auto& e = events_[i];
        ev.frame = e.frame;
        ev.size  = (uint32_t)e.data.size();
        ev.data  = e.data.data();
        return true;
    }

    bool write_event(uint32_t f, const uint8_t* d, uint32_t s) override {
        DummyMidiEvent e;
        e.frame = f;
        e.data.assign(d, d + s);
        events_.push_back(std::move(e));
        return true;
    }

private:
    std::vector<DummyMidiEvent> events_;
};

class DummyMidiPort : public IMidiPort {
public:
    IMidiBuffer* midi_buffer(uint32_t) override {
        buffer_.clear();
        return &buffer_;
    }
private:
    DummyMidiBuffer buffer_;
};

class DummyEngine : public IDspEngine {
public:

    enum class Mode {
        Realtime,
        FastForward,
        Manual
    };

    DummyEngine(uint32_t sr = 48000, uint32_t bs = 1024)
        : sr_(sr)
        , bs_(bs)
        , mode_(Mode::Realtime)
        , running_(false)
        , active_(false)
        , sample_clock_(0)
        , xrun_probability_(0.0)
    {}

    ~DummyEngine() override {
        close();
    }

    void set_mode(Mode m) {
        mode_ = m;
    }

    void set_xrun_probability(double p) {
        xrun_probability_ = p;
    }

    uint64_t sample_clock() const {
        return sample_clock_.load();
    }

    void step(uint32_t blocks = 1) {
        for (uint32_t i = 0; i < blocks; ++i)
            process_block();
    }

    bool open(const std::string& n) override {
        name_ = n;
        return true;
    }

    bool activate() override {
        if (active_) return true;

        running_ = true;

        if (mode_ == Mode::Realtime) {
            audio_thread_ = std::thread(&DummyEngine::audio_loop, this);
            timer_thread_ = std::thread(&DummyEngine::timer_loop, this);
        }
        else if (mode_ == Mode::FastForward) {
            audio_thread_ = std::thread(&DummyEngine::fast_loop, this);
        }
        tick_counter_ = 0;
        active_ = true;
        return true;
    }

    void deactivate() override {
        if (!active_) return;

        running_ = false;
        tick_cv_.notify_all();

        if (timer_thread_.joinable())
            timer_thread_.join();

        if (audio_thread_.joinable())
            audio_thread_.join();

        active_ = false;
    }

    void close() override {
        deactivate();
    }

    uint32_t sample_rate() const override {
        return sr_;
    }

    uint32_t buffer_size() const override {
        return bs_;
    }

    void set_process_callback(ProcessCallback cb, void* ud) override {
        process_cb_ = cb;
        userdata_ = ud;
    }

    std::unique_ptr<IDspPort> create_audio_port(const std::string&, bool) override {
        return std::make_unique<DummyAudioPort>(bs_);
    }

    std::unique_ptr<IDspPort> create_midi_port(const std::string&, bool) override {
        return std::make_unique<DummyMidiPort>();
    }

private:

    void process_block() {
        if (!process_cb_) return;

        if (xrun_probability_ > 0.0) {
            if (rand_double() < xrun_probability_)
                return;
        }

        process_cb_(bs_, userdata_);
        sample_clock_ += bs_;
    }

    void audio_loop() {
        uint64_t local_tick = 0;

        while (running_) {
            std::unique_lock<std::mutex>lock(tick_mutex_);
            tick_cv_.wait(lock, [&] {
                return !running_ || tick_counter_.load(
                    std::memory_order_acquire) > local_tick; });

            if (!running_) break;
            local_tick++;
            lock.unlock();
            process_block();
        }
    }

    void fast_loop() {
        while (running_)
            process_block();
    }

    void timer_loop() {
        const int64_t period_ns =
            (int64_t(bs_) * 1000000000LL) / sr_;

        timespec next;
        clock_gettime(CLOCK_MONOTONIC, &next);

        while (running_) {
            timespec_add_ns(next, period_ns);
            tick_counter_.fetch_add(1, std::memory_order_release);
            tick_cv_.notify_one();
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
        }
    }

    static double rand_double() {
        return double(rand()) / RAND_MAX;
    }

private:

    std::string name_;

    uint32_t sr_;
    uint32_t bs_;

    Mode mode_;

    ProcessCallback process_cb_ = nullptr;
    void* userdata_ = nullptr;

    std::atomic<bool> running_;
    std::atomic<bool> active_;

    std::atomic<uint64_t> sample_clock_;

    double xrun_probability_;

    std::atomic<uint64_t> tick_counter_{0};
    std::mutex tick_mutex_;
    std::condition_variable tick_cv_;

    std::thread audio_thread_;
    std::thread timer_thread_;
};
