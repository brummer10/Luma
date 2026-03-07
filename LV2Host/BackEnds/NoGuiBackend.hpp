
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

#include <termios.h>
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

    bool create_ui(int, int) override { return true; }
    void close_window() override {}
    void embed_native(void*) override {}
    void resize(int, int) override {}
    void finalize_window(const char*) override {}
    void set_preset_name(const std::string) override {};
    void* native_window() override { return nullptr; }

    void set_close_callback(std::function<void()> cb) override {
        close_cb = std::move(cb);
    }

    void attach_bridge(IHostUiBridge* b) override {
        bridge = b;
        begin_region_from_cursor();
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


    bool get_cursor_position(int& row, int& col) {
        termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        std::cout << "\033[6n" << std::flush;
        char buf[32] = {0};
        if (read(STDIN_FILENO, buf, sizeof(buf) - 1) <= 0) return false;
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (sscanf(buf, "\033[%d;%dR", &row, &col) != 2) return false;
        return true;
    }

    int row = 0;
    void begin_region_from_cursor() {
        int col;
        if (!get_cursor_position(row, col)) return;
        std::cout << "\033[" << row << ";999r";
        std::cout << "\033[" << row << ";1H";
    }

    void end_region() {
        std::cout << "\033[r";
        std::cout << "\033[" << row << ";1H";
        std::cout << "\033[J";
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
        "  quit              - exit\n\n";
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
                << " min " << bridge->get_control_min(i)
                << " / max " << bridge->get_control_max(i)
                << "\n";
        }
    }

    void handle_command(const std::string& line) {
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
                if (bridge->port_is_control(idx) &&
                        bridge->port_is_input(idx)) {
                    bridge->set_control(idx, value);
                    std::cout << bridge->port_name(idx)
                        << " = " << value << "\n";
                }
            }
        }
        else if (cmd == "quit") {
            end_region();
            if (close_cb) {
                close_cb();
            }
        }
        else if (!cmd.empty()) {
            print_help();
        }
    }
};
