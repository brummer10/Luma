
/*
 * main.cpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2024 brummer <brummer@web.de>
 */

/****************************************************************
        main.cpp - lixputty interface for Luma LV2Host
                   
****************************************************************/

#include "MultiHost.hpp"
#include "JackEngine.hpp"
#ifndef NOGUI
#include "XputtyUiBackend.cpp"
#include "StubGuiBackend.cpp"
#if defined(HAVE_GTK2)
#include "GTK2UiBackend.cpp"
#endif
#endif

#include "xwidgets.h"
#include "TextEntry.h"
#include "Systray.h"
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <thread>

#include "Draw.cc"

MultiHost mh;
LV2Host* cur_host = nullptr;
// Initial Plugin/Preset Scan
LV2Host temp;

Widget_t* win = nullptr;
Widget_t* plugin_view = nullptr;
Widget_t* search_entry = nullptr;
Widget_t* preset_menu = nullptr;

std::vector<InfoPair> all_plugins;
std::vector<InfoPair> filtered_plugins;
std::vector<InfoPair> current_presets;

evfunc lrelease;

char **list = nullptr;
int list_size = 0;

// convert plugin list to char** list for display
void fill_plugin_list() {
    listview_remove_list(plugin_view);
    for (int i = 0; i < list_size; ++i) {
        delete[] list[i];
    }
    delete[] list;
    list = nullptr;

    size_t n = filtered_plugins.size();
    list_size = (int)n;
    list = new char*[n + 1];
    for (size_t i = 0; i < n; ++i) {
        list[i] = new char[filtered_plugins[i].label.size() + 1];
        std::strcpy(list[i], filtered_plugins[i].label.c_str());
    }
    list[n] = nullptr;
    listview_set_list(plugin_view, list, list_size);
    expose_widget(plugin_view);
}

// filter plugins by regex
static void filter_plugins(const std::string& search) {
    filtered_plugins.clear();
    std::string needle = search;
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    for (auto& p : all_plugins) {
        std::string label = p.label;
        std::transform(label.begin(), label.end(), label.begin(), ::tolower);
        if (needle.empty() || label.find(needle) != std::string::npos) {
            filtered_plugins.push_back(p);
        }
    }
    fill_plugin_list();
    widget_show_all(plugin_view);
}

// text input box callback
static void search_callback(void* w_, void*) {
    Widget_t *w = (Widget_t*)w_;
    const char* text = w->input_label;
    std::string txt = text ? text : "";
    if (!txt.empty()) txt.pop_back();
    filter_plugins(txt);
}

// load a plugin
static bool loadPlugin() {
    int index = (int)adj_get_value(plugin_view->adj);
    if (index < 0 || index >= (int)filtered_plugins.size()) {
        fprintf(stderr, "Failed to init plugin %i\n", index);
        return false;
    }
    cur_host = nullptr;
    cur_host = mh.create_instance();
    cur_host->set_engine(std::make_unique<JackEngine>());
    cur_host->set_resource(win->app);
    #ifndef NOGUI
    cur_host->register_ui_backend(std::make_shared<XputtyUiBackend>());
    cur_host->register_ui_backend(std::make_shared<StubGuiBackend>());
    #if defined(HAVE_GTK2)
    cur_host->register_ui_backend(std::make_shared<Gtk2UiBackend>());
    #endif
    #endif
    std::string uri = filtered_plugins[index].uri;
    if (!cur_host->init(uri.c_str())) {
        fprintf(stderr, "Failed to init plugin\n");
        return false;
    }
    cur_host->initUi();
    cur_host->setRun();
    return true;
}

// load plugin button callback
static void load_plugin(void* w_, void* ) {
    Widget_t *w = (Widget_t*)w_;
    if (w->flags & HAS_POINTER && !adj_get_value(w->adj)){
        if (!loadPlugin()) {
            fprintf(stderr, "Failed to load plugin\n");
        }
    }
}

// pop up a menu to select the Preset to load
static void check_presets(void *w_, void* button_, void* user_data) {
    Widget_t *w = (Widget_t*)w_;
    lrelease(w_, button_, user_data);
    XButtonEvent *xbutton = (XButtonEvent*)button_;
    if(xbutton->button == Button1) {
        int index = (int)adj_get_value(plugin_view->adj);
        if (index < 0 || index >= (int)filtered_plugins.size()) return;
        std::string uri = filtered_plugins[index].uri;
        current_presets = temp.get_presets(uri.c_str());
        if (current_presets.empty()) return;
        Widget_t *view_port = preset_menu->childlist->childs[0];
        int i = view_port->childlist->elem;
        for(;i>-1;i--) {
            menu_remove_item(preset_menu,view_port->childlist->childs[i]);
        }

        for (auto& p : current_presets) {
            menu_add_item(preset_menu, p.label.c_str());
        }
        contex_menu_show(w, preset_menu, 12, xbutton);
    }
    
}

// load plugin and apply preset
static void load_preset(void* , void* item_, void* ) {
    if (!loadPlugin()) {
        fprintf(stderr, "fail to load plugin\n");
        return;
    }
    int index = *(int*)item_;
    if (index < 0 || index >= (int)current_presets.size()) return;
    cur_host->applyPreset(current_presets[index].uri, current_presets[index].label);
}

// quit button callback
static void quit_app(void* w_, void*) {
    Widget_t *w = (Widget_t*)w_;
    if (w->flags & HAS_POINTER && !adj_get_value(w->adj)){
        mh.shutdown_all();
        quit(win);
    }
}

// forward key press from window to text input box
void key_press(void *, void *key_, void *user_data) {
    search_entry->func.key_press_callback(search_entry, key_, user_data);
}

int main(int , char** )
{
    if (0 == XInitThreads())
        std::cerr << "Warning: XInitThreads() failed\n";
    
    Xputty app;
    main_init(&app);
    TextEntry tx;

    win = create_window(&app, DefaultRootWindow(app.dpy), 100, 100, 390, 500);
    widget_set_icon_from_png(win, LDVAR(LV2Host_png));
    set_widget_color(win, (Color_state)0, (Color_mod)1, 0.145f, 0.153f, 0.169f, 1.0f);
    set_widget_color(win, (Color_state)2, (Color_mod)1, 0.117f, 0.121f, 0.133f, 1.0f);
    win->func.expose_callback = draw_window;
    win->func.key_press_callback = key_press;
    widget_set_title(win, "Luma LV2 Host");

    search_entry = tx.addTextEntry(win, "", 20, 20, 350, 30);
    search_entry->func.value_changed_callback = search_callback;

    plugin_view = add_listview(win,"", 20, 70, 350, 350);
    set_widget_color(plugin_view->childlist->childs[0],
        (Color_state)0, (Color_mod)2, 0.102f, 0.106f, 0.118f, 1.0f);
    set_widget_color(plugin_view->childlist->childs[0],
        (Color_state)0, (Color_mod)3, 0.902f, 0.902f, 0.902f, 1.0f);
    set_widget_color(plugin_view->childlist->childs[0],
        (Color_state)0, (Color_mod)5, 0.188f, 0.196f, 0.220f, 1.0f);
    listview_set_scale_factor(plugin_view, 0.24);
    lrelease = plugin_view->func.button_release_callback;
    plugin_view->func.button_release_callback = check_presets;
    //plugin_view->func.value_changed_callback = check_presets;
    
    preset_menu = create_menu(plugin_view,25);
    preset_menu->func.button_release_callback = load_preset;

    Widget_t* load_btn = add_button(win,"Load Plugin", 20, 440, 120, 40);
    load_btn->func.expose_callback = draw_button;
    load_btn->func.value_changed_callback = load_plugin;

    Widget_t* quit_btn = add_button(win, "Quit",250, 440, 120, 40);
    quit_btn->func.expose_callback = draw_button;
    quit_btn->func.value_changed_callback = quit_app;

    all_plugins = temp.find_plugin_matches("");
    filtered_plugins = all_plugins;
    fill_plugin_list();

    create_systray_widget(win, 0, 0, 240, 240);

    widget_show_all(win);
    main_run(&app);
    for (int i = 0; i < list_size; ++i) {
        delete[] list[i];
    }
    delete[] list;
    mh.shutdown_all();
    main_quit(&app);

    return 0;
}
