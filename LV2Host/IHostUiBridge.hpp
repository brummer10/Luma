
#pragma once
#include <cstdint>
#include <vector>
#include <string>

class IHostUiBridge {
public:
    virtual ~IHostUiBridge() = default;

    // ----------------------------
    // Lifecycle
    // ----------------------------

    virtual void request_shutdown() = 0;

    // ----------------------------
    // Control Ports
    // ----------------------------

    virtual uint32_t control_port_count() const = 0;

    virtual void list_controls() const = 0;

    virtual float get_control(uint32_t port) const = 0;

    virtual void set_control(uint32_t port, float value) = 0;

    // ----------------------------
    // Atom Messaging
    // ----------------------------

    virtual void send_atom_to_plugin(
        uint32_t port,
        uint32_t size,
        uint32_t type,
        const void* data) = 0;

    // ----------------------------
    // Metadata (optional but useful)
    // ----------------------------

    virtual const char* port_name(uint32_t port) const = 0;

    virtual bool port_is_input(uint32_t port) const = 0;

    virtual bool port_is_control(uint32_t port) const = 0;
};
