
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
    virtual bool create_ui(int w, int h) = 0;
    virtual void close_window() = 0;
    virtual void embed_native(void* child) = 0;
    virtual void resize(int w, int h) = 0;
    virtual void finalize_window(const char* title) = 0;
    virtual void poll_events() = 0;
    virtual void set_close_callback(std::function<void()> cb) = 0;
    virtual void set_preset_name(const std::string pname) = 0;

    virtual void patch_set(const std::string property, float v) = 0;
    virtual void patch_set(const std::string property, int v) = 0;
    virtual void patch_set(const std::string property, bool v) = 0;
    virtual void patch_set(const std::string property, const char* path) = 0;
    virtual void control_set(uint32_t port, float value) = 0;

    virtual void* native_window() = 0;
};

