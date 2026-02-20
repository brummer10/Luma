#pragma once

#include "IUiBackend.hpp"

#include <gtk/gtk.h>
#include <functional>
#include "IHostUiBridge.hpp"


class Gtk2UiBackend : public IUiBackend {
public:
    Gtk2UiBackend();
    ~Gtk2UiBackend() override;

    void attach_bridge(IHostUiBridge* b) override;
    const char* lv2_ui_uri() const override;
    bool create_window(int w, int h) override;
    void embed_native(void* widget) override;
    void resize(int w, int h) override;
    void finalize_window(const char* title) override;
    void poll_events() override;
    void set_close_callback(std::function<void()> cb) override;
    void* native_window() override { return window_; }

private:
    static gboolean on_delete(GtkWidget*, GdkEvent*, gpointer);

    GtkWidget* window_ = nullptr;
    GtkWidget* container_ = nullptr;
    IHostUiBridge* bridge = nullptr;

    std::function<void()> close_cb_;
};

