
/*
 * X11UiBackend.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include "IUiBackend.hpp"
#include "IHostUiBridge.hpp"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cstring>
#include <functional>

/****************************************************************
    X11UiBackend.hpp - this is the X11 UI backend for Luma LV2 host

****************************************************************/

class X11UiBackend : public IUiBackend {
public:
    X11UiBackend();
    ~X11UiBackend() override;

    void attach_bridge(IHostUiBridge* b) override;

    // LV2 UI type we support
    const char* lv2_ui_uri() const override;

    // window lifecycle
    bool create_ui(int w, int h) override;
    void close_window() override;
    void embed_native(void* child) override;
    void resize(int w, int h) override;
    void finalize_window(const char* title) override;
    void set_close_callback(std::function<void()> cb) override;
    void set_preset_name(const std::string pname) override;

    // event loop
    void poll_events() override;

    // native handles
    void* native_window() override {return (void*)window_; }

private:
    static void set_xdnd_proxy(Display* dpy, Window plugin_window);
    std::function<void()> close_cb_;
    Atom wm_delete_ = None;
    Atom wm_protocols = None;
    Display* display_ = nullptr;
    Window window_ = 0;
    Window plugin_window = 0;
    IHostUiBridge* bridge = nullptr;
    int idle_counter = 0;
    bool resize_enabled = false;
    int width_ = 0;
    int height_ = 0;
};

