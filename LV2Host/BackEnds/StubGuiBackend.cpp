#include "StubGuiBackend.hpp"

#include <X11/Xutil.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

StubGuiBackend::StubGuiBackend() {}

StubGuiBackend::~StubGuiBackend() {
    if (display) {
        XDestroyWindow(display, window);
        XCloseDisplay(display);
    }
}

void StubGuiBackend::attach_bridge(IHostUiBridge* b) {
    bridge = b;
}

bool StubGuiBackend::create_ui(int w, int h) {
    width = w;
    height = h;

    display = XOpenDisplay(nullptr);
    if (!display) return false;

    int screen = DefaultScreen(display);

    window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        100, 100,
        width, height,
        1,
        BlackPixel(display, screen),
        WhitePixel(display, screen));

    XSelectInput(display, window,
        StructureNotifyMask | ExposureMask | ButtonPressMask | ButtonMotionMask);

    gc = DefaultGC(display, screen);

    XMapWindow(display, window);

    wm_delete_ = XInternAtom(display, "WM_DELETE_WINDOW", False);
    wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    XSetWMProtocols(display, window, &wm_delete_, 1);

    rebuild_layout();

    return true;
}

void StubGuiBackend::set_preset_name(const std::string pname) {
    XLockDisplay(display);
    std::string name = bridge->getPluginName();
    name += " - " + pname;
    finalize_window(name.c_str());
    XFlush(display);
    XUnlockDisplay(display);
}

void StubGuiBackend::close_window() {
    if (display) {
        if (window)
            XDestroyWindow(display, window);
        XCloseDisplay(display);
    }
    window = 0;
    display = nullptr;
}

void StubGuiBackend::finalize_window(const char* title) {
    if (display && window)
        XStoreName(display, window, title ? title : "Stub GUI");

    XChangeProperty(display, window, XInternAtom(display, "_NET_WM_NAME", False),
                    XInternAtom(display, "UTF8_STRING", False), 8,
                    PropModeReplace, (unsigned char*)title, std::strlen(title));
}

void StubGuiBackend::rebuild_layout() {
    sliders.clear();
    meters.clear();

    int x = 20;

    uint32_t controls = bridge->control_port_count();
    for (uint32_t i = 0; i < controls; ++i) {
        sliders.push_back({i, x});
        x += 40;
    }

    x += 20;

    uint32_t meter_count = bridge->meter_count();
    for (uint32_t i = 0; i < meter_count; ++i) {
        meters.push_back({i, x});
        x += 30;
    }
}

void StubGuiBackend::poll_events() {
    while (XPending(display)) {
        XEvent e;
        XNextEvent(display, &e);

        if (e.type == Expose) {
            draw();
        } else if (e.type == ButtonPress || e.type == MotionNotify) {
            int mx = e.xbutton.x;
            int my = e.xbutton.y;

            for (const auto& s : sliders) {
                int sx = s.x;
                int sy = 40;
                int h = 150;

                if (mx >= sx && mx <= sx + 20 &&
                    my >= sy && my <= sy + h) {

                    float norm = 1.0f -
                        float(my - sy) / float(h);

                    norm = std::clamp(norm, 0.0f, 1.0f);

                    float min = bridge->get_control_min(s.control_index);
                    float max = bridge->get_control_max(s.control_index);

                    if (std::isfinite(min) &&
                        std::isfinite(max) &&
                        max > min) {

                        float v = min + norm * (max - min);
                        bridge->set_control(s.control_index, v);
                    }
                }
            }
            draw();

        } else if (e.type == ClientMessage) {
            if ((Atom)e.xclient.message_type == wm_protocols &&
                    (Atom)e.xclient.data.l[0] == wm_delete_) {
                if (close_cb)
                    close_cb();
            }
        }
    }

    draw(); // refresh meters
}

void StubGuiBackend::draw_slider(const Slider& s) {
    float v = bridge->get_control(s.control_index);
    float min = bridge->get_control_min(s.control_index);
    float max = bridge->get_control_max(s.control_index);

    if (!std::isfinite(min) ||
        !std::isfinite(max) ||
        max <= min)
        return;

    float norm = (v - min) / (max - min);
    norm = std::clamp(norm, 0.0f, 1.0f);

    int sx = s.x;
    int sy = 40;
    int h = 150;
    int fill = int(h * norm);

    XDrawRectangle(display, window, gc,
                   sx, sy, 20, h);

    XFillRectangle(display, window, gc,
                   sx + 1, sy + h - fill,
                   18, fill);

    const char* name =
        bridge->port_name(
            bridge->get_control_port_index(s.control_index));

    if (name)
        XDrawString(display, window, gc,
                    sx - 5, sy + h + 15,
                    name, strlen(name));
}

void StubGuiBackend::draw_meter(const Meter& m) {
    float norm = bridge->get_meter(m.meter_index);
    norm = std::clamp(norm, 0.0f, 1.0f);

    int sx = m.x;
    int sy = 40;
    int h = 150;
    int fill = int(h * norm);

    XDrawRectangle(display, window, gc,
                   sx, sy, 15, h);

    XFillRectangle(display, window, gc,
                   sx + 1, sy + h - fill,
                   13, fill);

    const char* name = bridge->meter_name(m.meter_index);

    if (name)
        XDrawString(display, window, gc,
                    sx - 5, sy + h + 15,
                    name, strlen(name));
}

void StubGuiBackend::draw() {
    XClearWindow(display, window);

    for (const auto& s : sliders)
        draw_slider(s);

    for (const auto& m : meters)
        draw_meter(m);

    XFlush(display);
}
