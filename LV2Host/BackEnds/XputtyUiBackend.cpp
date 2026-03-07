
/*
 * XputtyUiBackend.cpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#include "XputtyUiBackend.hpp"

/****************************************************************
    XputtyUiBackend.cpp - this is the Xputty UI backend for Luma LV2 host
                          with implemented support for state save/restore

****************************************************************/

XputtyUiBackend::XputtyUiBackend() {}

XputtyUiBackend::~XputtyUiBackend() {
    close_window();
}

void XputtyUiBackend::attach_bridge(IHostUiBridge* b) {
    bridge = b;
}

const char* XputtyUiBackend::lv2_ui_uri() const {
    return LV2_UI__X11UI;
}

void XputtyUiBackend::refreshPresets() {
    preset_list_ = bridge->get_presets();
    combobox_delete_entrys(presets_);
    combobox_add_entry(presets_, "Default");
    int i = 0;
    int set = 0;
    for (auto& p : preset_list_) {
        i++;
        combobox_add_entry(presets_, p.label.c_str());
        if (presetName.compare(p.label) == 0) {
            set = i;
        }
    }
    combobox_set_active_entry(presets_, set);    
}

bool XputtyUiBackend::create_ui(int w, int h) {
    width_ = w;
    height_ = h;

    app = (Xputty*)bridge->get_resource();
    if (!app) {
        Xputty main;
        app = &main;
        main_init(app);
    }

    window_ = create_window(app, DefaultRootWindow(app->dpy), 100, 100, w, h);
    window_->parent_struct = this;
    window_->flags |= HIDE_ON_DELETE;
    window_->func.configure_notify_callback = [] (void *w_, void* ) {
        Widget_t *w = (Widget_t*)w_;
        XputtyUiBackend* self = static_cast<XputtyUiBackend*>(w->parent_struct);
        if (!self->container_) return;
        Metrics_t metrics;
        os_get_window_metrics(w, &metrics);
        if (self->width_  != metrics.width || self->height_ != metrics.height) {
            self->width_ = metrics.width;
            self->height_ = metrics.height;
            XResizeWindow(self->app->dpy, (Window)self->plugin_window, metrics.width, metrics.height-35);
        }
    };

    window_->func.unmap_notify_callback = [] (void *w_, void* ) {
        Widget_t *w = (Widget_t*)w_;
        XputtyUiBackend* self = static_cast<XputtyUiBackend*>(w->parent_struct);
        if (!self->window_) return;
        if (self->close_cb_) self->close_cb_();
    };

    header_ = create_widget(app, window_, 0, 0, w, 35);
    header_->parent_struct = this;
    header_->scale.gravity = NORTHEAST;
    header_->func.expose_callback =  [] (void *w_, void* ) {
        Widget_t *w = (Widget_t*)w_;
        cairo_set_source_rgba(w->crb, 0.145f, 0.153f, 0.169f, 1.0f);
        cairo_paint (w->crb);
    };

    save_presets_ = add_button(header_, "Save Preset",w-105, 4, 90, 30);
    set_widget_color(save_presets_, (Color_state)0, (Color_mod)2, 0.145f, 0.153f, 0.169f, 1.0f);
    save_presets_->scale.gravity = WESTNORTH;
    save_presets_->parent_struct = this;
    save_presets_->func.value_changed_callback = [] (void* w_, void*) {
        Widget_t *w = (Widget_t*)w_;
        XputtyUiBackend* self = static_cast<XputtyUiBackend*>(w->parent_struct);
        if (w->flags & HAS_POINTER && !adj_get_value(w->adj)){
            Widget_t *dia = self->sb.showSaveBox(w, "Save Preset", "Save Preset as:");
            int x1, y1;
            os_translate_coords( w, w->widget, 
                os_get_root_window(w->app, IS_WIDGET), 0, 0, &x1, &y1);
            os_move_window(w->app->dpy,dia,x1-200, y1+40);
        }
    };

    save_presets_->func.dialog_callback = [] (void *w_, void* user_data ) {
        Widget_t *w = (Widget_t*)w_;
        XputtyUiBackend* self = static_cast<XputtyUiBackend*>(w->parent_struct);
        if(user_data !=NULL && strlen(*(const char**)user_data)) {
            self->presetName = (*(const char**)user_data);
            if (self->bridge->savePresetBundle(self->presetName)) {
                std::string name = self->bridge->getPluginName();
                name += " - " + self->presetName;
                os_set_title(self->window_, name.c_str());
                self->refreshPresets();
            }
        }
    };

    presets_ = add_combobox(header_, "", 20,2,200,28);
    set_widget_color(presets_->childlist->childs[1]->childlist->childs[0], 
        (Color_state)0, (Color_mod)2, 0.145f, 0.153f, 0.169f, 1.0f);
    presets_->scale.gravity = EASTNORTH;
    presets_->parent_struct = this;
    refreshPresets();
    presets_->func.value_changed_callback = [] (void* w_, void*) {
        Widget_t* w = (Widget_t*)w_;
        XputtyUiBackend* self = static_cast<XputtyUiBackend*>(w->parent_struct);
        if (!self || !self->bridge) return;
        int index = (int)adj_get_value(w->adj)-1;
        if (index == -1) {
            self->bridge->restoreDefaults();
            std::string name = self->bridge->getPluginName();
            os_set_title(self->window_, name.c_str());
            return;
        }
        if (index < 0 || index >= (int)self->preset_list_.size()) return;
        auto& p = self->preset_list_[index];
        self->bridge->applyPreset(p.uri, p.label);
    };

    container_ = create_widget(app, window_, 0, 35, w, h-35);
    container_->parent_struct = this;
    container_->scale.gravity = NORTHWEST;

    widget_set_dnd_aware(window_);

    XFlush(app->dpy);

    return true;
}

static void dummy_callback(void*, void* ) {}

void XputtyUiBackend::set_preset_name(const std::string pname) {
    presetName = pname;
    XLockDisplay(app->dpy);
    std::string name = bridge->getPluginName() + " - " + presetName;
    os_set_title(window_, name.c_str());
    if (presets_) {
        xevfunc store = presets_->func.value_changed_callback;
        presets_->func.value_changed_callback = dummy_callback;
        int i = 0;
        int set = 0;
        for (auto& p : preset_list_) {
            i++;
            if (presetName.compare(p.label) == 0) {
                set = i;
            }
        }
        combobox_set_active_entry(presets_, set);
        presets_->func.value_changed_callback = store;
    }
    XFlush(app->dpy);
    XUnlockDisplay(app->dpy);
}

void XputtyUiBackend::close_window() {
    if(window_ && app) { 
        destroy_widget(window_, app);
        XFlush(app->dpy);
        window_ = nullptr;
    }

    if (app && !bridge->get_resource()) {
        main_quit(app);
        app = nullptr;
    }
}

void XputtyUiBackend::embed_native(void* child) {
    if (!window_ || !child) return;

    plugin_window = (Window)child;
    //XReparentWindow(app->dpy, plugin_window, container_->widget, 0, 0);
    set_xdnd_proxy(app->dpy, plugin_window);
    // propagate size hints if available
    XSizeHints hints;
    long supplied;

    if (XGetWMNormalHints(app->dpy, plugin_window, &hints, &supplied)) {
        hints.min_height = hints.min_height + 35;
        hints.max_height = hints.max_height + 35;
        hints.min_width = hints.min_width > 330 ? hints.min_width : 330;
        hints.max_width = hints.max_width > 330 ? hints.max_width : 330;
        XSetWMNormalHints(app->dpy, window_->widget, &hints);
    }
    widget_show_all(window_);
    XFlush(app->dpy);
}

void XputtyUiBackend::resize(int w, int h) {
    if (!app->dpy || !window_) return;
    width_ = w;
    height_ = h+35;
    XResizeWindow(app->dpy, window_->widget, width_, height_);
    XFlush(app->dpy);
}

void XputtyUiBackend::finalize_window(const char* title) {
    if (!app->dpy || !window_) return;
    os_set_title(window_, title);
    XFlush(app->dpy);
}

void XputtyUiBackend::poll_events() {
    // handled by libxputty
    return;
}

void XputtyUiBackend::set_close_callback(std::function<void()> cb) {
    close_cb_ = std::move(cb);
}

void XputtyUiBackend::set_xdnd_proxy(Display* dpy, Window plugin_window) {
    if (!dpy || !plugin_window) return;

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
