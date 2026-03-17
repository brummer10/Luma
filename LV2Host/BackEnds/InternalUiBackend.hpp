
/*
 * InternalUiBackend.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include "IUiBackend.hpp"
#include "IHostUiBridge.hpp"

#include "xwidgets.h"
#include "SaveBox.h"
#include "FileButton.h"
#include <cstring>
#include <functional>

/****************************************************************
    InternalUiBackend.hpp - this is the Xputty UI backend for Luma LV2 host

****************************************************************/

class InternalUiBackend : public IUiBackend {
public:
    InternalUiBackend();
    ~InternalUiBackend() override;
    SaveBox sb;

    void attach_bridge(IHostUiBridge* b) override;
    const char* lv2_ui_uri() const override;
    bool create_ui(int w, int h) override;
    void close_window() override;
    void embed_native(void* child) override;
    void resize(int w, int h) override;
    void finalize_window(const char* title) override;
    void set_close_callback(std::function<void()> cb) override;
    void poll_events() override;
    void set_preset_name(const std::string pname) override;
    void* native_window() override {return (void*)container_->widget; }

    void patch_set(const std::string property, float v) override;
    void patch_set(const std::string property, int v) override;
    void patch_set(const std::string property, bool v) override;
    void patch_set(const std::string property, const char* path) override;
    void control_set(uint32_t port, float value) override;

private:
    void calcSize(int &w, int &h);
    Widget_t *get_widget_from_urid(const std::string urid);
    char* utf8crop(char* dst, const char* src, size_t sizeDest );
    void utf8crop_middle(char* dst, const char* src, size_t maxLen);
    void createController();
    void setCtrlValues();
    void refreshPresets();
    std::function<void()> close_cb_;
    Atom wm_delete_ = None;
    Atom wm_protocols = None;
    Xputty* app = nullptr;
    Widget_t* window_ = nullptr;
    Widget_t* container_ = nullptr;
    Widget_t* header_ = nullptr;
    Widget_t* presets_ = nullptr;
    Widget_t* save_presets_ = nullptr;
    std::vector<Widget_t*> contr;
    std::vector<Widget_t*> patch_contr;
    std::string presetName = "";
    std::vector<InfoPair> preset_list_;
    Window plugin_window = 0;
    IHostUiBridge* bridge = nullptr;
    int idle_counter = 0;
    bool resize_enabled = false;
    int width_ = 0;
    int height_ = 0;
};


