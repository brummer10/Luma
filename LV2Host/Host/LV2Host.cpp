

#include <dlfcn.h>
#include <unistd.h>


/*
 * LV2Host.cpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#include <cstring>
#include <cstdlib>
#include <cassert>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <filesystem>
#include <cctype>

#include "LV2Host.h"
#include "NoGuiBackend.hpp"
#include "DummyEngine.hpp"


thread_local LV2Host* current_host = nullptr;

/****************************************************************
        LV2Host - class to load and run LV2 plugins

****************************************************************/

LV2Host::LV2Host() : ctx(LV2HostContext::acquire())
        , world(ctx->world()), plugs(ctx->plugs()) {

    engine = std::make_unique<DummyEngine>();
    auto noGui = std::make_shared<NoGuiBackend>();
    register_ui_backend(noGui);
    backend = noGui;
}

LV2Host::~LV2Host() {
    stopUi();
    closeHost();
}

bool LV2Host::init(const char* uri) {
    plugin_uri = uri;

    if (!world) return false;

    bool set = init_lilv() && init_engine() && init_ports() && init_instance();

    if (set) {
        init_state(world, plugin, instance, um, unm, &ports, plugin_name,
                        &ui_needs_control_update, &ui_needs_initial_update);
        store_default(feat_);
    } else {
        closeHost();
    }
    return set;
}

void LV2Host::closeHost() {

    if (instance) lilv_instance_deactivate(instance);

    ctx->unregister_worker(&host_worker);
    host_worker.stop_worker();

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

    if (backend) backend->close_window();

    if (world) {
        freeNodes();
        world = nullptr;
    }

    is_down.store(true);
}

void LV2Host::set_engine(std::unique_ptr<IDspEngine> e) {
    #ifndef DEBUG
    engine = std::move(e);
    #else
    (void) e;
    #endif
}

void LV2Host::register_ui_backend(std::shared_ptr<IUiBackend> b) {
    if (b) available_backends.push_back(b);
}

bool LV2Host::is_nogui() {
    return  !backend || backend->lv2_ui_uri() == nullptr ? true : false;
}

bool LV2Host::initUi() {
    select_backend_for_plugin();
    if (init_ui()) return engine->activate();
    return false;
}

/****************************************************************
                        UI LOOP

****************************************************************/

void LV2Host::startUi() {

    if (!ui_is_running.load()) {
        ui_is_running.store(true);
        setRun();
        ui_thread = std::thread(&LV2Host::run_ui_loop, this);

        if (!backend->lv2_ui_uri()) {
            while (!shutdown_requestet.load())
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(50));
        }
    }
}

void LV2Host::setRun() {
    run.store(true, std::memory_order_release); 
}

bool LV2Host::getRun() {
    return run.load(); 
}

bool LV2Host::isDown() {
    return is_down.load(); 
}

void LV2Host::run_ui_loop() {

    while (run.load()) {
        std::this_thread::sleep_for( std::chrono::milliseconds(16));
        runUi();
    }
}

void LV2Host::stopUi() {
    if (!ui_is_running.exchange(false)) return;
    if (ui_thread.joinable() && std::this_thread::get_id() != ui_thread.get_id()) {
            ui_thread.join();
        }
}

void LV2Host::runUi() {
    if (run.load()) {
        if (shutdown_requestet.load()) {
            //fprintf(stderr, "Exit\n");
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
        }
    }
}

void LV2Host::request_shutdown() {
    shutdown_requestet.store(true, std::memory_order_release);
}


/****************************************************************
            FIND - list available plugin
                   return a vector with plugin uri and name

****************************************************************/

std::vector<InfoPair> LV2Host::find_plugin_matches(const std::string& input) {
    return ctx->registry.find_plugin_matches(input);
}

/****************************************************************
            Preset - find available presets for plugin
                     return a list with preset uri and name
****************************************************************/

std::vector<InfoPair> LV2Host::get_presets(const char* pluginUri) {
    return ctx->registry.get_presets(pluginUri);
}


/****************************************************************
        IHostUiBridge - Interface from host to UI

****************************************************************/

void LV2Host::list_controls() const {
    for (const auto& p : ports) {
        if (p.is_control) {
            printf("[%d] %s = %f\n",
                p.index,
                p.symbol ? p.symbol : "?",
                p.control);
        }
    }
}

uint32_t LV2Host::meter_count() const {
    uint32_t count = 0;
    for (const auto& p : ports)
        if (p.is_control && !p.is_input)
            count++;
    return count;
}

uint32_t LV2Host::get_meter_port_index(uint32_t index) const {
    uint32_t count = 0;
    for (const auto& p : ports) {
        if (p.is_control && !p.is_input) {
            if (index == count) return p.index;
            count++;
        }
    }
    return count;
}

const char* LV2Host::meter_name(uint32_t index) const {
    uint32_t current = 0;
    for (const auto& p : ports) {
        if (p.is_control && !p.is_input) {
            if (current == index) return p.symbol ? p.symbol : "?";
            current++;
        }
    }
    return "?";
}

float LV2Host::get_meter(uint32_t index) const {
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

uint32_t LV2Host::control_port_count() const {
    uint32_t count = 0;
    for (const auto& p : ports)
        if (p.is_control && p.is_input)
            count++;
    return count;
}

uint32_t LV2Host::get_control_port_index(uint32_t index) const {
    uint32_t count = 0;
    for (const auto& p : ports) {
        if (p.is_control && p.is_input) {
            if (index == count) return p.index;
            count++;
        }
    }
    return count;
}

void LV2Host::set_control(uint32_t index, float value) {
    for (auto& p : ports) {
        if (p.index == (size_t)index && p.is_control) {
            p.control = value;
            ui_dirty.store(true);
            return;
        }
    }
}

float LV2Host::get_control(uint32_t index) const {
    for (const auto& p : ports) {
        if (p.index == (size_t)index && p.is_control)
            return p.control;
    }

    return 0.0f;
}

float LV2Host::get_control_min(uint32_t index) const {
    for (const auto& p : ports) {
        if (p.index == (size_t)index && p.is_control)
            return p.fmin;
    }

    return 0.0f;
}

float LV2Host::get_control_max(uint32_t index) const {
    for (const auto& p : ports) {
        if (p.index == (size_t)index && p.is_control)
            return p.fmax;
    }

    return 0.0f;
}

void LV2Host::send_atom_to_plugin(uint32_t port, uint32_t size,
                        uint32_t type,const void* data) {

    auto& p = ports[port];
    if (!p.is_atom || !p.atom_state) return;
    p.atom_state->ui_to_dsp.resize(size);
    memcpy(p.atom_state->ui_to_dsp.data(), data, size);
    p.atom_state->ui_to_dsp_type = type;
    p.atom_state->ui_to_dsp_pending.store(true, std::memory_order_release);
}

const char* LV2Host::port_name(uint32_t port) const {
    return ports[port].symbol;
}

bool LV2Host::port_is_input(uint32_t port) const {
    return ports[port].is_input;
}

bool LV2Host::port_is_control(uint32_t port) const {
    return ports[port].is_control;
}

void LV2Host::set_resource(void* res) {
    hres = res;
}

void* LV2Host::get_resource() const {
    return hres;
}

const std::vector<InfoPair> LV2Host::get_presets() {
    return get_presets(plugin_uri.c_str());
}

void LV2Host::applyPreset(const std::string& uri, const std::string& label) {
    host_worker.wait_until_idle();
    host_worker.clear_responses();
    apply_preset(uri, feat_);
    preset_label = label;
    if (backend) backend->set_preset_name(preset_label);
}

bool LV2Host::savePresetBundle(const std::string& preset_name) {
    return save_preset_bundle(preset_name, feat_);
}

void LV2Host::restoreDefaults() {
    host_worker.wait_until_idle();
    host_worker.clear_responses();
    restore_default(feat_);
    ui_needs_initial_update.store(true);
}

const std::string& LV2Host::getPluginName() const {
    return plugin_name;
}

/****************************************************************
                        URIDs

****************************************************************/

void LV2Host::init_urids() {
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

LV2_URID LV2Host::map_uri(LV2_URID_Map_Handle h, const char* uri) {
    auto* self = static_cast<LV2Host*>(h);
    auto it = self->urid_map.find(uri);
    if (it != self->urid_map.end())
        return it->second;

    LV2_URID id = self->urid_map.size() + 1;
    self->urid_map[uri] = id;
    self->urid_unmap[id]  = uri;
    return id;
}

const char* LV2Host::unmap_uri(LV2_URID_Unmap_Handle h, LV2_URID urid) {
    auto* self = static_cast<LV2Host*>(h);

    auto it = self->urid_unmap.find(urid);
    if (it == self->urid_unmap.end())
        return nullptr;

    return it->second.c_str();
}


/****************************************************************
                        FEATURES

****************************************************************/

char* LV2Host::make_path_func(LV2_State_Make_Path_Handle, const char* path) {
    return strdup(path);
}

char* LV2Host::map_path_func(LV2_State_Map_Path_Handle, const char* abstract_path) {
    return strdup(abstract_path);
}

void LV2Host::free_path_func(LV2_State_Free_Path_Handle, char* path) {
    free(path);
}

void LV2Host::init_features() {
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
    host_worker.schedule.schedule_work = host_worker.host_schedule_work;
    host_worker.feature.URI  = LV2_WORKER__schedule;
    host_worker.feature.data = &host_worker.schedule;

    features.data_access.data_access = data_access_cb;
    features.data_access_feature.URI  = LV2_DATA_ACCESS_URI;
    features.data_access_feature.data = &features.data_access;

    feat_[0] = &features.um_f;
    feat_[1] = &features.unm_f;
    feat_[2] = &features.map_path_feature;
    feat_[3] = &features.make_path_feature;
    feat_[4] = &features.free_path_feature;
    feat_[5] = &host_worker.feature;
    feat_[6] = nullptr;
}


/****************************************************************
        LILV - init world and check if plugin is supported

****************************************************************/

bool LV2Host::feature_is_supported(const char* uri, const LV2_Feature*const* f) {
    for (; *f; ++f)
        if (!strcmp(uri, (*f)->URI)) return true;
    return false;
}

bool LV2Host::check_resize_port_requirements(const LilvPlugin* plugin) {
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

bool LV2Host::checkFeatures(const LilvPlugin* plugin, const LV2_Feature*const* feat) {
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

bool LV2Host::init_lilv() {

    plugin = lilv_plugins_get_by_uri(plugs, lilv_new_uri(world, plugin_uri.c_str()));
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

void LV2Host::freeNodes() {
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

void LV2Host::process_wrapper(uint32_t nframes,void* userdata) {
    auto* self = static_cast<LV2Host*>(userdata);
    self->process(nframes);
}

bool LV2Host::init_engine() {
    if (!engine->open(plugin_name)) return false;

    max_block_length = engine->buffer_size();
    engine->set_process_callback(&LV2Host::process_wrapper,this);
    return true;
}

/****************************************************************
                PORTS - init plugin ports

****************************************************************/

bool LV2Host::init_ports() {
    hports.init(world, plugin, engine.get(), required_atom_size, audio_class,
                control_class, atom_class, input_class, urids);

    return hports.init_ports(ports);
}

/****************************************************************
                DSP - init plugin instance

****************************************************************/

bool LV2Host::init_instance() {

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
        ctx->register_worker(&host_worker);
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

int LV2Host::process(uint32_t nframes) {
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
    if (host_worker.iface ) host_worker.deliver_worker_responses();
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
                dump_atom_event(ev, "DSP => UI", true);
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

void LV2Host::ui_write(LV2UI_Controller c, uint32_t port,
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

uint32_t LV2Host::ui_port_map(LV2UI_Feature_Handle h, const char* uri) {
    auto* self = static_cast<LV2Host*>(h);
    for (auto& p : self->ports)
        if (p.uri == uri) return p.index;
    return LV2UI_INVALID_PORT_INDEX;
}

int LV2Host::ui_resize(LV2UI_Feature_Handle h, int w, int hgt) {
    auto* self = static_cast<LV2Host*>(h);
    self->backend->resize(w, hgt);
    return 0;
}

void LV2Host::load_defaults() {
    for (auto& p : ports)
        if (p.is_control && p.is_input) {
            p.control = p.defvalue;
        }
}

void LV2Host::send_initial_ui_values() {
    if (!ui_handle) return;
    for (auto& p : ports)
        if (p.is_control && p.is_input) {
            p.control = p.defvalue;
            ui_desc->port_event(
                ui_handle, p.index, sizeof(float), 0, &p.defvalue);
        }
}

void LV2Host::send_control_values() {
    if (!ui_handle) return;
    for (auto& p : ports)
        if (p.is_control && p.is_input) {
            ui_desc->port_event(
                ui_handle, p.index, sizeof(float), 0, &p.control);
        }
}

void LV2Host::send_control_outputs() {
    if (!ui_handle) return;
    for (auto& p : ports)
        if (p.is_control && !p.is_input) {
            ui_desc->port_event(
                ui_handle, p.index, sizeof(float), 0, &p.control);
        }
}

void LV2Host::destroy_ui() {
    if (!ui_handle) return;
    if (ui_desc && ui_handle) {
        ui_desc->cleanup(ui_handle);
        ui_handle = nullptr;
    }
}

unsigned int LV2Host::host_ui_supported(const char* ui_type, const char* host_type) {
    if (!ui_type || !host_type) return 0;
    return strcmp(ui_type, host_type) == 0;
}

bool LV2Host::is_bridge_ui(const LilvUI* ui) {
    if (!ui) return false;
    char* bundle = lilv_node_get_path(lilv_ui_get_bundle_uri(ui), nullptr);
    bool is_bridge = false;
    if (bundle) {
        if (strstr(bundle, "lv2-gtk2-ui-bridge") || strstr(bundle, "lv2-gtk-ui-bridge")) {
            is_bridge = true;
        }
        free(bundle);
    }
    return is_bridge;
}

void LV2Host::select_backend_for_plugin() {
    //backend.reset();
    const LilvUIs* uis = lilv_plugin_get_uis(plugin);
    for (auto& b : available_backends) {
        if (!b->lv2_ui_uri()) continue;
        if (!uis) continue;
        LilvNode* backend_uri = lilv_new_uri(world, b->lv2_ui_uri());
        LILV_FOREACH(uis, i, uis) {
            const LilvUI* cand = lilv_uis_get(uis, i);
            if (is_bridge_ui(cand)) continue;
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

const void* LV2Host::data_access_cb(const char* uri) {
    extern LV2Host* current_host;
    if (!current_host || !current_host->instance) return nullptr;
    return lilv_instance_get_extension_data(
        current_host->instance, uri);
}

/****************************************************************
            UI -init the UI (Host and Plugin)

****************************************************************/

bool LV2Host::init_ui() {
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
        if (is_bridge_ui(cand)) continue;
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
        if (!backend->create_ui(640, 480)) {
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
        backend = available_backends.front();
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
        backend = available_backends.front();
        backend->attach_bridge(this);
        backend->set_close_callback([this]() {request_shutdown();});
        if (!ui_needs_control_update.load()) load_defaults();
        return true;
    }
    ui_desc = plugin_gui;
    // Create backend window
    if (!backend->create_ui(640, 480)) {
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

    LV2_Feature* feats[] = {&parent, &resize_f, &pm_f, &ui_options_feature, &features.um_f,
         &features.unm_f, &instance_access_feature, &features.data_access_feature, nullptr};
    current_host = this;
    ui_handle = ui_desc->instantiate(ui_desc, plugin_uri.c_str(), bundle, ui_write,
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
