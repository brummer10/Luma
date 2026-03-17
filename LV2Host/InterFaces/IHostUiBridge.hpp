
/*
 * IHostUiBridge.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

#pragma once
#include <cstdint>
#include <vector>
#include <string>

#include "LV2HostTypes.hpp"
#include "LV2HostPorts.hpp"

class IHostUiBridge {
public:
    virtual ~IHostUiBridge() = default;

    virtual void request_shutdown() = 0;

    virtual uint32_t control_port_count() const = 0;

    virtual void list_controls() const = 0;

    virtual const std::vector<Port>& get_ports() const = 0;
    virtual const std::vector<PortGroup>& get_groups() const = 0;

    virtual void patch_set(const std::string& uri, float value) = 0;
    virtual void patch_set(const std::string& uri, int value) = 0;
    virtual void patch_set(const std::string& uri, bool value) = 0;
    virtual void patch_set(const std::string& uri, const char* value) = 0;
    virtual void patch_set(const std::string& uri, std::string value) = 0;

    virtual void patch_get(const std::string& uri) = 0;

    virtual const std::vector<EnumPair>& get_enum_pair(uint32_t index) const = 0;

    virtual float get_control(uint32_t port) const = 0;

    virtual uint32_t get_control_port_index(uint32_t index) const = 0;

    virtual float get_control_min(uint32_t port) const = 0;

    virtual float get_control_max(uint32_t port) const = 0;

    virtual void set_control(uint32_t port, float value) = 0;

    virtual uint32_t meter_count() const = 0;

    virtual uint32_t get_meter_port_index(uint32_t index) const = 0;

    virtual float get_meter(uint32_t meter) const = 0;

    virtual const char* meter_name(uint32_t meter) const = 0;

    virtual void send_atom_to_plugin(
        uint32_t port,
        uint32_t size,
        uint32_t type,
        const void* data) = 0;

    virtual const char* port_name(uint32_t port) const = 0;

    virtual bool port_is_input(uint32_t port) const = 0;

    virtual bool port_is_control(uint32_t port) const = 0;

    virtual void set_resource(void* res) = 0;

    virtual void* get_resource() const = 0;
    
    virtual const std::vector<InfoPair> get_presets() = 0;

    virtual void applyPreset(const std::string& uri, const std::string& label) = 0;

    virtual void restoreDefaults() = 0;

    virtual const std::string& getPluginName() const = 0;
    
    virtual bool savePresetBundle(const std::string& preset_name)  = 0;
};
