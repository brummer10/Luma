
/*
 * LV2HostState.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once

#include <lilv/lilv.h>
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>

#include <string>
#include <vector>
#include <atomic>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdio>

#include "LV2HostPorts.hpp"

/****************************************************************
    LV2HostState.hpp - load and save LV2 presets and default state
                    

****************************************************************/

class LV2HostState {
protected:
    LilvWorld* world = nullptr;
    const LilvPlugin* plugin = nullptr;
    LilvInstance* instance = nullptr;
    LV2_URID_Map um {};
    LV2_URID_Unmap unm {};
    std::vector<Port>* ports = nullptr;
    std::string plugin_name;
    std::atomic<bool>* ui_needs_control_update = nullptr;
    std::atomic<bool>* ui_needs_initial_update = nullptr;
    LilvState* default_state = nullptr;

public:
    LV2HostState() = default;

    virtual ~LV2HostState() {
        if (default_state)
            lilv_state_free(default_state);
    }

    void init_state(LilvWorld* w, const LilvPlugin* p, LilvInstance* inst, LV2_URID_Map map,
                    LV2_URID_Unmap unmap, std::vector<Port>* port_list, const std::string& name,
                    std::atomic<bool>* ctrl_update, std::atomic<bool>* init_update) {
        world = w;
        plugin = p;
        instance = inst;
        um = map;
        unm = unmap;
        ports = port_list;
        plugin_name = name;
        ui_needs_control_update = ctrl_update;
        ui_needs_initial_update = init_update;
    }

private:
    static void set_port_value(const char* port_symbol, void* user_data,
                               const void* value, uint32_t size, uint32_t) {

        auto* self = static_cast<LV2HostState*>(user_data);

        for (auto& p : *self->ports) {
            if (!p.is_control) continue;

            if (strcmp(p.symbol, port_symbol) == 0) {
                if (size == sizeof(float))
                    p.control = *(const float*)value;
                break;
            }
        }
    }

    static const void* get_port_value(const char* port_symbol, void* user_data,
                                            uint32_t* size, uint32_t* type) {

        auto* self = static_cast<LV2HostState*>(user_data);
        for (auto& p : *self->ports) {
            if (!p.is_control) continue;
            if (strcmp(p.symbol, port_symbol) == 0) {
                *size = sizeof(float);
                *type = self->um.map(self->um.handle, LV2_ATOM__Float);
                return &p.control;
            }
        }
        return nullptr;
    }

    static std::string sanitize_filename(const std::string& name) {
        std::string out;
        for (char c : name) {
            if (std::isalnum((unsigned char)c))
                out += c;
            else if (c == ' ')
                out += '_';
        }
        if (out.empty())
            out = "Preset";
        return out;
    }

    static std::string ensure_lv2_suffix(const std::string& s) {
        if (s.size() >= 4) {
            std::string tail = s.substr(s.size() - 4);
            std::transform(tail.begin(), tail.end(), tail.begin(), ::tolower);
            if (tail == ".lv2")
                return s;
        }
        return s + ".lv2";
    }

public:

/****************************************************************
                        LOAD PRESET
****************************************************************/

    void apply_preset(const std::string& preset_uri, const LV2_Feature* feat[7]) {

        LilvNode* preset = lilv_new_uri(world, preset_uri.c_str());
        if (!preset) {
            fprintf(stderr, "Invalid preset URI\n");
            ui_needs_initial_update->store(true);
            return;
        }

        LilvState* state = lilv_state_new_from_world(world, &um, preset);
        if (!state) {
            char* path = lilv_file_uri_parse(preset_uri.c_str(), nullptr);
            if (!path) {
                fprintf(stderr, "Preset not found\n");
                lilv_node_free(preset);
                ui_needs_initial_update->store(true);
                return;
            }

            state = lilv_state_new_from_file(world, &um, nullptr, path);
            free(path);
            if (!state) {
                fprintf(stderr, "Failed to load preset\n");
                lilv_node_free(preset);
                ui_needs_initial_update->store(true);
                return;
            }
        }

        lilv_state_restore(state, instance, set_port_value, this, 0, feat);
        lilv_state_free(state);
        lilv_node_free(preset);
        ui_needs_control_update->store(true);
        ui_needs_initial_update->store(false);
    }

/****************************************************************
                    SAVE PRESET
****************************************************************/

    bool save_preset_bundle(const std::string& preset_name, const LV2_Feature* feat[7]) {
        const char* home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "HOME not set\n");
            return false;
        }

        std::string safe = sanitize_filename(preset_name);
        std::string base_dir = std::string(home) + "/.lv2";
        std::filesystem::create_directories(base_dir);
        std::string bundle_dir = base_dir + "/" + plugin_name + "_" + ensure_lv2_suffix(safe);

        if (std::filesystem::exists(bundle_dir))
            std::filesystem::remove_all(bundle_dir);

        std::filesystem::create_directories(bundle_dir);
        std::string abs_bundle = std::filesystem::absolute(bundle_dir).string();
        std::string preset_uri = "file://" + abs_bundle + "#preset";
        std::string preset_file = preset_name + ".ttl";

        LilvState* state = lilv_state_new_from_instance(plugin, instance, &um,
                abs_bundle.c_str(), abs_bundle.c_str(), abs_bundle.c_str(),
                abs_bundle.c_str(), get_port_value, this, 0, feat);

        if (!state) {
            fprintf(stderr, "Failed to create state\n");
            return false;
        }

        lilv_state_set_label(state, preset_name.c_str());

        int ok = lilv_state_save(world, &um, &unm, state, preset_uri.c_str(),
                                    abs_bundle.c_str(), preset_file.c_str());

        lilv_state_free(state);

        if (ok != 0) {
            fprintf(stderr, "Failed to save preset\n");
            return false;
        }

        LilvNode* bundle_uri = lilv_new_file_uri(world, nullptr, abs_bundle.c_str());
        lilv_world_load_bundle(world, bundle_uri);
        lilv_node_free(bundle_uri);

        return true;
    }

/****************************************************************
                DEFAULT STATE save/load
****************************************************************/

    void store_default(const LV2_Feature* feat[7]) {
        if (default_state) lilv_state_free(default_state);
        default_state = lilv_state_new_from_instance(plugin, instance, &um,
                nullptr, nullptr, nullptr, nullptr, get_port_value, this, 0, feat);
    }

    void restore_default(const LV2_Feature* feat[7]) {
        if (!default_state) return;
        lilv_state_restore(default_state, instance, set_port_value, this, 0, feat);
    }
};
