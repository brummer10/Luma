
/*
 * GTK2UiBackend.cpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#include "GTK2UiBackend.hpp"

/****************************************************************
    GTK2UiBackend.cpp - this is the GTK2 UI backend for Luma LV2 host

****************************************************************/

static void log_handler(const gchar* domain, GLogLevelFlags level,
                                    const gchar* message, gpointer) {

    if ((level & G_LOG_LEVEL_CRITICAL) && strstr(message, "Source ID") &&
                                    strstr(message, "was not found")) return;

    g_log_default_handler(domain, level, message, nullptr);
}

Gtk2UiBackend::Gtk2UiBackend() {
    g_log_set_handler("GLib", G_LOG_LEVEL_CRITICAL, log_handler, nullptr);
    if (!gtk_init_check(0, nullptr)) {
        fprintf(stderr, "GTK init failed\n");
    }
}

Gtk2UiBackend::~Gtk2UiBackend() {
    if (window_) {
        gtk_widget_destroy(window_);
        window_ = nullptr;
    }
}
    
void Gtk2UiBackend::attach_bridge(IHostUiBridge* b) {
    bridge = b;
}

const char* Gtk2UiBackend::lv2_ui_uri() const {
    return LV2_UI__GtkUI;
}

bool Gtk2UiBackend::create_window(int w, int h) {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window_), w, h);
    container_ = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window_), container_);
    g_signal_connect(window_, "delete-event", G_CALLBACK(on_delete), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(on_destroy), this);
    gtk_widget_show_all(window_);
    return true;
}

void Gtk2UiBackend::close_window() {
    if (window_) {
        gtk_widget_destroy(window_);
        window_ = nullptr;
    }    
}

void Gtk2UiBackend::on_destroy(GtkWidget*, gpointer data) {
    auto* self = static_cast<Gtk2UiBackend*>(data);
    self->window_ = nullptr;
    self->container_ = nullptr;
    if (self->close_cb_) {
        self->close_cb_();
    }
}

void Gtk2UiBackend::embed_native(void* widget) {
    if (!container_ || !widget)
        return;

    GtkWidget* child = GTK_WIDGET(widget);
    gtk_container_add(GTK_CONTAINER(container_), child);
    gtk_widget_show(child);
}

void Gtk2UiBackend::resize(int w, int h) {
    if (window_) {
        gtk_window_resize(GTK_WINDOW(window_), w, h);
    }
}

void Gtk2UiBackend::finalize_window(const char* title) {
    if (window_) {
        gtk_window_set_title(GTK_WINDOW(window_), title);
    }
}

void Gtk2UiBackend::poll_events() {
    if (!window_) return;
    while (gtk_events_pending()) {
        gtk_main_iteration_do(FALSE);
    }
}

void Gtk2UiBackend::set_close_callback(std::function<void()> cb) {
    close_cb_ = std::move(cb);
}

gboolean Gtk2UiBackend::on_delete(GtkWidget*, GdkEvent*, gpointer) {
    return FALSE; 
}

