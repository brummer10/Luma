
/*
 * X11UiBackend.cpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#include "X11UiBackend.hpp"

/****************************************************************
    X11UiBackend.cpp - this is the X11 UI backend for Luma LV2 host

****************************************************************/

X11UiBackend::X11UiBackend() {}

X11UiBackend::~X11UiBackend() {
    if (display_) {
        if (window_)
            XDestroyWindow(display_, window_);
        XCloseDisplay(display_);
    }
}

void X11UiBackend::attach_bridge(IHostUiBridge* b) {
    bridge = b;
}

const char* X11UiBackend::lv2_ui_uri() const {
    return LV2_UI__X11UI;
}

bool X11UiBackend::create_window(int w, int h) {
    width_ = w;
    height_ = h;

    if (!display_)
        display_ = XOpenDisplay(nullptr);

    if (!display_)
        return false;

    window_ = XCreateSimpleWindow(display_, DefaultRootWindow(display_),
                                                100, 100, w, h, 0, 0, 0);

    long event_mask = StructureNotifyMask | ExposureMask;

    XSelectInput(display_, window_, event_mask);
    XMapWindow(display_, window_);

    wm_delete_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
    wm_protocols = XInternAtom(display_, "WM_PROTOCOLS", False);
    XSetWMProtocols(display_, window_, &wm_delete_, 1);

    // Enable drag & drop
    Atom dnd_version = 5;
    Atom XdndAware = XInternAtom(display_, "XdndAware", False);
    XChangeProperty(display_, window_, XdndAware, XA_ATOM, 32,
                PropModeReplace, (unsigned char*)&dnd_version, 1);

    XFlush(display_);

    return true;
}

void X11UiBackend::close_window() {
    if (display_) {
        if (window_)
            XDestroyWindow(display_, window_);
        XCloseDisplay(display_);
    }
    window_ = 0;
    display_ = nullptr;
}

void X11UiBackend::embed_native(void* child) {
    if (!display_ || !window_ || !child)
        return;

    plugin_window = (Window)child;
    set_xdnd_proxy(display_, plugin_window);
    // propagate size hints if available
    XSizeHints hints;
    long supplied;

    if (XGetWMNormalHints(display_, plugin_window, &hints, &supplied)) {
        XSetWMNormalHints(display_, window_, &hints);
    }

    XFlush(display_);
}

void X11UiBackend::resize(int w, int h) {
    if (!display_ || !window_)
        return;

    width_ = w;
    height_ = h;

    XLockDisplay(display_);
    XResizeWindow(display_, window_, w, h);
    XFlush(display_);
    XUnlockDisplay(display_);
}

void X11UiBackend::finalize_window(const char* title) {
    if (!display_ || !window_)
        return;

    XStoreName(display_, window_, title);

    XChangeProperty(display_, window_, XInternAtom(display_, "_NET_WM_NAME", False),
                    XInternAtom(display_, "UTF8_STRING", False), 8,
                    PropModeReplace, (unsigned char*)title, std::strlen(title));

    XFlush(display_);
}

void X11UiBackend::poll_events() {
    if (!display_) return;

    if (!resize_enabled) {
        idle_counter++;
        if (idle_counter > 30) resize_enabled = true;
    }

    while (XPending(display_) > 0) {
        XEvent ev;
        XNextEvent(display_, &ev);

        switch (ev.type) {

        case ClientMessage:
            if ((Atom)ev.xclient.message_type == wm_protocols &&
                    (Atom)ev.xclient.data.l[0] == wm_delete_) {
                if (close_cb_)
                    close_cb_();
            }
            break;

        case ConfigureNotify:
            if (width_  != ev.xconfigure.width || height_ != ev.xconfigure.height) {
                width_  = ev.xconfigure.width;
                height_ = ev.xconfigure.height;
                if (resize_enabled)
                    XResizeWindow(display_, (Window)plugin_window, width_, height_);
            }
            break;

        default:
            break;
        }
    }
}

void X11UiBackend::set_close_callback(std::function<void()> cb) {
    close_cb_ = std::move(cb);
}

void X11UiBackend::set_xdnd_proxy(Display* dpy, Window plugin_window) {
    if (!dpy || !plugin_window)
        return;

    Atom xdnd_proxy = XInternAtom(dpy, "XdndProxy", False);
    if (xdnd_proxy == None)
        return;

    Window root, parent;
    Window* children = nullptr;
    unsigned int nchildren = 0;
    Window w = plugin_window;

    while (w != None) {
        XChangeProperty(dpy, w, xdnd_proxy, XA_WINDOW, 32,
            PropModeReplace, (unsigned char*)&plugin_window, 1);
        if (!XQueryTree(dpy, w, &root, &parent, &children, &nchildren))
            break;

        if (children) XFree(children);
        if (parent == root || parent == None) break;
        w = parent;
    }

    XFlush(dpy);
}
