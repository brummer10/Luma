
/*
 * IUiBackend.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once
#include <memory>
#include <string>
#include <functional>

/****************************************************************
        IUiBackend.hpp - define a UI backend for Luma LV2 host

****************************************************************/

class IHostUiBridge; 

class IUiBackend {
public:
    virtual ~IUiBackend() = default;

    virtual void attach_bridge(IHostUiBridge* bridge) = 0;

    virtual const char* lv2_ui_uri() const = 0;
    virtual bool create_window(int w, int h) = 0;
    virtual void close_window() = 0;
    virtual void embed_native(void* child) = 0;
    virtual void resize(int w, int h) = 0;
    virtual void finalize_window(const char* title) = 0;
    virtual void poll_events() = 0;
    virtual void set_close_callback(std::function<void()> cb) = 0;

    virtual void* native_window() = 0;
};

