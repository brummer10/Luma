
/*
 * LV2HostContext.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <chrono>

#include <lilv/lilv.h>
#include "LV2PluginRegistry.hpp"
#include "LV2HostWorker.hpp"
#include "lv2_ringbuffer.h" 

/****************************************************************
        LV2HostContext - shared context for all LV2Host instances

****************************************************************/

class LV2HostContext {
public:
    LV2PluginRegistry registry;

    static std::shared_ptr<LV2HostContext> acquire() {
        static std::weak_ptr<LV2HostContext> weak;
        static std::mutex m;

        std::lock_guard<std::mutex> lock(m);

        auto ctx = weak.lock();
        if (!ctx) {
            ctx = std::shared_ptr<LV2HostContext>(new LV2HostContext());
            weak = ctx;
        }
        return ctx;
    }

    void register_worker(LV2HostWorker* w) {
        std::lock_guard<std::mutex> lock(mutex_);
        workers.push_back(w);
    }

    void unregister_worker(LV2HostWorker* w) {
        std::lock_guard<std::mutex> lock(mutex_);
        workers.erase(std::remove(workers.begin(), workers.end(), w), workers.end());
    }

    LilvWorld* world() const { return world_; }
    const LilvPlugins* plugs() const { return plugs_; }

    ~LV2HostContext() {
        running.store(false);
        if (worker_thread.joinable())
            worker_thread.join();

        lilv_world_free(world_);
    }

private:
    LV2HostContext() {
        running.store(true);
        worker_thread = std::thread(&LV2HostContext::thread_loop, this);

        world_ = lilv_world_new();
        lilv_world_load_all(world_);
        plugs_ = lilv_world_get_all_plugins(world_);
        registry.init(world_, plugs_);
    }

    void thread_loop() {
        while (running.load(std::memory_order_acquire)) {
            std::vector<LV2HostWorker*> local_workers;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                local_workers = workers;
            }
            for (auto* w : local_workers) {
                process_worker(w);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    static void process_worker(LV2HostWorker* w) {
        if (!w) return;
        if (!w->running.load(std::memory_order_acquire)) return;
        if (!w->requests) return;
        if (!w->iface) return; 
        if (lv2_ringbuffer_read_space(w->requests) < sizeof(uint32_t)) return;
        uint32_t size;
        lv2_ringbuffer_peek(w->requests, (char*)&size, sizeof(uint32_t));
        if (lv2_ringbuffer_read_space(w->requests) < sizeof(uint32_t) + size) return;
        lv2_ringbuffer_read(w->requests, (char*)&size, sizeof(uint32_t));
        std::vector<uint8_t> buf(size);
        lv2_ringbuffer_read(w->requests, (char*)buf.data(), size);
        w->iface->work(w->dsp_handle, w->host_respond, w, size, buf.data());
        w->jobs.fetch_sub(1, std::memory_order_acq_rel);
    }

    std::vector<LV2HostWorker*> workers;
    std::mutex mutex_;
    std::thread worker_thread;
    std::atomic<bool> running{false};

    LilvWorld* world_;
    const LilvPlugins* plugs_;
};

