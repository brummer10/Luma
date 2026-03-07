
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
    bool init_ports(std::vector<Port>& ports) {
        if (!plugin || !engine || !world) return false;

        ports.clear();
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

    //std::vector<Port>& get_ports() { return ports; }

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
