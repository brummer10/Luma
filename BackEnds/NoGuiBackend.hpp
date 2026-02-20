
/*
 * NoGuiBackend.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once

#include "IUiBackend.hpp"
#include "IHostUiBridge.hpp"

#include <iostream>
#include <sstream>
#include <sys/select.h>
#include <unistd.h>

/****************************************************************
    NoGuiBackend.hpp - this is the No UI backend for Luma LV2 host

****************************************************************/
class NoGuiBackend : public IUiBackend {
public:
    const char* lv2_ui_uri() const override {
        return nullptr;
    }

    bool create_window(int, int) override { return true; }
    void embed_native(void*) override {}
    void resize(int, int) override {}
    void finalize_window(const char*) override {}
    void* native_window() override { return nullptr; }

    void set_close_callback(std::function<void()> cb) override {
        close_cb = std::move(cb);
    }

    void attach_bridge(IHostUiBridge* b) override {
        bridge = b;
        print_help();
    }

    void poll_events() override {
        if (!stdin_ready())
            return;

        std::string line;
        std::getline(std::cin, line);
        handle_command(line);
    }

private:
    IHostUiBridge* bridge = nullptr;
    std::function<void()> close_cb;

    int last_drawn_lines = 0;

    void clear_output() {
        if (last_drawn_lines <= 0) return;
        // move cursor up
        std::cout << "\033[" << last_drawn_lines << "A";
        // clear to end of screen
        std::cout << "\033[J";
        last_drawn_lines = 0;
    }

    bool stdin_ready() {
        fd_set set;
        struct timeval tv{0, 0};

        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);

        return select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv) > 0;
    }

    void print_help() {
        std::cout <<
        "\nNoGui Console Commands:\n"
        "  list              - show control ports\n"
        "  set <i> <value>   - set control\n"
        "  ctrl+c              - exit\n\n";
    }

    void list_ports() {
        if (!bridge) return;
        for (size_t i = 0; i < bridge->control_port_count(); ++i) {
            if (!bridge->port_is_control(i) ||
                !bridge->port_is_input(i))
                continue;

            std::cout
                << i << ": "
                << bridge->port_name(i)
                << " = "
                << bridge->get_control(i)
                << "\n";
            last_drawn_lines++;
        }
    }

    void handle_command(const std::string& line) {
        clear_output();
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "list") {
            list_ports();
        }
        else if (cmd == "set") {
            size_t idx;
            float value;

            if (iss >> idx >> value && bridge) {
                bridge->set_control(idx, value);
                std::cout
                    << bridge->port_name(idx)
                    << " = " << value << "\n";
            }
        }
        else if (cmd == "quit") {
            if (close_cb) {
                close_cb();
                ::close(STDIN_FILENO);
            }
        }
        else if (!cmd.empty()) {
            print_help();
        }
    }
};
