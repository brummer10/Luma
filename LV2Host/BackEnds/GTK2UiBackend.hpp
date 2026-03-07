
/*
 * GTK2UiBackend.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include "IUiBackend.hpp"

#include <gtk/gtk.h>
#include <functional>
#include "IHostUiBridge.hpp"


/****************************************************************
    GTK2UiBackend.hpp - this is the GTK2 UI backend for Luma LV2 host

****************************************************************/

class Gtk2UiBackend : public IUiBackend {
public:
    Gtk2UiBackend();
    ~Gtk2UiBackend() override;

    void attach_bridge(IHostUiBridge* b) override;
    const char* lv2_ui_uri() const override;
    bool create_ui(int w, int h) override;
    void close_window() override;
    void embed_native(void* widget) override;
    void resize(int w, int h) override;
    void finalize_window(const char* title) override;
    void poll_events() override;
    void set_preset_name(const std::string pname) override;
    void set_close_callback(std::function<void()> cb) override;
    void* native_window() override { return window_; }

private:
    static void on_preset_changed(GtkComboBox*, gpointer);
    static void on_save_clicked(GtkButton*, gpointer);
    static gboolean on_delete(GtkWidget*, GdkEvent*, gpointer);
    static void on_destroy(GtkWidget*, gpointer data);
    void refreshPresets();
    GtkWidget* window_ = nullptr;
    GtkWidget* container_ = nullptr;
    GtkWidget* child_ = nullptr;
    GtkWidget* header_box_ = nullptr;
    GtkWidget* preset_combo_ = nullptr;
    GtkWidget* save_button_ = nullptr;

    std::vector<InfoPair> preset_list_;
    std::string presetName;


    IHostUiBridge* bridge = nullptr;

    std::function<void()> close_cb_;
};

