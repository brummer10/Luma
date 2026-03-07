
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

void Gtk2UiBackend::refreshPresets() {
    if (!bridge || !preset_combo_) return;
    preset_list_ = bridge->get_presets();
    GtkTreeModel* model = gtk_combo_box_get_model(GTK_COMBO_BOX(preset_combo_));
    gint count = gtk_tree_model_iter_n_children(model, NULL);

    for (gint i = count - 1; i >= 0; --i) {
        gtk_combo_box_text_remove(
            GTK_COMBO_BOX_TEXT(preset_combo_), i);
    }
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(preset_combo_), "Default");

    int set = 0;
    int i = 0;
    for (auto& p : preset_list_) {
        i++;
        gtk_combo_box_text_append_text(
            GTK_COMBO_BOX_TEXT(preset_combo_), p.label.c_str());
        if (presetName == p.label)
            set = i;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(preset_combo_), set);
}

void Gtk2UiBackend::on_preset_changed(GtkComboBox* box, gpointer data) {
    auto* self = static_cast<Gtk2UiBackend*>(data);
    if (!self || !self->bridge) return;
    int index = gtk_combo_box_get_active(box) - 1;
    if (index == -1) {
        self->bridge->restoreDefaults();
        gtk_window_set_title(GTK_WINDOW(self->window_), self->bridge->getPluginName().c_str());
        return;
    }

    if (index < 0 || index >= (int)self->preset_list_.size()) return;
    auto& p = self->preset_list_[index];
    self->bridge->applyPreset(p.uri, p.label);
}

void Gtk2UiBackend::on_save_clicked(GtkButton*, gpointer data) {
    auto* self = static_cast<Gtk2UiBackend*>(data);
    if (!self || !self->bridge) return;
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Save Preset", GTK_WINDOW(self->window_),
        GTK_DIALOG_MODAL, GTK_STOCK_OK, GTK_RESPONSE_OK, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);

    GtkWidget* entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), entry, TRUE, TRUE, 5);
    gtk_widget_show(entry);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char* text = gtk_entry_get_text(GTK_ENTRY(entry));

        if (text && strlen(text)) {
            self->presetName = text;
            if (self->bridge->savePresetBundle(self->presetName)) {
                std::string name = self->bridge->getPluginName() + " - " + self->presetName;
                gtk_window_set_title( GTK_WINDOW(self->window_), name.c_str());
                self->refreshPresets();
            }
        }
    }

    gtk_widget_destroy(dialog);
}

bool Gtk2UiBackend::create_ui(int w, int h) {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_resizable(GTK_WINDOW(window_), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window_), w, h);

    GtkWidget* main_vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window_), main_vbox);

    header_box_ = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), header_box_, FALSE, FALSE, 5);

    preset_combo_ = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(header_box_), preset_combo_, FALSE, FALSE, 5);
    g_signal_connect(preset_combo_, "changed", G_CALLBACK(on_preset_changed), this);

    save_button_ = gtk_button_new_with_label("Save Preset");
    gtk_box_pack_end(GTK_BOX(header_box_), save_button_, FALSE, FALSE, 5);
    g_signal_connect(save_button_, "clicked", G_CALLBACK(on_save_clicked), this);

    container_ = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), container_, TRUE, TRUE, 0);

    refreshPresets();

    g_signal_connect(window_, "delete-event", G_CALLBACK(on_delete), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(on_destroy), this);

    return true;
}

void Gtk2UiBackend::set_preset_name(const std::string pname) {
    presetName = pname;
    std::string name = bridge->getPluginName() + " - " + pname;
    gtk_window_set_title( GTK_WINDOW(window_), name.c_str());
    if (preset_combo_) {
        g_signal_handlers_block_by_func(preset_combo_, (gpointer)on_preset_changed, this);
        int set = 0;
        int i = 0;
        for (auto& p : preset_list_) {
            i++;
            if (presetName == p.label) set = i;
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(preset_combo_), set);
        g_signal_handlers_unblock_by_func(preset_combo_, (gpointer)on_preset_changed, this);
    }
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
    if (!container_ || !widget) return;
    GtkWidget* child = GTK_WIDGET(widget);
    gtk_container_add(GTK_CONTAINER(container_), child);
    gtk_widget_show(child);
    gtk_widget_show_all(window_);
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

