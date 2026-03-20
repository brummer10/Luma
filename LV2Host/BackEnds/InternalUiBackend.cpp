
/*
 * InternalUiBackend.cpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#include "InternalUiBackend.hpp"

/****************************************************************
    InternalUiBackend.cpp - this is the Xputty UI backend for Luma LV2 host
                          with implemented support for state save/restore

****************************************************************/

InternalUiBackend::InternalUiBackend() {}

InternalUiBackend::~InternalUiBackend() {
    close_window();
}

void InternalUiBackend::attach_bridge(IHostUiBridge* b) {
    bridge = b;
}

const char* InternalUiBackend::lv2_ui_uri() const {
    return LV2_UI__INTERNAL;
}

void InternalUiBackend::refreshPresets() {
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

inline int portWidth(const Port& p) {
    if (p.dt == tp_enum)
        return 200;
    return 100;
}

bool isUsableGroup(const PortGroup& g) {
    if (!g.ports.size())
        return false;
    return true;
}

void calcGroupSize(const std::vector<Port>& ports, const PortGroup& g,
                                        int maxWidth, int& w, int& h) {
    int x = 20;
    int x1 = 20;
    int y = 20;
    for (auto pi : g.ports) {
        const Port& p = ports[pi];
        if ((p.is_control || p.is_patch) && p.is_input) {
            int pw = portWidth(p);
            if ((x + pw) > maxWidth) {
                y += 90;
                x1 = std::max<int>(x1, x);
                x = 20;
            }
            x += pw;
        }
    }
    x1 = std::max<int>(x1, x);
    w = std::max<int>(200, x1 + 20);
    h = std::max<int>(100, y + 90);
}

void InternalUiBackend::calcSize(int &w, int &h) {
    const auto& ports  = bridge->get_ports();
    const auto& groups = bridge->get_groups();

    int y = 20;
    int max_w = 360;
    //group_sizes.push_back(0);
    for (const auto& g : groups) {
        if (!isUsableGroup(g)) continue;
        int gw = 0;
        int gh = 0;
        calcGroupSize(ports, g, w, gw, gh);
        group_sizes.push_back(gh);
        y += gh ;
        max_w = std::max<int>(max_w, gw);
    }
    w = max_w + 20;
    h = std::max<int>(140, y + 20);
}

void InternalUiBackend::createController() {
    int x = 20;
    int y = 20;
    int i = 0;
    int gh = 0;
    const std::vector<Port>& ports = bridge->get_ports();
    const std::vector<PortGroup>& groups = bridge->get_groups();
    for (auto& g : groups) {
        if (!isUsableGroup(g)) continue;
        Widget_t* frame = add_frame(container_, g.name.c_str(), 0, gh, container_->width, group_sizes[i]);
        gh += group_sizes[i];
        i += 1;
        x = 20;
        y = 20;
        for (auto pi : g.ports) {
            const Port& p = ports[pi];
            if (p.df == dtp_no_gui) continue;
            if (p.is_control && p.is_input) {
                if (p.dt == tp_scale || p.dt == tp_int) {
                    if (x + 100 > frame->width) {
                        y += 90;
                        x = 20;
                    }
                    float step = 1.0;
                    if (p.dt == tp_scale) step = pow(10.0, round(log10((p.fmax - p.fmin) / 300.0)));
                    Widget_t* ctr = add_knob(frame, p.name, x, y, 100, 80);
                    utf8crop_middle(ctr->input_label, p.name, 15);
                    ctr->label = ctr->input_label;
                    contr.push_back(ctr);
                    ctr->data = p.index;
                    ctr->parent_struct = this;
                    set_adjustment(ctr->adj, p.defvalue, p.control, p.fmin, p.fmax, step, CL_CONTINUOS);
                    ctr->func.value_changed_callback = [] (void *w_, void* ) {
                        Widget_t *w = (Widget_t*)w_;
                        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
                        self->bridge->set_control(w->data, adj_get_value(w->adj));
                    };
                    x += 100;
                } else if (p.dt == tp_toggle) {
                    if (x + 100 > frame->width) {
                        y += 90;
                        x = 20;
                    }
                    Widget_t* ctr = add_toggle_button(frame, p.name, x+5, y+20, 90, 40);
                    utf8crop_middle(ctr->input_label, p.name, 15);
                    ctr->label = ctr->input_label;
                    contr.push_back(ctr);
                    ctr->data = p.index;
                    ctr->parent_struct = this;
                    adj_set_value(ctr->adj, p.defvalue);
                    ctr->func.value_changed_callback = [] (void *w_, void* ) {
                        Widget_t *w = (Widget_t*)w_;
                        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
                        self->bridge->set_control(w->data, adj_get_value(w->adj));
                    };
                    x += 100;
                } else if (p.dt == tp_trigger) {
                    if (x + 100 > frame->width) {
                        y += 90;
                        x = 20;
                    }
                    Widget_t* ctr = add_button(frame, p.name, x+5, y+20, 90, 40);
                    utf8crop_middle(ctr->input_label, p.name, 15);
                    ctr->label = ctr->input_label;
                    contr.push_back(ctr);
                    ctr->data = p.index;
                    ctr->parent_struct = this;
                    adj_set_value(ctr->adj, p.defvalue);
                    ctr->func.value_changed_callback = [] (void *w_, void* ) {
                        Widget_t *w = (Widget_t*)w_;
                        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
                        self->bridge->set_control(w->data, adj_get_value(w->adj));
                    };
                    x += 100;
                } else if (p.dt == tp_enum) {
                    if (x + 220 > frame->width) {
                        y += 90;
                        x = 20;
                    }
                    add_label(frame, p.name, x, y, 200, 20);
                    Widget_t* ctr = add_combobox(frame, p.name, x, y+20, 200, 40);
                    contr.push_back(ctr);
                    ctr->data = p.index;
                    ctr->parent_struct = this;
                    int i = 0;
                    int set = 0;
                    for (auto& e : p.enumdict) {
                        i++;
                        combobox_add_entry(ctr, e.label.c_str());
                        if (i == p.defvalue) {
                            set = i;
                        }
                    }
                    combobox_set_active_entry(ctr, set);    
                    adj_set_value(ctr->adj, p.defvalue);
                    ctr->func.value_changed_callback = [] (void *w_, void* ) {
                        Widget_t *w = (Widget_t*)w_;
                        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
                        const std::vector<EnumPair>& e = self->bridge->get_enum_pair(w->data);
                        self->bridge->set_control(w->data, e[(int)adj_get_value(w->adj)].val);
                    };
                    x += 200;
                }
            } else if (p.is_patch && p.is_input) {
                if (p.dt == tp_scale || p.dt == tp_int) {
                    if (x + 100 > frame->width) {
                        y += 90;
                        x = 20;
                    }
                    float step = 1.0;
                    if (p.dt == tp_scale) step = pow(10.0, round(log10((p.fmax - p.fmin) / 300.0)));
                    Widget_t* ctr = add_knob(frame, p.name, x, y, 100, 80);
                    utf8crop_middle(ctr->input_label, p.name, 15);
                    ctr->label = ctr->input_label;
                    patch_contr.push_back(ctr);
                    ctr->data = p.dt;
                    ctr->parent_struct = this;
                    ctr->user_data = (void*) &p.uri;
                    set_adjustment(ctr->adj, p.defvalue, p.defvalue, p.fmin, p.fmax, step, CL_CONTINUOS);
                    ctr->func.value_changed_callback = [] (void *w_, void* ) {
                        Widget_t *w = (Widget_t*)w_;
                        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
                        const std::string& uri = *(std::string*)w->user_data;
                        float value = adj_get_value(w->adj);
                        if (w->data == tp_int) {
                            self->bridge->patch_set(uri, (int)value);
                        } else {
                            self->bridge->patch_set(uri, value);
                        }
                    };
                    x += 100;
                } else if (p.dt == tp_toggle) {
                    if (x + 100 > frame->width) {
                        y += 90;
                        x = 20;
                    }
                    Widget_t* ctr = add_toggle_button(frame, p.name, x+5, y+20, 90, 40);
                    utf8crop_middle(ctr->input_label, p.name, 15);
                    ctr->label = ctr->input_label;
                    patch_contr.push_back(ctr);
                    ctr->data = -1;
                    ctr->parent_struct = this;
                    ctr->user_data = (void*) &p.uri;
                    adj_set_value(ctr->adj, p.defvalue);
                    ctr->func.value_changed_callback = [] (void *w_, void* ) {
                        Widget_t *w = (Widget_t*)w_;
                        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
                        const std::string& uri = *(std::string*)w->user_data;
                        float value = adj_get_value(w->adj);
                        self->bridge->patch_set(uri, (bool)value);
                    };
                    x += 100;
                } else if (p.dt == tp_atom) {
                    if (x + 100 > frame->width) {
                        y += 90;
                        x = 20;
                    }
                    add_label(frame, p.name, x, y, 100, 20);
                    Widget_t* ctr = add_lv2_file_button(frame, p.name, x+10, y+20, 80, 40);
                    utf8crop_middle(ctr->input_label, p.name, 15);
                    ctr->label = ctr->input_label;
                    ctr->data = -1;
                    ctr->parent_struct = this;
                    ctr->user_data = (void*) &p.uri;
                    Widget_t* ctrl = add_label(frame, "", x, y+60, 100, 20);
                    patch_contr.push_back(ctrl);
                    ctrl->data = -1;
                    ctrl->parent_struct = this;
                    ctrl->user_data = (void*) &p.uri;
                    ctr->func.user_callback = [] (void *w_, void* user_data) {
                        Widget_t *w = (Widget_t*)w_;
                        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
                        const std::string& uri = *(std::string*)w->user_data;
                        if(user_data !=NULL) {
                            const char* path = *(const char**)user_data;
                            self->bridge->patch_set(uri, path);
                            //self->bridge->patch_get("");
                        }
                    };
                    x += 100;
                }
            } /* else if (p.is_control && !p.is_input) {
                if (p.dt == tp_scale || p.dt == tp_int) {
                    if (x + 100 > frame->width) {
                        y += 90;
                        x = 20;
                    }
                    float step = 1.0;
                    if (p.dt == tp_scale) step = pow(10.0, round(log10((p.fmax - p.fmin) / 300.0)));
                    add_label(frame, p.name, x, y, 100, 20);
                    Widget_t* ctr = add_vmeter(frame, p.name, false, x+40, y+20, 20, 70);
                    contr.push_back(ctr);
                    ctr->data = p.index;
                    ctr->parent_struct = this;
                    set_adjustment(ctr->adj, p.defvalue, p.control, p.fmin, p.fmax, step, CL_METER);
                    x += 100;
                }
            }*/
        }
    }
}

char* InternalUiBackend::utf8crop(char* dst, const char* src, size_t sizeDest ) {
    if( sizeDest ){
        size_t sizeSrc = strlen(src);
        while( sizeSrc >= sizeDest ){
            const char* lastByte = src + sizeSrc;
            while( lastByte-- > src )
                if((*lastByte & 0xC0) != 0x80)
                    break;
            sizeSrc = lastByte - src;
        }
        memcpy(dst, src, sizeSrc);
        dst[sizeSrc] = '\0';
    }
    return dst;
}

void InternalUiBackend::utf8crop_middle(char* dst, const char* src, size_t maxLen) {
    size_t len = strlen(src);
    if (len < maxLen) {
        strcpy(dst, src);
        return;
    }

    if (maxLen < 5) {
        utf8crop(dst, src, maxLen);
        return;
    }

    size_t left = (maxLen - 3) / 2;
    size_t right = maxLen - 3 - left;
    char tmp[256];
    utf8crop(tmp, src, left + 1);
    strcpy(dst, tmp);
    strcat(dst, "...");
    const char* tail = src + len - right;
    utf8crop(tmp, tail, right + 1);
    strcat(dst, tmp);
}

static void dummyCallback(void*, void* ) {}

void InternalUiBackend::setCtrlValues() {
    for (auto w : contr) {
        int index = w->data;
        xevfunc store = w->func.value_changed_callback;
        w->func.value_changed_callback = dummyCallback;
        float value = bridge->get_control(index);
        const std::vector<EnumPair>& e = bridge->get_enum_pair(w->data);
        if (e.size()) {
            auto it = std::find_if(e.begin(), e.end(), [value](const EnumPair& p) {
                return std::fabs(p.val - value) < 0.0001f;
            });
            int index = (it != e.end()) ? std::distance(e.begin(), it) : -1;
            if (index > -1) value = (float) index;
        }
        adj_set_value(w->adj, value);
        w->func.value_changed_callback = store;
    }
}

void InternalUiBackend::control_set(uint32_t port, float value) {
    for (auto w : contr) {
        if (port == (uint32_t) w->data) {
            xevfunc store = w->func.value_changed_callback;
            w->func.value_changed_callback = dummyCallback;
            //XLockDisplay(app->dpy);
            adj_set_value(w->adj, value);
            //expose_widget(w);
            //XFlush(app->dpy);
           // XUnlockDisplay(app->dpy);
            w->func.value_changed_callback = store;
        }
    }
}

Widget_t *InternalUiBackend::get_widget_from_urid(const std::string urid) {
    for(auto wid : patch_contr) {
        if (*(const std::string*)wid->user_data == urid) {
            return wid;
        }
    }
    return nullptr;
}

void InternalUiBackend::patch_set(const std::string property, float v) {
    Widget_t *wid = get_widget_from_urid(property);
    if (wid) {
        XLockDisplay(app->dpy);
        adj_set_value(wid->adj, v);
        XFlush(app->dpy);
        XUnlockDisplay(app->dpy);
    }
}
void InternalUiBackend::patch_set(const std::string property, int v) {
    Widget_t *wid = get_widget_from_urid(property);
    if (wid) {
        XLockDisplay(app->dpy);
        adj_set_value(wid->adj, (float)v);
        XFlush(app->dpy);
        XUnlockDisplay(app->dpy);
    }  
}
void InternalUiBackend::patch_set(const std::string property, bool v) {
    Widget_t *wid = get_widget_from_urid(property);
    if (wid) {
        XLockDisplay(app->dpy);
        adj_set_value(wid->adj, (float)v);
        XFlush(app->dpy);
        XUnlockDisplay(app->dpy);
    }
}
void InternalUiBackend::patch_set(const std::string property, const char* path) {
    Widget_t *wid = get_widget_from_urid(property);
    if (wid) {
        const char* filename = strrchr(path, '/');
        filename = filename ? filename + 1 : path;
        utf8crop_middle(wid->input_label, filename, 15);
        XLockDisplay(app->dpy);
        wid->label = wid->input_label;
        expose_widget(wid);
        XFlush(app->dpy);
        XUnlockDisplay(app->dpy);
    }
    //fprintf(stderr, "%s %s\n", property.c_str(), path);
}

bool InternalUiBackend::create_ui(int w, int h) {
    width_ = w;
    height_ = h;

    app = (Xputty*)bridge->get_resource();
    if (!app) {
        Xputty main;
        app = &main;
        main_init(app);
    }
    calcSize(w,h);
    window_ = create_window(app, DefaultRootWindow(app->dpy), 100, 100, w, h);
    window_->parent_struct = this;
    window_->flags |= HIDE_ON_DELETE;

    window_->func.unmap_notify_callback = [] (void *w_, void* ) {
        Widget_t *w = (Widget_t*)w_;
        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
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
        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
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
        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
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
        InternalUiBackend* self = static_cast<InternalUiBackend*>(w->parent_struct);
        if (!self || !self->bridge) return;
        int index = (int)adj_get_value(w->adj)-1;
        if (index == -1) {
            self->bridge->restoreDefaults();
            std::string name = self->bridge->getPluginName();
            os_set_title(self->window_, name.c_str());
            self->setCtrlValues();
            return;
        }
        if (index < 0 || index >= (int)self->preset_list_.size()) return;
        auto& p = self->preset_list_[index];
        self->bridge->applyPreset(p.uri, p.label);
    };

    container_ = create_widget(app, window_, 0, 35, w, h-35);
    container_->parent_struct = this;
    container_->scale.gravity = NORTHWEST;
    container_->func.expose_callback =  [] (void *w_, void* ) {
        Widget_t *w = (Widget_t*)w_;
        cairo_set_source_rgba(w->crb, 0.125f, 0.133f, 0.149f, 1.0f);
        cairo_paint (w->crb);
    };

    widget_set_dnd_aware(window_);
    createController();
    std::string name = bridge->getPluginName();
    os_set_title(window_, name.c_str());
    widget_show_all(window_);
    XFlush(app->dpy);
    bridge->patch_get("");
    return true;
}

void InternalUiBackend::set_preset_name(const std::string pname) {
    presetName = pname;
    XLockDisplay(app->dpy);
    std::string name = bridge->getPluginName() + " - " + presetName;
    os_set_title(window_, name.c_str());
    if (presets_) {
        xevfunc store = presets_->func.value_changed_callback;
        presets_->func.value_changed_callback = dummyCallback;
        int i = 0;
        int set = 0;
        for (auto& p : preset_list_) {
            i++;
            if (presetName.compare(p.label) == 0) {
                set = i;
            }
        }
        combobox_set_active_entry(presets_, set);
        setCtrlValues();
        presets_->func.value_changed_callback = store;
    }
    XFlush(app->dpy);
    XUnlockDisplay(app->dpy);
}

void InternalUiBackend::close_window() {
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

void InternalUiBackend::embed_native(void*) {
    widget_show_all(window_);
    XFlush(app->dpy);
}

void InternalUiBackend::resize(int w, int h) {
    if (!app->dpy || !window_) return;
    width_ = w;
    height_ = h+35;
    XResizeWindow(app->dpy, window_->widget, width_, height_);
    XFlush(app->dpy);
}

void InternalUiBackend::finalize_window(const char* title) {
    if (!app->dpy || !window_) return;
    os_set_title(window_, title);
    XFlush(app->dpy);
}

void InternalUiBackend::poll_events() {
    // handled by libxputty
    return;
}

void InternalUiBackend::set_close_callback(std::function<void()> cb) {
    close_cb_ = std::move(cb);
}


