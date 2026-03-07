
/*
 * LV2HostWorker.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <lv2/worker/worker.h>
#include <atomic>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>

/****************************************************************
    LV2HostWorker.hpp - do work in a worker thread provided by the host

****************************************************************/

struct LV2HostWorker {

    lv2_ringbuffer_t* requests = nullptr;
    lv2_ringbuffer_t* responses = nullptr;

    LV2_Worker_Schedule schedule;
    LV2_Feature feature;
    const LV2_Worker_Interface* iface = nullptr;
    LV2_Handle dsp_handle = nullptr;

    std::atomic<bool> running{false};
    std::atomic<bool> work_pending{false};
    std::atomic<int> jobs{0};

    struct WorkerRequest {
        uint32_t size;
        uint8_t  data[0];
    };

    struct WorkerResponse {
        uint32_t size;
        uint8_t  data[0];
    };

/****************************************************************
    STATIC CALLBACKS
****************************************************************/

    static LV2_Worker_Status host_schedule_work(LV2_Worker_Schedule_Handle handle,
                                                uint32_t size, const void* data) {

        auto* w = (LV2HostWorker*)handle;
        w->jobs.fetch_add(1, std::memory_order_acq_rel);
        const size_t total = sizeof(uint32_t) + size;

        if (lv2_ringbuffer_write_space(w->requests) < total)
            return LV2_WORKER_ERR_NO_SPACE;

        lv2_ringbuffer_write(w->requests, (const char*)&size, sizeof(uint32_t));
        lv2_ringbuffer_write(w->requests, (const char*)data, size);
        w->work_pending.store(true, std::memory_order_release);

        return LV2_WORKER_SUCCESS;
    }

    static LV2_Worker_Status host_respond(LV2_Worker_Respond_Handle handle,
                                          uint32_t size, const void* data) {

        auto* w = (LV2HostWorker*)handle;
        const size_t total = sizeof(uint32_t) + size;

        if (lv2_ringbuffer_write_space(w->responses) < total)
            return LV2_WORKER_ERR_NO_SPACE;

        lv2_ringbuffer_write(w->responses, (const char*)&size, sizeof(uint32_t));
        lv2_ringbuffer_write(w->responses, (const char*)data, size);

        return LV2_WORKER_SUCCESS;
    }

/****************************************************************
    MEMBER FUNCTIONS
****************************************************************/

    void deliver_worker_responses()
    {
        while (true) {
            if (lv2_ringbuffer_read_space(responses) < sizeof(uint32_t)) break;
            uint32_t size;
            lv2_ringbuffer_peek(responses, (char*)&size, sizeof(uint32_t));
            if (lv2_ringbuffer_read_space(responses) < sizeof(uint32_t) + size) break;
            lv2_ringbuffer_read(responses, (char*)&size, sizeof(uint32_t));
            std::vector<uint8_t> buf(size);
            lv2_ringbuffer_read(responses, (char*)buf.data(), size);
            iface->work_response(dsp_handle, size, buf.data());
        }
    }

    void wait_until_idle() {

        if (!iface || !responses) return;
        while (true) {
            if (jobs.load(std::memory_order_acquire) <= 0 &&
                lv2_ringbuffer_read_space(requests) <= 0)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void clear_responses() {

        if (!iface || !responses) return;
        while (lv2_ringbuffer_read_space(responses) > 0) {
            char dummy[256];
            lv2_ringbuffer_read(responses, dummy, std::min(sizeof(dummy),
                                    lv2_ringbuffer_read_space(responses)));
        }
    }

    void stop_worker() {

        if (!running.exchange(false)) return;
        if (requests) {
            lv2_ringbuffer_free(requests);
            requests = nullptr;
        }

        if (responses) {
            lv2_ringbuffer_free(responses);
            responses = nullptr;
        }

        iface = nullptr;
        dsp_handle = nullptr;
    }
};
