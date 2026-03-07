
/*
 * LV2PluginRegistry.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <lilv/lilv.h>

#include <vector>
#include <string>
#include <algorithm>
#include <iostream>

#include "LV2HostTypes.hpp"

/****************************************************************
    LV2PluginRegistry - get all plugins or by regex
                        get available presets for plugin

****************************************************************/
class LV2PluginRegistry {
public:
    LV2PluginRegistry() = default;

    void init(LilvWorld* w, const LilvPlugins* p) {
        world   = w;
        plugins = p;
    }

    std::vector<InfoPair> find_plugin_matches(const std::string& input) const {

        std::vector<InfoPair> results;
        if (!world || !plugins) return results;
        std::string needle = to_lower(input);

        LILV_FOREACH(plugins, i, plugins) {
            const LilvPlugin* plug = lilv_plugins_get(plugins, i);
            std::string uri = lilv_node_as_uri(lilv_plugin_get_uri(plug));
            const LilvNode* name_node = lilv_plugin_get_name(plug);
            std::string name = name_node ? lilv_node_as_string(name_node) : "";
            std::string lname = to_lower(name);
            bool match = false;
            if (!input.empty()) {
                if (input == uri) match = true;
                if (input == name) match = true;
                if (needle == lname) match = true;
                if (lname.find(needle) != std::string::npos) match = true;
                if (uri.find(needle) != std::string::npos) match = true;
            } else {
                match = true;
            }
            if (match) results.push_back({uri, name});
        }

        std::sort(results.begin(), results.end(),
            [](const InfoPair& a, const InfoPair& b) {
                return a.label < b.label;
            });

        return results;
    }

    std::vector<InfoPair> get_presets(const char* pluginUri) const {

        std::vector<InfoPair> result;
        if (!world || !pluginUri) return result;
        LilvNode* uri = lilv_new_uri(world, pluginUri);
        const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, uri);

        if (!plugin) {
            lilv_node_free(uri);
            return result;
        }

        LilvNode* preset_class = lilv_new_uri(world, "http://lv2plug.in/ns/ext/presets#Preset");
        const LilvNodes* presets = lilv_plugin_get_related(plugin, preset_class);

        if (!presets || lilv_nodes_size(presets) == 0) {
            lilv_node_free(preset_class);
            lilv_node_free(uri);
            return result;
        }

        LilvNode* label_pred = lilv_new_uri(world, "http://www.w3.org/2000/01/rdf-schema#label");

        LILV_FOREACH(nodes, i, presets) {
            const LilvNode* preset = lilv_nodes_get(presets, i);
            // ensure preset data is loaded
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

private:
    LilvWorld* world = nullptr;
    const LilvPlugins* plugins = nullptr;

    static std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c){ return std::tolower(c); });
        return s;
    }
};
