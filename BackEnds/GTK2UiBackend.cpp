#include "GTK2UiBackend.hpp"

Gtk2UiBackend::Gtk2UiBackend() {
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
    
void Gtk2UiBackend::attach_bridge(IHostUiBridge* b) override {
    bridge = b;
}

const char* Gtk2UiBackend::lv2_ui_uri() const {
    return LV2_UI__GtkUI;
}

bool Gtk2UiBackend::create_window(int w, int h) {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_default_size(GTK_WINDOW(window_), w, h);

    container_ = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(window_), container_);

    g_signal_connect(
        window_,
        "delete-event",
        G_CALLBACK(on_delete),
        this
    );

    gtk_widget_show_all(window_);

    return true;
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
    while (gtk_events_pending()) {
        gtk_main_iteration_do(FALSE);
    }
}

void Gtk2UiBackend::set_close_callback(std::function<void()> cb) {
    close_cb_ = std::move(cb);
}

gboolean Gtk2UiBackend::on_delete(
    GtkWidget*,
    GdkEvent*,
    gpointer data)
{
    auto* self = static_cast<Gtk2UiBackend*>(data);

    if (self->close_cb_) {
        self->close_cb_();
    }

    return TRUE; // prevent GTK from destroying automatically
}

