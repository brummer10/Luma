
/*
 * LV2HostPorts.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once

#include <lilv/lilv.h>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <atomic>

#include "URIDs.h"
#include "IDspEngine.hpp"
#include "lv2_ringbuffer.h"

/****************************************************************
                        PORT DATA

****************************************************************/

enum DisplayType {
    tp_scale, tp_scale_log, tp_toggle, tp_enum, tp_display, tp_trigger, tp_none, tp_int, tp_enabled, tp_atom,
};

enum display_flags { dtp_normal, dtp_no_gui, dtp_log = 1 };

struct AtomState {
    std::vector<uint8_t> ui_to_dsp;
    uint32_t ui_to_dsp_type = 0;
    std::atomic<bool> ui_to_dsp_pending{false};
    lv2_ringbuffer_t* dsp_to_ui = nullptr;

    AtomState(size_t sz = 16384) {
        dsp_to_ui = lv2_ringbuffer_create(sz);
    }

    ~AtomState() {
        if (dsp_to_ui) lv2_ringbuffer_free(dsp_to_ui);
    }
};

struct Port {
    uint32_t index = 0;
    bool is_audio = false;
    bool is_input = false;
    bool is_control = false;
    bool is_atom = false;
    bool is_midi = false;
    bool is_patch = false; 

    float control = 0.0f;
    float defvalue = 0.0f;
    float fmin = 0.0f;
    float fmax = 0.0f;

    std::unique_ptr<IDspPort> engine_port;

    LV2_Atom_Sequence* atom = nullptr;
    uint32_t atom_buf_size = 8192;
    std::vector<EnumPair> enumdict;
    AtomState* atom_state = nullptr;
    display_flags df = dtp_normal;
    DisplayType dt = tp_scale;

    std::string uri;
    const char* symbol = nullptr;
    const char* name = nullptr;

    std::string group_uri;
    std::string group_name;
    uint32_t group_index = UINT32_MAX;

    std::string role;
};

struct PortGroup {
    std::string uri;
    std::string name;
    std::vector<uint32_t> ports;
};

/****************************************************************
        LV2HostPorts - fetch the plugin port data

****************************************************************/

class LV2HostPorts {
public:
    LV2HostPorts() = default;

    // set all required host member before init_ports() 
    void init(LilvWorld* world, const LilvPlugin* plugin, IDspEngine* engine,
              uint32_t required_atom_size, const LilvNode* audio_class,
              const LilvNode* control_class, const LilvNode* atom_class,
              const LilvNode* input_class, URIDs urids) {

        this->world = world;
        this->plugin = plugin;
        this->engine = engine;
        this->required_atom_size = required_atom_size;
        this->audio_class = audio_class;
        this->control_class = control_class;
        this->atom_class = atom_class;
        this->input_class = input_class;
        this->urids = urids;
    }

    // fetch port data and return them in std::vector<Port> ports
    bool init_ports(std::vector<Port>& ports, std::vector<PortGroup>& groups) {
        if (!plugin || !engine || !world) return false;

        ports.clear();
        groups.clear();
        uint32_t n = lilv_plugin_get_num_ports(plugin);
        ports.reserve(n);

        LilvNode* midi_event = lilv_new_uri(world, LV2_MIDI__MidiEvent);
        LilvNode* notOnGui = lilv_new_uri(world, LV2_PORT_PROPS__notOnGUI);

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
                p.uri = std::string(lilv_node_as_uri(lilv_plugin_get_uri(plugin))) +
                        "#" + lilv_node_as_string(sym);
                p.symbol = lilv_node_as_string(sym);
            }

            if (p.is_audio) {
                p.engine_port = engine->create_audio_port(p.symbol ? p.symbol : "audio",
                                                          p.is_input ? true : false);
            }

            if (p.is_atom && p.is_midi) {
                p.engine_port = engine->create_midi_port(p.symbol ? p.symbol : "midi",
                                                         p.is_input ? true : false);
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

            if (p.is_control) {
                LilvNode *pdflt, *pmin, *pmax, *nm;
                nm = lilv_port_get_name(plugin, lp);
                p.name = lilv_node_as_string(nm);
                lilv_node_free(nm);
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

                LilvNode* is_int = lilv_new_uri(world, LV2_CORE__integer);
                if (lilv_port_has_property(plugin, lp, is_int)) {
                    p.dt = tp_int;
                }
                lilv_node_free(is_int);
                LilvNode* is_tog = lilv_new_uri(world, LV2_CORE__toggled);
                if (lilv_port_has_property(plugin, lp, is_tog)) {
                    p.dt = tp_toggle;
                }
                lilv_node_free(is_tog);
                LilvScalePoints* sp = lilv_port_get_scale_points(plugin, lp);
                int num_sp = lilv_scale_points_size(sp);
                if (num_sp > 0) {
                    for (LilvIter* it = lilv_scale_points_begin(sp);
                            !lilv_scale_points_is_end(sp, it);
                            it = lilv_scale_points_next(sp, it)) {
                        const LilvScalePoint* ps = lilv_scale_points_get(sp, it);
                        p.enumdict.push_back(
                            {lilv_node_as_float( lilv_scale_point_get_value(ps)),
                            lilv_node_as_string(lilv_scale_point_get_label(ps))});
                    }
                    p.dt = tp_enum;
                    std::sort(p.enumdict.begin(), p.enumdict.end(),
                        [](const EnumPair& a, const EnumPair& b) {
                            return a.val < b.val;
                        });
                    
                }
                lilv_scale_points_free(sp);
                LilvNode* is_trigger = lilv_new_uri(world, LV2_PORT_PROPS__trigger);
                if (lilv_port_has_property(plugin, lp, is_trigger)) {
                    p.dt = tp_trigger;
                }
                 if (lilv_port_has_property(plugin, lp, notOnGui)) {
                    p.df = dtp_no_gui;
                 }
                lilv_node_free(is_trigger);
            }
            ports.emplace_back(std::move(p));
        }

        LilvNode* patch_writable = lilv_new_uri(world, LV2_PATCH__writable);
        LilvNode* patch_readable = lilv_new_uri(world, LV2_PATCH__readable);

        LilvNode* is_min = lilv_new_uri(world, LV2_CORE__minimum);
        LilvNode* is_max = lilv_new_uri(world, LV2_CORE__maximum);
        LilvNode* is_def = lilv_new_uri(world, LV2_CORE__default);
        
        LilvNode* is_label = lilv_new_uri(world, LILV_NS_RDFS "label");
        LilvNode* is_range = lilv_new_uri(world, LILV_NS_RDFS "range");
        LilvNode* is_float = lilv_new_uri(world, LV2_ATOM__Float);
        LilvNode* is_path = lilv_new_uri(world, LV2_ATOM__Path);
        LilvNode* is_bool = lilv_new_uri(world, LV2_ATOM__Bool);
        LilvNode* is_a_int = lilv_new_uri(world, LV2_ATOM__Int);

        LilvNodes* writables = lilv_world_find_nodes(world, lilv_plugin_get_uri(plugin),
                                                                patch_writable, NULL);

        LilvNodes* readables = lilv_world_find_nodes(world, lilv_plugin_get_uri(plugin),
                                                                patch_readable, NULL);

        LILV_FOREACH(nodes, r, readables) {
            const LilvNode* prop = lilv_nodes_get(readables, r);
            Port p;
            p.index = ports.size();
            p.is_atom = false;
            p.is_patch = true;
            p.is_input = false;
            p.uri = lilv_node_as_uri(prop);

            const LilvNode* label = lilv_world_get(world, prop, is_label, NULL);
            if (label)  p.name = lilv_node_as_string(label);
            const LilvNode* min = lilv_world_get(world, prop, is_min, NULL);
            const LilvNode* max = lilv_world_get(world, prop, is_max, NULL);
            const LilvNode* def = lilv_world_get(world, prop, is_def, NULL);

            if (min) p.fmin = lilv_node_as_float(min);
            if (max) p.fmax = lilv_node_as_float(max);
            if (def) p.defvalue = lilv_node_as_float(def);

            p.atom_state = new AtomState;

            ports.emplace_back(std::move(p));
        }

        LILV_FOREACH(nodes, w, writables) {
            const LilvNode* prop = lilv_nodes_get(writables, w);
            Port p;
            p.index = ports.size();
            p.is_atom = false;
            p.is_patch = true;
            p.is_input = true;
            p.uri = lilv_node_as_uri(prop);

            const LilvNode* label = lilv_world_get(world, prop, is_label, NULL);
            if (label) p.name = lilv_node_as_string(label);
            const LilvNode* range = lilv_world_get(world, prop, is_range, NULL);

            if (range) {
                if (lilv_node_equals(range, is_float))
                    p.dt = tp_scale;
                else if (lilv_node_equals(range, is_a_int))
                    p.dt = tp_int;
                else if (lilv_node_equals(range, is_bool))
                    p.dt = tp_toggle;
                else if (lilv_node_equals(range, is_path))
                    p.dt = tp_atom;
            }

            const LilvNode* min = lilv_world_get(world, prop, is_min, NULL);
            const LilvNode* max = lilv_world_get(world, prop, is_max, NULL);
            const LilvNode* def = lilv_world_get(world, prop, is_def, NULL);

            if (min) p.fmin = lilv_node_as_float(min);
            if (max) p.fmax = lilv_node_as_float(max);
            if (def) p.defvalue = lilv_node_as_float(def);

            p.atom_state = new AtomState;

            ports.emplace_back(std::move(p));
        }

        lilv_nodes_free(writables);
        lilv_nodes_free(readables);
        lilv_node_free(patch_writable);
        lilv_node_free(patch_readable);

        lilv_node_free(is_min);
        lilv_node_free(is_max);
        lilv_node_free(is_def);

        lilv_node_free(is_label);
        lilv_node_free(is_range);
        lilv_node_free(is_float);
        lilv_node_free(is_path);
        lilv_node_free(is_bool);
        lilv_node_free(is_a_int);

        lilv_node_free(midi_event);
        lilv_node_free(notOnGui);

        // port groups unused for now
/*
        LilvNode* pg_group = lilv_new_uri(world, "http://lv2plug.in/ns/ext/port-groups#group");
        LilvNode* pg_name  = lilv_new_uri(world, "http://lv2plug.in/ns/ext/port-groups#name");
        LilvNode* pg_role  = lilv_new_uri(world, "http://lv2plug.in/ns/ext/port-groups#role");
        PortGroup g;
        g.uri = "urn:default";;
        g.name = "Default";
        groups.push_back(std::move(g));
        for (uint32_t i = 0; i < ports.size(); ++i) {
            const LilvPort* lp = nullptr;
            const LilvNode* group = nullptr;
            if (ports[i].is_patch) {
                LilvNode* prop = lilv_new_uri(world, ports[i].uri.c_str());
                group = lilv_world_get(world, prop, pg_group, NULL);
                lilv_node_free(prop);
            } else {
                lp = lilv_plugin_get_port_by_index(plugin, ports[i].index);
                group = lilv_port_get(plugin, lp, pg_group);
            }
            if (group) {
                std::string guri = lilv_node_as_uri(group);
                ports[i].group_uri = guri;
                uint32_t gid;
                auto it = group_map.find(guri);
                if (it == group_map.end()) {
                    gid = groups.size();
                    group_map[guri] = gid;
                    PortGroup g;
                    g.uri = guri;
                    const LilvNode* name = lilv_world_get(world, group, pg_name, NULL);
                    if (name) g.name = lilv_node_as_string(name);
                    groups.push_back(std::move(g));
                } else {
                    gid = it->second;
                }
                ports[i].group_index = gid;
                groups[gid].ports.push_back(i);

               // const LilvNode* role = lilv_port_get(plugin, lp, pg_role);
               // if (role)
               //     ports[i].role = lilv_node_as_uri(role);
            } else {
                ports[i].group_uri = "urn:default";
                ports[i].group_index = 0;
                groups[0].ports.push_back(i);
            }
        }
        lilv_node_free(pg_group);
        lilv_node_free(pg_name);
        lilv_node_free(pg_role);

        std::sort(groups.begin(), groups.end(),
            [](const PortGroup& a, const PortGroup& b) {
                return a.name < b.name;
            });
*/
        return true;
    }

    //std::vector<Port>& get_ports() { return ports; }
    //std::vector<PortGroup> groups;
    std::unordered_map<std::string, uint32_t> group_map;
private:
    LilvWorld* world = nullptr;
    const LilvPlugin* plugin = nullptr;
    IDspEngine* engine = nullptr;
    uint32_t required_atom_size = 0;

    const LilvNode* audio_class = nullptr;
    const LilvNode* control_class = nullptr;
    const LilvNode* atom_class = nullptr;
    const LilvNode* input_class = nullptr;
    URIDs urids = {};

    //std::vector<Port> ports;
};
