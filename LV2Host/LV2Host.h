
/*
 * LV2Host.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include "lv2_ringbuffer.h"

#include <lilv/lilv.h>

#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/patch/patch.h>
#include <lv2/worker/worker.h>
#include <lv2/state/state.h>
#include <lv2/resize-port/resize-port.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/data-access/data-access.h>

#include <vector>
#include <string>
#include <atomic>
#include <unordered_map>
#include <thread>
#include <memory>

#include "URIDs.h"
#include "LV2HostTypes.hpp"
#include "LV2HostPorts.hpp"
#include "LV2HostState.hpp"
#include "LV2HostWorker.hpp"
#include "LV2HostContext.hpp"

#include "IDspEngine.hpp"
#include "IHostUiBridge.hpp"
#include "IUiBackend.hpp"

/****************************************************************
        LV2Host - class to load and run LV2 plugins

****************************************************************/

class LV2Host : public IHostUiBridge, public LV2HostState {
public:

    explicit LV2Host();
    ~LV2Host();

    bool init(const char* uri);
    bool initUi();

    void closeHost();

    void set_engine(std::unique_ptr<IDspEngine> e);
    void register_ui_backend(std::shared_ptr<IUiBackend> b);

    bool is_nogui();

    void startUi();
    void stopUi();

    void request_shutdown();

    bool isDown();
    bool getRun();
    void setRun();

    std::vector<InfoPair> find_plugin_matches(const std::string& input);
    std::vector<InfoPair> get_presets(const char* pluginUri);

    /* IHostUiBridge implementation */

    void list_controls() const override;

    uint32_t meter_count() const override;
    uint32_t get_meter_port_index(uint32_t index) const override;
    const char* meter_name(uint32_t index) const override;
    float get_meter(uint32_t index) const override;

    uint32_t control_port_count() const override;
    uint32_t get_control_port_index(uint32_t index) const override;

    void set_control(uint32_t index, float value) override;
    float get_control(uint32_t index) const override;

    float get_control_min(uint32_t index) const override;
    float get_control_max(uint32_t index) const override;

    void send_atom_to_plugin(uint32_t port, uint32_t size,
                             uint32_t type,const void* data) override;

    const char* port_name(uint32_t port) const override;
    bool port_is_input(uint32_t port) const override;
    bool port_is_control(uint32_t port) const override;

    void set_resource(void* res) override;
    void* get_resource() const override;

    const std::vector<InfoPair> get_presets() override;

    void applyPreset(const std::string& uri, const std::string& label) override;
    bool savePresetBundle(const std::string& preset_name) override;
    void restoreDefaults() override;

    const std::string& getPluginName() const override;


/* ===============================
   UI LOOP
================================ */

    void run_ui_loop();
    void runUi();

private:

/* ===============================
   URIDs
================================ */

    void init_urids();

    static LV2_URID map_uri(LV2_URID_Map_Handle h, const char* uri);
    static const char* unmap_uri(LV2_URID_Unmap_Handle h, LV2_URID urid);

/* ===============================
   FEATURES
================================ */

    void init_features();

    static char* make_path_func(LV2_State_Make_Path_Handle, const char* path);
    static char* map_path_func(LV2_State_Map_Path_Handle, const char* abstract_path);
    static void free_path_func(LV2_State_Free_Path_Handle, char* path);

/* ===============================
   LILV
================================ */

    bool init_lilv();
    void freeNodes();

    bool feature_is_supported(const char* uri, const LV2_Feature*const* f);
    bool check_resize_port_requirements(const LilvPlugin* plugin);
    bool checkFeatures(const LilvPlugin* plugin, const LV2_Feature*const* feat);

/* ===============================
   ENGINE
================================ */

    bool init_engine();
    static void process_wrapper(uint32_t nframes, void* userdata);

/* ===============================
   PORTS
================================ */

    bool init_ports();

/* ===============================
   INSTANCE
================================ */

    bool init_instance();

/* ===============================
   PROCESS
================================ */

    int process(uint32_t nframes);

/* ===============================
   UI
================================ */

    static void ui_write(LV2UI_Controller c, uint32_t port,
                         uint32_t size, uint32_t type, const void* buf);

    static uint32_t ui_port_map(LV2UI_Feature_Handle handle, const char* uri);
    static int ui_resize(LV2UI_Feature_Handle handle, int w, int h);

    void load_defaults();
    void send_initial_ui_values();
    void send_control_values();
    void send_control_outputs();

    void destroy_ui();

    static unsigned int host_ui_supported(const char* ui_type,
                                          const char* host_type);

    bool is_bridge_ui(const LilvUI* ui);

    void select_backend_for_plugin();

    static const void* data_access_cb(const char* uri);

/* ===============================
   DATA
================================ */

    struct {
        LV2_Feature um_f;
        LV2_Feature unm_f;

        LV2_Feature map_path_feature;
        LV2_Feature make_path_feature;
        LV2_Feature free_path_feature;
        LV2_Feature bbl_feature;

        LV2_Extension_Data_Feature data_access;
        LV2_Feature data_access_feature;
    } features;

    const LV2_Feature* feat_[7];


    LV2HostPorts hports;
    LV2HostWorker host_worker;
    URIDs urids;

    std::shared_ptr<LV2HostContext> ctx;
    std::unique_ptr<IDspEngine> engine;
    std::shared_ptr<IUiBackend> backend;
    std::vector<std::shared_ptr<IUiBackend>> available_backends;
    std::vector<Port> ports;

    std::unordered_map<std::string, LV2_URID> urid_map;
    std::unordered_map<LV2_URID, std::string> urid_unmap;

    std::thread ui_thread;

    std::atomic<bool> close_ui{false};
    std::atomic<bool> ui_is_running{false};
    std::atomic<bool> shutdown_requestet{false};
    std::atomic<bool> shutdown{false};
    std::atomic<bool> run{false};
    std::atomic<bool> lilv_is_inited{false};
    std::atomic<bool> is_down{false};

    std::atomic<bool> ui_dirty{false};
    std::atomic<bool> ui_needs_initial_update{false};
    std::atomic<bool> ui_needs_control_update{false};

    LilvWorld* world = nullptr;
    const LilvPlugins* plugs = nullptr;
    const LilvPlugin* plugin = nullptr;

    LilvNode* audio_class = nullptr;
    LilvNode* control_class = nullptr;
    LilvNode* atom_class = nullptr;
    LilvNode* input_class = nullptr;
    LilvNode* x11_class = nullptr;
    LilvNode* rsz_minimumSize = nullptr;

    LilvInstance* instance = nullptr;
    const LV2UI_Idle_Interface* idle = nullptr;

    LV2_URID_Map um;
    LV2_URID_Unmap unm;

    LV2_State_Map_Path map_path;
    LV2_State_Make_Path make_path;
    LV2_State_Free_Path free_path;

    uint32_t required_atom_size = 4096;
    uint32_t max_block_length = 0;

    std::string plugin_uri;
    std::string preset_uri;
    std::string preset_label;
    std::string plugin_name;

    LV2UI_Resize resize;
    void* ui_dl = nullptr;
    const LV2UI_Descriptor* ui_desc = nullptr;
    LV2UI_Handle ui_handle = nullptr;
    LV2UI_Widget ui_widget = nullptr;

    void* hres = nullptr;

    bool init_ui();
};
