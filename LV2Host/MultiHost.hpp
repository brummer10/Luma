
/*
 * MultiHost.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once

#include "LV2Host.hpp"

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

/****************************************************************
    MultiHost.hpp - run multiple LV2 plugin UI's in one UI thred
                    keep a vector with pointers to each instance
                    clean up all instances on exit
****************************************************************/

class MultiHost {
public:
    LV2Host* create_instance() {
        auto host = std::make_unique<LV2Host>();
        auto ptr = host.get();
        hosts.push_back(std::move(host));
        bool expected = false;
        if (ui_is_running.compare_exchange_strong(expected, true)) {
            run.store(true);
            ui_thread = std::thread(&MultiHost::run_ui, this);
        }
        return ptr;
    }

    void run_ui() {
        while (run.load()) {
            for (auto it = hosts.begin(); it != hosts.end();) {
                auto& h = *it;
                h->runUi();
                if (h->isDown()) it = hosts.erase(it);
                else ++it;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    void shutdown_all() {
        for (auto& h : hosts)
            h->request_shutdown();
        while(!hosts.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(32));
            #if defined(NOGUI)
            for (auto& h : hosts) {
                if (h && h->is_nogui() && !h->getRun()) h->closeHost();
            }
            #endif
        };
    }

    ~MultiHost() {
        shutdown_all();
        if (ui_is_running.exchange(false)) {
            run.store(false);
            if (ui_thread.joinable() && std::this_thread::get_id() != ui_thread.get_id()) {
                ui_thread.join();
            }
        }
        hosts.clear();
    }

private:
    std::vector<std::unique_ptr<LV2Host>> hosts;
    std::thread ui_thread;
    std::atomic<bool> ui_is_running{false};
    std::atomic<bool> run{false};
};
