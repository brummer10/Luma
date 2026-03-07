
/*
 * GTK2inX11UiBackend.cpp
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 *
 * X11 + GtkPlug (XEmbed) backend for Luma LV2 host
 *
 * Implements IUiBackend for embedding GtkUI plugins in a native X11 window.
 */

#include "IUiBackend.hpp"
#include "IHostUiBridge.hpp"

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <functional>

class GTK2inX11UiBackend : public IUiBackend {
public:
    GTK2inX11UiBackend()
        : display_(nullptr), window_(0), plug_(nullptr), bridge_(nullptr)
    {}

    ~GTK2inX11UiBackend() override {
        close_window();
    }

    void attach_bridge(IHostUiBridge* bridge) override {
        bridge_ = bridge;
    }

    const char* lv2_ui_uri() const override {
        return LV2_UI__GtkUI;
    }

    void set_preset_name(const std::string pname) override {}

    bool create_window(int w, int h) override {
        if (!gtk_init_check(nullptr, nullptr)) {
            fprintf(stderr, "GTK init failed\n");
            return false;
        }
        GdkDisplay* gdk_display = gdk_display_get_default();
        display_ = gdk_x11_display_get_xdisplay(gdk_display);
        if (!display_) {
            fprintf(stderr, "Cannot open X display\n");
            return false;
        }
        width_ = w;
        height_ = h;

        int screen = DefaultScreen(display_);
        window_ = XCreateSimpleWindow(
            display_,
            RootWindow(display_, screen),
            0, 0, w, h, 1,
            BlackPixel(display_, screen),
            WhitePixel(display_, screen)
        );

        // Select events for XEmbed
        XSelectInput(display_, window_,
            StructureNotifyMask |
            ExposureMask |
            FocusChangeMask |
            ButtonPressMask |
            ButtonReleaseMask |
            PointerMotionMask);

        // Set _XEMBED_INFO property (version 0, flags 0)
        Atom xembed_info = XInternAtom(display_, "_XEMBED_INFO", False);
        long data[2] = { 0, 0 };
        XChangeProperty(display_, window_, xembed_info, xembed_info, 32,
                        PropModeReplace, (unsigned char*)data, 2);
        xembed_ = XInternAtom(display_, "_XEMBED", False);
        xembed_info_ = XInternAtom(display_, "_XEMBED_INFO", False);

        long info[2] = {0, 0};
        XChangeProperty(display_, window_,
                        xembed_info_,
                        xembed_info_,
                        32,
                        PropModeReplace,
                        (unsigned char*)info,
                        2);
        XMapWindow(display_, window_);

        wm_delete_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
        wm_protocols = XInternAtom(display_, "WM_PROTOCOLS", False);
        XSetWMProtocols(display_, window_, &wm_delete_, 1);
        XFlush(display_);

        gw = gdk_x11_window_foreign_new_for_display(gdk_display, window_);
        plug_ = gtk_widget_new(GTK_TYPE_WINDOW, NULL);
        gtk_widget_set_size_request(plug_, width_, height_);
        g_signal_connect(plug_, "realize", G_CALLBACK(my_gtk_realize), gw);
        gtk_widget_set_has_window(plug_, TRUE);
        gtk_widget_realize(plug_);
        container_ = gtk_vbox_new(FALSE, 0);
        gtk_container_add(GTK_CONTAINER(plug_), container_);
        send_xembed(XEMBED_EMBEDDED_NOTIFY);
        send_xembed(XEMBED_WINDOW_ACTIVATE);
        send_xembed(XEMBED_FOCUS_IN);
        XFlush(display_);
        gtk_widget_show_all(plug_);
        return true;
    }

    void finalize_window(const char* title) override {
        if (!display_ || !window_)
            return;

        XStoreName(display_, window_, title);

        XChangeProperty(display_, window_, XInternAtom(display_, "_NET_WM_NAME", False),
                        XInternAtom(display_, "UTF8_STRING", False), 8,
                        PropModeReplace, (unsigned char*)title, std::strlen(title));

        XFlush(display_);
    }

    static void my_gtk_realize(GtkWidget* widget, gpointer user) {
        gtk_widget_set_window(widget, (GdkWindow*)user);
    }

    void embed_native(void* child) override {
        GtkWidget* plugin_widget = GTK_WIDGET(child);
        gtk_container_add(GTK_CONTAINER(container_), plugin_widget);
        gtk_widget_show_all(plug_);
    }

    void resize(int w, int h) override {
        width_ = w;
        height_ = h;
        if (window_)
            XResizeWindow(display_, window_, w, h);
        if (plug_) {
            gtk_widget_set_size_request(plug_, width_, height_);
            gtk_widget_set_size_request(container_, width_, height_);

            gtk_widget_queue_resize(plug_);
            gtk_widget_queue_draw(plug_);
        }
        XFlush(display_);
    }

    void poll_events() override {
        XEvent ev;
        XNextEvent(display_, &ev);

        switch (ev.type) {
        case FocusIn:
            send_xembed(XEMBED_FOCUS_IN);
            break;

        case FocusOut:
            send_xembed(XEMBED_FOCUS_OUT); // XEMBED_FOCUS_OUT
            break;

        case ButtonRelease:
            send_xembed(XEMBED_FOCUS_OUT);
            break;
        case ClientMessage:
            if ((Atom)ev.xclient.message_type == wm_protocols &&
                    (Atom)ev.xclient.data.l[0] == wm_delete_) {
                if (window_ == ev.xclient.window)
                    if (close_cb_) close_cb_();
            }
            break;

        case ConfigureNotify:
            if (window_ != ev.xany.window) break;

            if (width_ != ev.xconfigure.width ||
                height_ != ev.xconfigure.height)
            {
                width_  = ev.xconfigure.width;
                height_ = ev.xconfigure.height;

                gtk_widget_set_size_request(plug_, width_, height_);
                gtk_widget_set_size_request(container_, width_, height_);

                gtk_widget_queue_resize(plug_);
                gtk_widget_queue_draw(plug_);
            }
            break;

        default:
            break;
        }
        while (gtk_events_pending())
            gtk_main_iteration_do(FALSE);
    }

    void set_close_callback(std::function<void()> cb) override {
        close_cb_ = std::move(cb);
    }

    void close_window() override {
        if (plug_) {
            gtk_widget_destroy(plug_);
            plug_ = nullptr;
        }
        if (window_) {
            XDestroyWindow(display_, window_);
            window_ = 0;
        }
       // if (display_) {
       //     XCloseDisplay(display_);
       //     display_ = nullptr;
       // }
    }

    void* native_window() override {
        return (void*)(uintptr_t)window_;
    }

private:
    static constexpr long XEMBED_EMBEDDED_NOTIFY   = 0;
    static constexpr long XEMBED_WINDOW_ACTIVATE   = 1;
    static constexpr long XEMBED_FOCUS_IN          = 4;
    static constexpr long XEMBED_FOCUS_OUT = 5;
    Atom wm_delete_ = None;
    void send_xembed(long message)
    {
        XEvent ev{};
        ev.xclient.type = ClientMessage;
        ev.xclient.window = GDK_WINDOW_XID((plug_->window));
        ev.xclient.message_type = xembed_;
        ev.xclient.format = 32;

        ev.xclient.data.l[0] = CurrentTime;
        ev.xclient.data.l[1] = message;
        ev.xclient.data.l[2] = 0;
        ev.xclient.data.l[3] = 0;
        ev.xclient.data.l[4] = 0;

        XSendEvent(display_,
                   GDK_WINDOW_XID((plug_->window)),
                   False,
                   NoEventMask,
                   &ev);
    }
    Atom xembed_;
    Atom xembed_info_;
    Atom wm_protocols = None;
    Display* display_ = nullptr;
    Window window_ = 0;
    GdkWindow* gw = nullptr;
    GtkWidget* plug_ = nullptr;
    GtkWidget* container_ = nullptr;
    IHostUiBridge* bridge_ = nullptr;
    std::function<void()> close_cb_;
    int idle_counter = 0;
    bool resize_enabled = false;
    int width_ = 0;
    int height_ = 0;
};
