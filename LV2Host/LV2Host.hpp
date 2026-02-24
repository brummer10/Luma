
/*
 * LV2Host.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


/****************************************************************
        LV2Host.h - a LV2 Host

****************************************************************/

//  g++ -g main.cpp -o lv2host `pkg-config --cflags --libs jack lilv-0 x11` -ldl

#pragma once


#include "lv2_ringbuffer.h"

#include <lilv/lilv.h>

#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
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

#include <dlfcn.h>
#include <unistd.h>

#include <vector>
#include <string>
#include <atomic>
#include <unordered_map>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <cassert>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <memory>
#include <mutex>

#include "IDspEngine.hpp"
#include "IHostUiBridge.hpp"
#include "IUiBackend.hpp"
#include "NoGuiBackend.hpp"
#include "DummyEngine.hpp"

class LV2Host;

static thread_local LV2Host* current_host = nullptr;
/****************************************************************
        LV2Host - class to host LV2 plugins with X11 GUI's

****************************************************************/

class LV2HostContext {
public:
    static std::shared_ptr<LV2HostContext> acquire() {
        static std::weak_ptr<LV2HostContext> weak;
        static std::mutex m;

        std::lock_guard<std::mutex> lock(m);

        auto ctx = weak.lock();
        if (!ctx) {
            ctx = std::shared_ptr<LV2HostContext>(new LV2HostContext());
            weak = ctx;
        }

        return ctx;
    }

    LilvWorld* world() const { return world_; }
    const LilvPlugins* plugs() const { return plugs_; }

    ~LV2HostContext() {
        lilv_world_free(world_);
    }

private:
    LV2HostContext() {

        world_ = lilv_world_new();
        lilv_world_load_all(world_);
        plugs_ = lilv_world_get_all_plugins(world_);
    }

    LilvWorld* world_;
    const LilvPlugins* plugs_;
};

class LV2Host : public IHostUiBridge {
public:
    std::atomic<bool> close_ui{false};
    explicit LV2Host() 
        : ctx(LV2HostContext::acquire())
        , world(ctx->world())
        , plugs(ctx->plugs()) {
        engine = std::make_unique<DummyEngine>();
        register_ui_backend(std::make_shared<NoGuiBackend>());
    }

    ~LV2Host() {
        stopUi();
        closeHost();
    }

    void set_engine(std::unique_ptr<IDspEngine> e) {
        #ifndef DEBUG
        engine = std::move(e);
        #endif
    }

    void register_ui_backend(std::shared_ptr<IUiBackend> b) {
        if (b) available_backends.push_back(b);
    }

    bool init(const char* uri) {
        plugin_uri = uri;
        if (!world) return false;
        return init_lilv()
            && init_engine()
            && init_ports()
            && init_instance();
    }

    bool initUi() {
        select_backend_for_plugin();
        return init_ui()
            && engine->activate();
    }

    void closeHost() {
        if (instance) {
            lilv_instance_deactivate(instance);
        }
        stop_worker();
        destroy_ui();
        if (ui_dl) {
            dlclose(ui_dl);
            ui_dl = nullptr;
        }

        engine->deactivate();
        engine->close();

        if (instance) {
            lilv_instance_free(instance);
            instance = nullptr;
        }

        for (auto& p : ports) {
            if (p.atom)
                free(p.atom);
            delete p.atom_state;
        }
        ports.clear();
        if(backend) backend->close_window();
        //stopUi();
        if (world) {
            freeNodes();
            world = nullptr;
        }        
    }

/****************************************************************
                        UI LOOP

****************************************************************/

    void startUi() {
        ui_is_running.store(true);
        ui_thread = std::thread(&LV2Host::run_ui_loop, this);
        if (!backend->lv2_ui_uri()) {
            while (!shutdown_requestet.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void stopUi() {
        if (!ui_is_running.exchange(false)) return;
        if (ui_thread.joinable() && std::this_thread::get_id() != ui_thread.get_id()) {
                ui_thread.join();
            }
    }

    void run_ui_loop() {

        run.store(true, std::memory_order_release); 
        int idle_counter = 0;
        bool resize_enabled = false;

        while (run.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS

            if (shutdown_requestet.load()) {
                fprintf(stderr, "Exit\n");
                shutdown.store(true, std::memory_order_release);
                run.store(false, std::memory_order_release);
                close_ui.store(true, std::memory_order_release);
                closeHost();
                return;
            }
            // run the host window
            backend->poll_events();

            if (ui_dirty.exchange(false)) send_control_outputs();
            if (ui_needs_initial_update.exchange(false))
                send_initial_ui_values();
            if (ui_needs_control_update.exchange(false))
                send_control_values();

            for (auto& p : ports) {
                if (!p.is_atom || p.is_input) continue;

                auto* rb = p.atom_state->dsp_to_ui;
                while (lv2_ringbuffer_read_space(rb) >= sizeof(LV2_Atom)) {
                    LV2_Atom hdr;
                    lv2_ringbuffer_peek(rb, (char*)&hdr, sizeof(LV2_Atom));
                    const uint32_t total = sizeof(LV2_Atom) + hdr.size;
                    if (lv2_ringbuffer_read_space(rb) < total) break;
                    std::vector<uint8_t> buf(total);
                    lv2_ringbuffer_read(rb, (char*)buf.data(), total);
                    ui_desc->port_event(ui_handle, p.index, total,
                                urids.atom_eventTransfer, buf.data());
                }
            }
            // run plugin UI idle loop
            if (idle && idle->idle && ui_handle) {
                idle->idle(ui_handle);
                if (!resize_enabled) {
                    idle_counter++;
                    if (idle_counter > 30) resize_enabled = true;
                }
            }
        }
    }

    void request_shutdown() {
        shutdown_requestet.store(true, std::memory_order_release);
    }

/****************************************************************
            FIND - list available plugin
                   return a vector with plugin uri and name

****************************************************************/

    struct InfoPair {
        std::string uri;
        std::string label;
    };

    std::vector<InfoPair> find_plugin_matches(const std::string& input) {

        // uri / name
        std::vector<InfoPair> results;
        // lowercase input
        std::string needle = input;
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);

        LILV_FOREACH(plugins, i, plugs) {

            const LilvPlugin* p = lilv_plugins_get(plugs, i);
            std::string uri = lilv_node_as_uri(lilv_plugin_get_uri(p));
            const LilvNode* name_node = lilv_plugin_get_name(p);
            std::string name = name_node ? lilv_node_as_string(name_node) : "";
            std::string lname = name;

            // match rules
            bool match = false;
            if (!input.empty()) {
                std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
                // exact URI
                if (input == uri) match = true;
                // exact name
                if (input == name) match = true;
                // case-insensitive exact name
                if (needle == lname) match = true;
                // substring
                if (lname.find(needle) != std::string::npos) match = true;
                // substring
                if (uri.find(needle) != std::string::npos) match = true;
            } else {
                match = true;
            }

            if (match) {
                InfoPair info;
                info.uri = uri;
                info.label = name;
                results.emplace_back(info);
            }
        }
        std::sort(results.begin(), results.end(),
            [](const InfoPair& a, const InfoPair& b) {
                return a.label < b.label;
            });
        return results;
    }

/****************************************************************
            Preset - find available presets for plugin
                     return a list with preset uri and name
****************************************************************/

    std::vector<InfoPair> get_presets(const char* plugin_uri) {

        std::vector<InfoPair> result;
        LilvNode* uri = lilv_new_uri(world, plugin_uri);

        const LilvPlugin* plugin = lilv_plugins_get_by_uri(
                        lilv_world_get_all_plugins(world), uri);

        if (!plugin) {
            std::cerr << "Plugin not found\n";
            lilv_node_free(uri);
            return result;
        }

        LilvNode* preset_class = lilv_new_uri(world,
                    "http://lv2plug.in/ns/ext/presets#Preset");

        const LilvNodes* presets = lilv_plugin_get_related(plugin, preset_class);

        if (!presets || lilv_nodes_size(presets) == 0) {
            lilv_node_free(preset_class);
            lilv_node_free(uri);
            return result;
        }

        LilvNode* label_pred = lilv_new_uri(world,
                    "http://www.w3.org/2000/01/rdf-schema#label");

        LILV_FOREACH(nodes, i, presets) {
            const LilvNode* preset = lilv_nodes_get(presets, i);
            // load preset into world
            lilv_world_load_resource(world, preset);
            InfoPair info;
            info.uri = lilv_node_as_uri(preset);
            LilvNode* label = lilv_world_get(world, preset, label_pred, nullptr);

            if (label && lilv_node_is_string(label)) {
                info.label = lilv_node_as_string(label);
                lilv_node_free(label);
            } else {
                info.label = "(no label)";
            }
            result.push_back(info);
        }

        lilv_node_free(label_pred);
        lilv_node_free(preset_class);
        lilv_node_free(uri);

        std::sort(result.begin(), result.end(),
            [](const InfoPair& a, const InfoPair& b) {
                return a.label < b.label;
            });
        return result;
    }

/****************************************************************
            STATE - load a preset

****************************************************************/

    static void set_port_value(const char* port_symbol, void* user_data,
                   const void* value, uint32_t size, uint32_t type) {

        (void) size;
        (void) type;
        auto* self = static_cast<LV2Host*>(user_data);
        for (auto& p : self->ports) {
            if (!p.is_control) continue;
            if (strcmp(p.symbol, port_symbol) == 0) {
                if (size == sizeof(float)) p.control = *(const float*)value;
                break;
            }
        }
    }

    static char* make_path_func(LV2_State_Make_Path_Handle, const char* path) {
        return strdup(path);
    }

    static char* map_path_func(LV2_State_Map_Path_Handle, const char* abstract_path) {
        return strdup(abstract_path);
    }

    static void free_path_func(LV2_State_Free_Path_Handle, char* path) {
        free(path);
    }

    LV2_State_Map_Path map_path;
    LV2_State_Make_Path make_path;
    LV2_State_Free_Path free_path;

    void apply_preset(std::string presetUri, std::string presetLabel) {
        preset_uri = presetUri;
        preset_label = presetLabel;

        LilvNode* preset = lilv_new_uri(world, preset_uri.c_str());
        if (!preset) {
            fprintf(stderr, "Invalid preset URI\n");
            ui_needs_initial_update.store(true);
            return ;
        }

        LilvState* state = lilv_state_new_from_world(world, &um, preset);

        if (!state) {
            char* path = lilv_file_uri_parse(preset_uri.c_str(), nullptr);
            if (!path) {
                fprintf(stderr, "Preset not found\n");
                lilv_node_free(preset);
                ui_needs_initial_update.store(true);
                return ;
            }

            LilvState* state = lilv_state_new_from_file(world, &um, nullptr, path);
            free(path);

            if (!state) {
                fprintf(stderr, "Failed to load preset\n");
                lilv_node_free(preset);
                ui_needs_initial_update.store(true);
                return ;
            }
        }

        const LV2_Feature* feat[] = {
            &features.um_f,
            &features.unm_f,
            &features.map_path_feature,
            &features.make_path_feature,
            &features.free_path_feature,
            &host_worker.feature,
            nullptr
        };

        lilv_state_restore(state, instance, set_port_value, this, 0, feat);

        lilv_state_free(state);
        lilv_node_free(preset);

        ui_needs_control_update.store(true);
        ui_needs_initial_update.store(false);
    }

/****************************************************************
        IHostUiBridge - Interface from host to UI

****************************************************************/

    void list_controls() const override {
        for (const auto& p : ports) {
            if (p.is_control) {
                printf("[%d] %s = %f\n",
                    p.index,
                    p.symbol ? p.symbol : "?",
                    p.control);
            }
        }
    }

    uint32_t meter_count() const override {
        uint32_t count = 0;
        for (const auto& p : ports)
            if (p.is_control && !p.is_input)
                count++;
        return count;
    }

    uint32_t get_meter_port_index(uint32_t index) const override {
        uint32_t count = 0;
        for (const auto& p : ports) {
            if (p.is_control && !p.is_input) {
                if (index == count) return p.index;
                count++;
            }
        }
        return count;
    }

    const char* meter_name(uint32_t index) const override {
        uint32_t current = 0;
        for (const auto& p : ports) {
            if (p.is_control && !p.is_input) {
                if (current == index) return p.symbol ? p.symbol : "?";
                current++;
            }
        }
        return "?";
    }

    float get_meter(uint32_t index) const override {
        uint32_t current = 0;
        for (const auto& p : ports) {
            if (p.is_control && !p.is_input) {
                if (current == index) {
                    float min = p.fmin;
                    float max = p.fmax;
                    if (max <= min) return 0.0f;
                    float norm = (p.control - min) / (max - min);
                    return std::clamp(norm, 0.0f, 1.0f);
                }
                current++;
            }
        }
        return 0.0f;
    }

    uint32_t control_port_count() const override {
        uint32_t count = 0;
        for (const auto& p : ports)
            if (p.is_control && p.is_input)
                count++;
        return count;
    }

    uint32_t get_control_port_index(uint32_t index) const override {
        uint32_t count = 0;
        for (const auto& p : ports) {
            if (p.is_control && p.is_input) {
                if (index == count) return p.index;
                count++;
            }
        }
        return count;
    }

    void set_control(uint32_t index, float value) override {
        for (auto& p : ports) {
            if (p.index == (size_t)index && p.is_control) {
                p.control = value;
                ui_dirty.store(true);
                return;
            }
        }
    }

    float get_control(uint32_t index) const override {
        for (const auto& p : ports) {
            if (p.index == (size_t)index && p.is_control)
                return p.control;
        }

        return 0.0f;
    }

    float get_control_min(uint32_t index) const override {
        for (const auto& p : ports) {
            if (p.index == (size_t)index && p.is_control)
                return p.fmin;
        }

        return 0.0f;
    }

    float get_control_max(uint32_t index) const override {
        for (const auto& p : ports) {
            if (p.index == (size_t)index && p.is_control)
                return p.fmax;
        }

        return 0.0f;
    }

    void send_atom_to_plugin(uint32_t port, uint32_t size,
                            uint32_t type,const void* data) override {

        auto& p = ports[port];
        if (!p.is_atom || !p.atom_state) return;
        p.atom_state->ui_to_dsp.resize(size);
        memcpy(p.atom_state->ui_to_dsp.data(), data, size);
        p.atom_state->ui_to_dsp_type = type;
        p.atom_state->ui_to_dsp_pending.store(true, std::memory_order_release);
    }

    const char* port_name(uint32_t port) const override {
        return ports[port].symbol;
    }

    bool port_is_input(uint32_t port) const override {
        return ports[port].is_input;
    }

    bool port_is_control(uint32_t port) const override {
        return ports[port].is_control;
    }

private:

/****************************************************************
                        WORKER

****************************************************************/

    struct WorkerRequest {
        uint32_t size;
        uint8_t  data[0];
    };

    struct WorkerResponse {
        uint32_t size;
        uint8_t  data[0];
    };

    struct LV2HostWorker {
        lv2_ringbuffer_t* requests = nullptr;
        lv2_ringbuffer_t* responses = nullptr;

        LV2_Worker_Schedule schedule;
        LV2_Feature feature;
        const LV2_Worker_Interface* iface = nullptr;
        LV2_Handle dsp_handle;

        std::atomic<bool> running{false};;
        std::atomic<bool> work_pending{false};;
        std::thread worker_thread;
    };

    // store work request in ringbuffer
    static LV2_Worker_Status host_schedule_work(
                        LV2_Worker_Schedule_Handle handle,
                        uint32_t size, const void* data) {

        auto* w = (LV2HostWorker*)handle;
        const size_t total = sizeof(uint32_t) + size;
        if (lv2_ringbuffer_write_space(w->requests) < total)
            return LV2_WORKER_ERR_NO_SPACE;

        lv2_ringbuffer_write(w->requests, (const char*)&size, sizeof(uint32_t));
        lv2_ringbuffer_write(w->requests, (const char*)data, size);
        w->work_pending.store(true, std::memory_order_release); 

        return LV2_WORKER_SUCCESS;
    }

    // worker thread, check if work is to be done, and do it
    static void worker_thread_func(LV2HostWorker* w) {
        while (w->running.load()) {
            if (lv2_ringbuffer_read_space(w->requests) < sizeof(uint32_t)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if (lv2_ringbuffer_read_space(w->requests) < sizeof(uint32_t)) {
                continue;
            }

            uint32_t size;
            lv2_ringbuffer_peek(w->requests, (char*)&size, sizeof(uint32_t));

            if (lv2_ringbuffer_read_space(w->requests) < sizeof(uint32_t) + size) {
                 continue;
            }

            lv2_ringbuffer_read(w->requests, (char*)&size, sizeof(uint32_t));
            std::vector<uint8_t> buf(size);
            lv2_ringbuffer_read(w->requests, (char*)buf.data(), size);
            w->iface->work(w->dsp_handle, host_respond, w, size, buf.data());
        }
    }

    // store response in ringbuffer when work is done
    static LV2_Worker_Status host_respond(
                        LV2_Worker_Respond_Handle handle,
                        uint32_t size, const void* data) {

        auto* w = (LV2HostWorker*)handle;
        const size_t total = sizeof(uint32_t) + size;

        if (lv2_ringbuffer_write_space(w->responses) < total)
            return LV2_WORKER_ERR_NO_SPACE;

        lv2_ringbuffer_write(w->responses, (const char*)&size, sizeof(uint32_t));

        lv2_ringbuffer_write(w->responses,(const char*)data, size);

        return LV2_WORKER_SUCCESS;
    }

    // inform plugin when work is done
    void deliver_worker_responses(LV2HostWorker* w) {
        while (true) {
            if (lv2_ringbuffer_read_space(w->responses) < sizeof(uint32_t)) break;

            uint32_t size;
            lv2_ringbuffer_peek(w->responses, (char*)&size, sizeof(uint32_t));

            if (lv2_ringbuffer_read_space(w->responses) < sizeof(uint32_t) + size) break;

            lv2_ringbuffer_read(w->responses, (char*)&size, sizeof(uint32_t));

            std::vector<uint8_t> buf(size);
            lv2_ringbuffer_read(w->responses, (char*)buf.data(), size);

            w->iface->work_response(w->dsp_handle, size, buf.data());
        }
    }

    // stop worker thread on exit
    void stop_worker() {
        if (!host_worker.running.exchange(false))
            return;

        if (host_worker.worker_thread.joinable())
            host_worker.worker_thread.join();

        if (host_worker.requests) {
            lv2_ringbuffer_free(host_worker.requests);
            host_worker.requests = nullptr;
        }

        if (host_worker.responses) {
            lv2_ringbuffer_free(host_worker.responses);
            host_worker.responses = nullptr;
        }

        host_worker.iface = nullptr;
        host_worker.dsp_handle = nullptr;
    }

    LV2HostWorker host_worker;

/****************************************************************
                        PORT DATA

****************************************************************/

    struct AtomState {
        std::vector<uint8_t> ui_to_dsp;
        uint32_t ui_to_dsp_type = 0;
        std::atomic<bool> ui_to_dsp_pending{false};

        lv2_ringbuffer_t* dsp_to_ui = nullptr;

        AtomState(size_t sz = 16384) {
            dsp_to_ui = lv2_ringbuffer_create(sz);
        }

        ~AtomState() {
            lv2_ringbuffer_free(dsp_to_ui);
        }
    };

    struct Port {
        uint32_t index = 0;
        bool is_audio = false;
        bool is_input = false;
        bool is_control = false;
        bool is_atom = false;
        bool is_midi = false;

        float control = 0.0f;
        float defvalue = 0.0f;
        float fmin = 0.0f;
        float fmax = 0.0f;

        std::unique_ptr<IDspPort> engine_port;

        LV2_Atom_Sequence* atom = nullptr;
        uint32_t atom_buf_size = 8192;
        AtomState* atom_state = nullptr;

        std::string uri;
        const char* symbol = nullptr;
    };

/****************************************************************
                        URIDs

****************************************************************/

    struct {
        LV2_URID atom_eventTransfer;
        LV2_URID atom_Sequence;
        LV2_URID atom_Object;
        LV2_URID atom_Float;
        LV2_URID atom_Int;
        LV2_URID atom_Double;
        LV2_URID atom_Bool;
        LV2_URID midi_Event;
        LV2_URID buf_maxBlock;
        LV2_URID atom_Path;
        LV2_URID atom_String;
        LV2_URID patch_Get;
        LV2_URID patch_Set;
        LV2_URID patch_property;
        LV2_URID patch_value;
        LV2_URID atom_URID;
        LV2_URID atom_Blank;
        LV2_URID atom_Chunk;
        LV2_URID param_sampleRate;
    } urids;

    void init_urids() {
        urids.atom_eventTransfer = map_uri(this, LV2_ATOM__eventTransfer);
        urids.atom_Sequence    = map_uri(this, LV2_ATOM__Sequence);
        urids.atom_Blank       = map_uri(this, LV2_ATOM__Blank);
        urids.atom_Chunk       = map_uri(this, LV2_ATOM__Chunk);
        urids.atom_Object      = map_uri(this, LV2_ATOM__Object);
        urids.atom_Float       = map_uri(this, LV2_ATOM__Float);
        urids.atom_Int         = map_uri(this, LV2_ATOM__Int);
        urids.atom_Double      = map_uri(this, LV2_ATOM__Double);
        urids.atom_Bool        = map_uri(this, LV2_ATOM__Bool);
        urids.midi_Event       = map_uri(this, LV2_MIDI__MidiEvent);
        urids.buf_maxBlock     = map_uri(this, LV2_BUF_SIZE__maxBlockLength);
        urids.atom_Path        = map_uri(this, LV2_ATOM__Path);
        urids.atom_String      = map_uri(this, LV2_ATOM__String);

        urids.patch_Get        = map_uri(this, LV2_PATCH__Get);
        urids.patch_Set        = map_uri(this, LV2_PATCH__Set);
        urids.patch_property   = map_uri(this, LV2_PATCH__property);
        urids.patch_value      = map_uri(this, LV2_PATCH__value);
        urids.atom_URID        = map_uri(this, LV2_ATOM__URID);
        urids.param_sampleRate = map_uri(this, LV2_PARAMETERS__sampleRate);
    }

    static LV2_URID map_uri(LV2_URID_Map_Handle h, const char* uri) {
        auto* self = static_cast<LV2Host*>(h);
        auto it = self->urid_map.find(uri);
        if (it != self->urid_map.end())
            return it->second;

        LV2_URID id = self->urid_map.size() + 1;
        self->urid_map[uri] = id;
        self->urid_unmap[id]  = uri;
        return id;
    }

    static const char* unmap_uri(LV2_URID_Unmap_Handle h, LV2_URID urid) {
        auto* self = static_cast<LV2Host*>(h);

        auto it = self->urid_unmap.find(urid);
        if (it == self->urid_unmap.end())
            return nullptr;

        return it->second.c_str();
    }

    std::unordered_map<std::string, LV2_URID> urid_map;
    std::unordered_map<LV2_URID, std::string> urid_unmap;

    LV2_URID_Map um;
    LV2_URID_Unmap unm;

/****************************************************************
                        FEATURES

****************************************************************/
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

    void init_features() {
        um.handle = this;
        um.map = map_uri;
        unm.handle = this;
        unm.unmap = unmap_uri;

        map_path.handle      = nullptr;
        map_path.abstract_path = map_path_func;
        make_path.handle = nullptr;
        make_path.path   = make_path_func;
        free_path.handle = nullptr;
        free_path.free_path = free_path_func;
        
        features.bbl_feature.URI  = LV2_BUF_SIZE__boundedBlockLength;
        features.bbl_feature.data = NULL;

        features.um_f.URI = LV2_URID__map;
        features.um_f.data = &um;

        features.unm_f.URI = LV2_URID__unmap;
        features.unm_f.data = &unm;

        features.map_path_feature.URI = LV2_STATE__mapPath;
        features.map_path_feature.data = &map_path;

        features.make_path_feature.URI = LV2_STATE__makePath;
        features.make_path_feature.data = &make_path;

        features.free_path_feature.URI = LV2_STATE__freePath;
        features.free_path_feature.data = &free_path;

        host_worker.schedule.handle = &host_worker;
        host_worker.schedule.schedule_work = host_schedule_work;
        host_worker.feature.URI  = LV2_WORKER__schedule;
        host_worker.feature.data = &host_worker.schedule;

        features.data_access.data_access = data_access_cb;
        features.data_access_feature.URI  = LV2_DATA_ACCESS_URI;
        features.data_access_feature.data = &features.data_access;
    }

/****************************************************************
        LILV - init world and check if plugin is supported

****************************************************************/

    bool feature_is_supported(const char* uri, const LV2_Feature*const* f) {
        for (; *f; ++f)
            if (!strcmp(uri, (*f)->URI)) return true;
        return false;
    }

    bool check_resize_port_requirements(const LilvPlugin* plugin) {
        uint32_t n = lilv_plugin_get_num_ports(plugin);
        LilvNode* min_size =
            lilv_new_uri(world, LV2_RESIZE_PORT__minimumSize);
        bool ok = true;

        for (uint32_t i = 0; i < n; ++i) {
            const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
            if (!lilv_port_is_a(plugin, port, atom_class)) continue;
            LilvNodes* sizes = lilv_port_get_value(plugin, port, min_size);
            if (!sizes || lilv_nodes_size(sizes) == 0) continue;
            const LilvNode* n = lilv_nodes_get_first(sizes);
            uint32_t required = lilv_node_as_int(n);

            //fprintf(stderr, "Atom port %s requires minimumSize = %u\n",
            //    lilv_node_as_string(lilv_port_get_symbol(plugin, port)),required);

            if (required > required_atom_size) {
                required_atom_size = required;
                //ok = false; // in case we don't want support resize
            }
            lilv_nodes_free(sizes);
        }
        lilv_node_free(min_size);
        return ok;
    }

    bool checkFeatures(const LilvPlugin* plugin, const LV2_Feature*const* feat) {
        LilvNodes* requests = lilv_plugin_get_required_features(plugin);
        LILV_FOREACH(nodes, f, requests) {
            const char* uri = lilv_node_as_uri(lilv_nodes_get(requests, f));
            if (!feature_is_supported(uri, feat)) {
                //fprintf(stderr, "Plugin %s \n", lilv_node_as_string(lilv_plugin_get_uri(plugin)));
                fprintf(stderr, "Feature %s is not supported\n", uri);
                lilv_nodes_free(requests);
                return false;
            }
        }
        lilv_nodes_free(requests);
        return true;
    }

    bool init_lilv() {

        plugin = lilv_plugins_get_by_uri(plugs, lilv_new_uri(world, plugin_uri));
        if (!plugin) return false;
        plugin_name = "lv2-x11-host";
        const LilvNode* nd = nullptr;
        nd = lilv_plugin_get_name(plugin);
        plugin_name = lilv_node_as_string(nd);

        audio_class     = lilv_new_uri(world, LV2_CORE__AudioPort);
        control_class   = lilv_new_uri(world, LV2_CORE__ControlPort);
        atom_class      = lilv_new_uri(world, LV2_ATOM__AtomPort);
        input_class     = lilv_new_uri(world, LV2_CORE__InputPort);
        x11_class       = lilv_new_uri(world, LV2_UI__X11UI);
        rsz_minimumSize = lilv_new_uri (world, LV2_RESIZE_PORT__minimumSize);
        init_urids();
        init_features();
        lilv_is_inited.store(true);
        if (!check_resize_port_requirements(plugin)) {
            fprintf(stderr,"%s requires resize-port support – not supported\n", plugin_name.data());
            return false;
        }

        return true;
    }

    void freeNodes() {
        if (!lilv_is_inited.load()) return;
        lilv_node_free (audio_class);
        lilv_node_free (control_class);
        lilv_node_free (atom_class);
        lilv_node_free (input_class);
        lilv_node_free (x11_class);
        lilv_node_free (rsz_minimumSize);
    }

/****************************************************************
        IDspEngine - setup audio/midi back-end

****************************************************************/

    static void process_wrapper(uint32_t nframes,void* userdata) {
        auto* self = static_cast<LV2Host*>(userdata);
        self->process(nframes);
    }

    bool init_engine() {
        if (!engine->open(plugin_name)) return false;

        max_block_length = engine->buffer_size();
        engine->set_process_callback(&LV2Host::process_wrapper,this);
        return true;
    }

/****************************************************************
                PORTS - init plugin ports

****************************************************************/

    bool init_ports() {
        uint32_t n = lilv_plugin_get_num_ports(plugin);
        ports.reserve(n);
        LilvNode* midi_event = lilv_new_uri(world, LV2_MIDI__MidiEvent);

        for (uint32_t i = 0; i < n; ++i) {
            const LilvPort* lp = lilv_plugin_get_port_by_index(plugin, i);
            Port p;
            p.index = i;

            p.is_audio   = lilv_port_is_a(plugin, lp, audio_class);
            p.is_control = lilv_port_is_a(plugin, lp, control_class);
            p.is_atom    = lilv_port_is_a(plugin, lp, atom_class);
            p.is_input   = lilv_port_is_a(plugin, lp, input_class);
            p.is_midi    = lilv_port_supports_event(plugin, lp, midi_event);

            const LilvNode* sym = lilv_port_get_symbol(plugin, lp);
            if (sym) {
                p.uri = std::string(lilv_node_as_uri(lilv_plugin_get_uri(plugin)))
                      + "#" + lilv_node_as_string(sym);
                p.symbol = lilv_node_as_string(sym);
                }

            if (p.is_audio) {
                p.engine_port = engine->create_audio_port(sym ? 
                    p.symbol : "audio",p.is_input ? true : false);
            }

            if (p.is_atom && p.is_midi) {
                p.engine_port = engine->create_midi_port(sym ? 
                    p.symbol : "midi",p.is_input ? true : false);
            }

            if (p.is_atom) {
                p.atom_buf_size = required_atom_size;

                p.atom = (LV2_Atom_Sequence*)aligned_alloc(64, p.atom_buf_size);
                memset(p.atom, 0, p.atom_buf_size);
                p.atom->atom.type = urids.atom_Sequence;

                if (p.is_input) {
                    p.atom->atom.size = sizeof(LV2_Atom_Sequence_Body);
                    p.atom->body.unit = 0;
                    p.atom->body.pad  = 0;
                } else {
                    p.atom->atom.size = 0;
                }

                p.atom_state = new AtomState;
            }

            if (p.is_control && p.is_input) {
                LilvNode *pdflt, *pmin, *pmax;
                lilv_port_get_range(plugin, lp, &pdflt, &pmin, &pmax);
                if (pmin) {
                    p.fmin = lilv_node_as_float(pmin);
                    lilv_node_free(pmin);
                }
                if (pmax) {
                    p.fmax = lilv_node_as_float(pmax);
                    lilv_node_free(pmax);
                }
                if (pdflt) {
                    p.defvalue = lilv_node_as_float(pdflt);
                    lilv_node_free(pdflt);
                }
            }

            ports.emplace_back(std::move(p));
        }
        lilv_node_free(midi_event);
        return true;
    }

/****************************************************************
                DSP - init plugin instance

****************************************************************/

    bool init_instance() {

        LV2_Options_Option options[] = {
            {LV2_OPTIONS_INSTANCE, 0, urids.buf_maxBlock,
                sizeof(uint32_t), urids.atom_Int, &max_block_length},
            { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr }
        };

        LV2_Feature opt_f { LV2_OPTIONS__options, options };

        LV2_Feature* feats[] = { &features.um_f, &features.unm_f, &opt_f,
                            &features.bbl_feature, &host_worker.feature, nullptr };

        if (!checkFeatures(plugin, feats)) return false;
        // instantiate the plugin dsp instance 
        instance = lilv_plugin_instantiate(plugin, engine->sample_rate(), feats);
        if (!instance) return false;
        // check if plugin require a worker thread 
        const LV2_Worker_Interface* iface = (const LV2_Worker_Interface*)
            lilv_instance_get_extension_data( instance, LV2_WORKER__interface);
        // start worker when plugin wants it
        if (iface) {
            host_worker.iface = iface;
            host_worker.dsp_handle = lilv_instance_get_handle(instance);
            host_worker.requests  = lv2_ringbuffer_create(8192);
            host_worker.responses = lv2_ringbuffer_create(8192);
            host_worker.running.store(true);
            host_worker.worker_thread =
                std::thread(worker_thread_func, &host_worker);
        }
        // connect control and atom ports
        for (auto& p : ports) {
            if (p.is_audio) continue;
            if (p.is_control)
                lilv_instance_connect_port(instance, p.index, &p.control);
            if (p.is_atom)
                lilv_instance_connect_port(instance, p.index, p.atom);
        }
        lilv_instance_activate(instance);
        return true;
    }

/****************************************************************
            PROCESS - run the audio/midi process
                      deliver and read atom ports

****************************************************************/

#if defined (DEBUG)
#include "LV2HostDebug.hpp"
#endif

    int process(uint32_t nframes) {
        if (shutdown.load()) return 0;
        for (Port& p : ports) {
            // connect all audio ports
            if (p.is_audio) {
                auto* ap = dynamic_cast<IAudioPort*>(p.engine_port.get());
                void* buf = ap->audio_buffer(nframes);
                lilv_instance_connect_port(instance, p.index, buf);
            }
            // prepare atom output buffers for plugin write  
            if (p.is_atom && !p.is_input) {
                p.atom->atom.type = 0;
                p.atom->atom.size = p.atom_buf_size - sizeof(LV2_Atom);
            }
            // handle atom input ports
            if (p.is_atom && p.is_input) {
                // handle midi input
                if (p.is_midi) {
                    IMidiPort* mp = dynamic_cast<IMidiPort*>(p.engine_port.get());
                    IMidiBuffer* mb = mp->midi_buffer(nframes);
                    uint32_t event_count = mb->event_count();
                    lv2_atom_sequence_clear(p.atom);
                    p.atom->atom.type = urids.atom_Sequence;
                    p.atom->atom.size = sizeof(LV2_Atom_Sequence_Body);
                    for (uint32_t i = 0; i < event_count; ++i) {
                        MidiEvent ev;
                        mb->get_event(i, ev);
                        uint8_t evbuf[sizeof(LV2_Atom_Event) + required_atom_size];
                        LV2_Atom_Event* aev = (LV2_Atom_Event*)evbuf;
                        aev->time.frames = ev.frame;
                        aev->body.type  = urids.midi_Event;
                        aev->body.size  = ev.size;
                        memcpy(LV2_ATOM_BODY(&aev->body), ev.data, ev.size);
                        lv2_atom_sequence_append_event(p.atom, p.atom_buf_size, aev);
                        #if defined(DEBUG)
                        dump_atom_event(aev, "midi input");
                        #endif
                    }
                }
                // handle atom messages from GUI to dsp
                if (p.atom_state->ui_to_dsp_pending.exchange(false)) {
                    p.atom->atom.type = urids.atom_Sequence;
                    p.atom->atom.size = 0;
                    const uint32_t body_size = p.atom_state->ui_to_dsp.size();
                    uint8_t evbuf[sizeof(LV2_Atom_Event) + required_atom_size];
                    LV2_Atom_Event* ev = (LV2_Atom_Event*)evbuf;
                    ev->time.frames = 0;
                    ev->body.type  = p.atom_state->ui_to_dsp_type;
                    ev->body.size  = body_size;
                    memcpy((uint8_t*)LV2_ATOM_BODY(&ev->body),
                        p.atom_state->ui_to_dsp.data(), body_size);
                    lv2_atom_sequence_append_event( p.atom, p.atom_buf_size, ev);
                    #if defined(DEBUG)
                    dump_atom_event(ev, "UI => DSP");
                    #endif
                }
            }
        }
        // run the plugin
        lilv_instance_run(instance, nframes);
        // deliver worker response (work done)
        if (host_worker.iface ) deliver_worker_responses(&host_worker);
        // handle atom output ports (dsp to GUI)
        for (Port& p : ports) {
            // send control output port values to the UI
            if (p.is_control && !p.is_input) ui_dirty.store(true);
            // reset atom input port buffer after dsp have read it
            if (p.is_atom && p.is_input) p.atom->atom.size = 0;
            // handle atom messages from dsp to UI (using a ringbuffer)
            if (p.is_atom && !p.is_input) {
                LV2_Atom_Sequence* seq = p.atom;
                LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
                    if (ev->body.size == 0)break;
                    if (p.atom->atom.type == 0) break;
                    #if defined(DEBUG)
                    dump_atom_event(ev, "DSP => UI");
                    #endif
                    const uint32_t total = sizeof(LV2_Atom) + ev->body.size;
                    if (lv2_ringbuffer_write_space(p.atom_state->dsp_to_ui) >= total) {
                        lv2_ringbuffer_write(p.atom_state->dsp_to_ui, (const char*)&ev->body, total);
                    }
                    // forward midi output to IMidiPort
                    if (ev->body.type == urids.midi_Event && p.is_midi) {
                        const uint8_t* midi = (const uint8_t*)LV2_ATOM_BODY(&ev->body);
                        const uint32_t size = ev->body.size;
                        const uint32_t frame = ev->time.frames;

                        IMidiPort* mp = dynamic_cast<IMidiPort*>(p.engine_port.get());
                        IMidiBuffer* mb = mp->midi_buffer(nframes);
                        mb->clear();
                        mb->write_event(frame, midi, size);
                        
                    }
                }
                p.atom->atom.type = 0;
                p.atom->atom.size = required_atom_size;
            }
        }
        return 0;
    }

/****************************************************************
                UI - Helper functions 

****************************************************************/

    static void ui_write(LV2UI_Controller c, uint32_t port,
                uint32_t size, uint32_t type, const void* buf) {

        auto* self = static_cast<LV2Host*>(c);
        auto& p = self->ports[port];

        if (p.is_control && size == sizeof(float)) {
            p.control = *(const float*)buf;
            #if defined(DEBUG)
            fprintf(stderr, "ui_write %s -> %f\n", p.symbol ? p.symbol : "?", p.control);
            #endif
            return;
        }

        if (p.is_atom) {
            p.atom_state->ui_to_dsp.resize(size);
            memcpy(p.atom_state->ui_to_dsp.data(), buf, size);
            p.atom_state->ui_to_dsp_type = type;
            p.atom_state->ui_to_dsp_pending.store(true, std::memory_order_release);
        }
    }

    static uint32_t ui_port_map(LV2UI_Feature_Handle h, const char* uri) {
        auto* self = static_cast<LV2Host*>(h);
        for (auto& p : self->ports)
            if (p.uri == uri) return p.index;
        return LV2UI_INVALID_PORT_INDEX;
    }

    static int ui_resize(LV2UI_Feature_Handle h, int w, int hgt) {
        auto* self = static_cast<LV2Host*>(h);
        self->backend->resize(w, hgt);
        return 0;
    }

    void load_defaults() {
        for (auto& p : ports)
            if (p.is_control && p.is_input) {
                p.control = p.defvalue;
            }
    }

    void send_initial_ui_values() {
        if (!ui_handle) return;
        for (auto& p : ports)
            if (p.is_control && p.is_input) {
                p.control = p.defvalue;
                ui_desc->port_event(
                    ui_handle, p.index, sizeof(float), 0, &p.defvalue);
            }
    }

    void send_control_values() {
        if (!ui_handle) return;
        for (auto& p : ports)
            if (p.is_control && p.is_input) {
                ui_desc->port_event(
                    ui_handle, p.index, sizeof(float), 0, &p.control);
            }
    }

    void send_control_outputs() {
        if (!ui_handle) return;
        for (auto& p : ports)
            if (p.is_control && !p.is_input) {
                ui_desc->port_event(
                    ui_handle, p.index, sizeof(float), 0, &p.control);
            }
    }

    void destroy_ui() {
        if (!ui_handle) return;
        if (ui_desc && ui_handle) {
            ui_desc->cleanup(ui_handle);
            ui_handle = nullptr;
        }
    }

    static unsigned int host_ui_supported(const char* ui_type, const char* host_type) {
        if (!ui_type || !host_type) return 0;
        return strcmp(ui_type, host_type) == 0;
    }

    void select_backend_for_plugin() {
        //backend.reset();
        const LilvUIs* uis = lilv_plugin_get_uis(plugin);
        if (!uis) return;
        for (auto& b : available_backends) {
            if (!b->lv2_ui_uri()) continue;
            LilvNode* backend_uri = lilv_new_uri(world, b->lv2_ui_uri());
            LILV_FOREACH(uis, i, uis) {
                const LilvUI* cand = lilv_uis_get(uis, i);
                const LilvNode* ui_type = nullptr;
                if (lilv_ui_is_supported(cand, host_ui_supported,
                                         backend_uri, &ui_type)) {
                    backend = b;
                    backend->attach_bridge(this);
                    backend->set_close_callback([this]() {request_shutdown();});
                    lilv_node_free(backend_uri);
                    return;
                }
            }
            lilv_node_free(backend_uri);
        }
        if (!available_backends.empty()) {
            backend = available_backends.front();
            backend->attach_bridge(this);
            backend->set_close_callback([this]() {request_shutdown();});
        }
    }

    static const void* data_access_cb(const char* uri) {
        extern LV2Host* current_host;
        if (!current_host || !current_host->instance) return nullptr;
        return lilv_instance_get_extension_data(
            current_host->instance, uri);
    }

/****************************************************************
            UI -init the UI (Host and Plugin)

****************************************************************/

    bool init_ui() {
        if (backend && !backend->lv2_ui_uri()) { // non gui mode
            if (!ui_needs_control_update.load()) load_defaults();
            return true;
        }
        // Find supported LV2 UI
        const LilvUIs* uis = lilv_plugin_get_uis(plugin);
        const LilvUI* ui = nullptr;
        const LilvNode* ui_type = nullptr;
        char* gui_uri = nullptr;

        // get supported ui type from backend
        LilvNode* backend_uri = lilv_new_uri(world, backend->lv2_ui_uri());
        // check if backend support plugin ui
        LILV_FOREACH(uis, i, uis) {
            const LilvUI* cand = lilv_uis_get(uis, i);
            if (lilv_ui_is_supported(cand, host_ui_supported, backend_uri, &ui_type)) {
                ui = cand;
                gui_uri = strdup (lilv_node_as_uri (lilv_ui_get_uri(ui)));
                break;
            }
        }

        lilv_node_free(backend_uri);
        // should never happen, as we've checked that already 
        // in select_backend_for_plugin()
        if (!ui) {
            backend = available_backends.front();
            backend->attach_bridge(this);
            backend->set_close_callback([this]() {request_shutdown();});
            if (!ui_needs_control_update.load()) load_defaults();
            if (!backend->create_window(640, 480)) {
                return false;
            }
            return true;
        }

        // Load UI binary
        char* so = lilv_node_get_path(lilv_ui_get_binary_uri(ui), nullptr);
        char* bundle = lilv_node_get_path(lilv_ui_get_bundle_uri(ui), nullptr);
        ui_dl = dlopen(so, RTLD_NOW);
        free(so);

        if (!ui_dl) {
            fprintf(stderr, "dlopen failed\n");
            free(bundle);
            free(gui_uri);
            backend = std::make_shared<NoGuiBackend>();
            backend->attach_bridge(this);
            backend->set_close_callback([this]() {request_shutdown();});
            if (!ui_needs_control_update.load()) load_defaults();
            return true;
        }

        auto fn = (const LV2UI_Descriptor* (*)(uint32_t)) dlsym(ui_dl, "lv2ui_descriptor");
        const LV2UI_Descriptor* plugin_gui = nullptr;
        uint32_t index = 0;

        while (fn) {
            plugin_gui = fn(index);
            if (!plugin_gui) break;
            if (!strcmp(plugin_gui->URI, gui_uri)) break;
            ++index;
        }
        free(gui_uri);

        if (!plugin_gui) {
            free(bundle);
            backend = std::make_shared<NoGuiBackend>();
            backend->attach_bridge(this);
            backend->set_close_callback([this]() {request_shutdown();});
            if (!ui_needs_control_update.load()) load_defaults();
            return true;
        }
        ui_desc = plugin_gui;
        // Create backend window
        if (!backend->create_window(640, 480)) {
            free(bundle);
            return false;
        }

        // LV2 instantiate
        float ui_sample_rate = (float)engine->sample_rate();

        LV2_Options_Option ui_options[] = {
            {LV2_OPTIONS_INSTANCE, 0, urids.param_sampleRate,
                sizeof(float), urids.atom_Float, &ui_sample_rate},
            { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr }
        };

        LV2_Feature ui_options_feature = {LV2_OPTIONS__options, ui_options };

        resize.handle = this;
        resize.ui_resize = ui_resize;

        LV2UI_Port_Map pm { this, ui_port_map };

        LV2_Feature pm_f { LV2_UI__portMap, &pm };
        LV2_Feature parent {
            LV2_UI__parent,
            (void*)backend->native_window()
        };
        LV2_Feature resize_f { LV2_UI__resize, &resize };

        LV2_Handle plugin_instance =
            lilv_instance_get_handle(instance);

        LV2_Feature instance_access_feature;
        instance_access_feature.URI  = LV2_INSTANCE_ACCESS_URI;
        instance_access_feature.data = plugin_instance;

        LV2_Feature* feats[] = {&parent, &resize_f, &pm_f, &ui_options_feature, 
            &features.um_f, &features.unm_f, &instance_access_feature, &features.data_access_feature, nullptr};
        current_host = this;
        ui_handle = ui_desc->instantiate(ui_desc, plugin_uri, bundle, ui_write,
                                                        this, &ui_widget, feats);
        current_host = nullptr;
        free(bundle);
        if (!ui_handle) return false;

        // Embed UI + finalize window
        backend->embed_native(ui_widget);
        std::string name = plugin_name;
        if (!preset_label.empty()) name += " - " + preset_label;
        backend->finalize_window(name.c_str());
        if (!ui_needs_control_update.load()) {
            load_defaults();
            ui_needs_initial_update.store(true);
        }

        if (ui_desc && ui_desc->extension_data) {
            const void* ext = ui_desc->extension_data(LV2_UI__idleInterface);
            idle = static_cast<const LV2UI_Idle_Interface*>(ext);
        }

        return true;
    }


/****************************************************************
        HOST DATA - private host data members

****************************************************************/

    std::shared_ptr<LV2HostContext> ctx;
    const char* plugin_uri;
    std::string preset_uri;
    std::string preset_label;
    std::string plugin_name;

    LilvWorld* world = nullptr;
    const LilvPlugins* plugs =  nullptr;
    const LilvPlugin* plugin = nullptr;
    LilvInstance* instance = nullptr;
    const LV2UI_Idle_Interface* idle = nullptr;

    LilvNode *audio_class, *control_class, *atom_class,
             *input_class, *x11_class,*rsz_minimumSize;

    std::unique_ptr<IDspEngine> engine;
    std::vector<std::shared_ptr<IUiBackend>> available_backends;
    std::shared_ptr<IUiBackend> backend = nullptr; 

    std::vector<Port> ports;

    LV2UI_Resize resize;
    void* ui_dl = nullptr;
    const LV2UI_Descriptor* ui_desc = nullptr;
    LV2UI_Handle ui_handle = nullptr;
    LV2UI_Widget ui_widget = nullptr;

    //Display* x_display = nullptr;
    //Window x_window = 0;
    int wx = 640;
    int wy = 480;

    uint32_t max_block_length = 4096;
    uint32_t required_atom_size = 8192;

    std::thread ui_thread;
    std::atomic<bool> ui_is_running{false};
    std::atomic<bool> lilv_is_inited{false};
    std::atomic<bool> ui_dirty{false};
    std::atomic<bool> ui_needs_initial_update{false};
    std::atomic<bool> ui_needs_control_update{false};
    std::atomic<bool> run{false};
    std::atomic<bool> shutdown{false};
    std::atomic<bool> shutdown_requestet{false};
};

