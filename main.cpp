
/*
 * main.cpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

/****************************************************************
        main.cpp - a minimal CLI interface for Luma

****************************************************************/

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <limits>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <cstdlib>

#include "LV2JackX11Host.hpp"

enum InputKey {
    KEY_NONE,
    KEY_ENTER,
    KEY_UP,
    KEY_DOWN,
    KEY_BACKSPACE,
    KEY_QUIT,
    KEY_DIGIT
};

struct KeyResult {
    InputKey key = KEY_NONE;
    int digit = -1;
};

class RawTerminal {
    termios old{};
public:
    RawTerminal() {
        tcgetattr(STDIN_FILENO, &old);
        termios raw = old;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }
    ~RawTerminal() {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    }
};

KeyResult read_key() {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return {};
    if (c == '\n') return { KEY_ENTER, -1 };
    if (c == 127 || c == '\b') return { KEY_BACKSPACE, -1 };
    if (c == 'q' || c == 'Q') return { KEY_QUIT, -1 };
    if (isdigit(c)) return { KEY_DIGIT, c - '0' };

    if (c == '\033') {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return {};
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return {};

        if (seq[0] == '[') {
            if (seq[1] == 'A') return { KEY_UP, -1 };
            if (seq[1] == 'B') return { KEY_DOWN, -1 };
        }
    }

    return {};
}

int get_terminal_width() {
    struct winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != -1 && w.ws_col > 0)
        return w.ws_col;

    const char* cols = std::getenv("COLUMNS");
    if (cols) return std::atoi(cols);

    return 80;
}

std::string ellipsize_middle(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    if (max_len <= 3) return s.substr(0, max_len);

    size_t keep = max_len - 3;
    size_t left = keep / 2;
    size_t right = keep - left;

    return s.substr(0, left) + "..." +
           s.substr(s.size() - right);
}

static int last_drawn_lines = 0;

static void clear_previous_output() {
    if (last_drawn_lines <= 0) return;
    // move cursor up
    std::cout << "\033[" << last_drawn_lines << "A";
    // clear to end of screen
    std::cout << "\033[J";
}

static void blue() {
    std::cout << "\033[1;34m";
}

static void red() {
    std::cout << "\033[1;31m";
}

static void clear_color() {
    std::cout << "\033[0m";
}

// returns selected index, or -1 if none selected
int pager_print(const std::vector<LV2X11JackHost::InfoPair>& matches, bool allow_default) {

    if (matches.empty()) return -1;

    const int term_width = get_terminal_width();
    const int max_col_width = 30;
    // compute longest label (clamped)
    size_t longest = 0;
    for (auto& m : matches)
        longest = std::max(longest,
            ellipsize_middle(m.label, max_col_width).size());

    const int col_width = std::min<int>(longest + 8, max_col_width + 8);
    int cols = std::max(1, term_width / col_width);
    const int rows = 10;
    const int per_page = rows * cols;
    size_t index = 0;
    RawTerminal raw;
    std::string number_buffer;

    while (true) {

        clear_previous_output();
        size_t end = std::min(index + per_page, matches.size());
        size_t count = end - index;
        size_t r = (count + cols - 1) / cols;
        int drawn = 0;

        for (size_t row = 0; row < r; ++row) {
            for (int col = 0; col < cols; ++col) {
                size_t i = index + row + col * r;
                if (i >= end) continue;
                std::string label = ellipsize_middle(matches[i].label, max_col_width);
                std::ostringstream cell;
                cell << "[" << i << "] " << label;
                std::cout << std::left << std::setw(col_width) << cell.str();
            }
            std::cout << "\n";
            drawn++;
        }

        std::cout << "\n↑/↓ scroll | Number select | ENTER confirm | q quit\n";

        if (!number_buffer.empty())
            std::cout << "Selection: " << number_buffer << "\n";
        else
            std::cout << "Selection: _\n";

        drawn += 3;
        last_drawn_lines = drawn;
        KeyResult k = read_key();

        if (k.key == KEY_QUIT) {
            clear_previous_output();
            return -2;
        }

        if (k.key == KEY_DIGIT) {
            number_buffer += char('0' + k.digit);
            continue;
        }

        if (k.key == KEY_BACKSPACE && !number_buffer.empty()) {
            number_buffer.pop_back();
            continue;
        }

        if (k.key == KEY_ENTER) {
            // number typed → confirm selection
            if (!number_buffer.empty()) {
                int n = std::stoi(number_buffer);
                if (n >= 0 && n < (int)matches.size())
                    return n;
                number_buffer.clear();
                continue;
            }

            // no number typed
            // first page → default selection
            if (allow_default && index == 0) return -1;
            // otherwise scroll forward
            if (index + per_page < matches.size()) index += per_page;
        }

        if (k.key == KEY_DOWN) {
            if (index + per_page < matches.size()) index += per_page;
        }

        if (k.key == KEY_UP) {
            if (index >= (size_t)per_page) index -= per_page;
        }
    }
    return -1;
}

int print_centered(const std::vector<std::string>& lines, int term_width) {
    size_t max_len = 0;
    for (const auto& l : lines)
        max_len = std::max(max_len, l.size());

    int pad = (term_width - (int)max_len) / 2;
    if (pad < 0) pad = 0;

    for (const auto& line : lines) {
        std::cout << std::string(pad, ' ') << line << "\n";
    }
    return pad;
}

int main(int argc, char *argv[]) {

    if (0 == XInitThreads())
        std::cerr << "Warning: XInitThreads() failed\n";

    std::string uri = (argc > 1) ? argv[1] : "";
    std::string preset_uri;
    std::string preset_label;

    LV2X11JackHost host;
    host.init_world();
    auto matches = host.find_plugin_matches(uri);

    if (matches.empty()) {
        red();
        std::cerr << "No plugin found\n"; 
        clear_color();
        host.closeHost();
        return 1;
    }
    std::string fm =  "Fond " + std::to_string(matches.size()) + " Plugins:";
    std::vector<std::string> logo = {
        " ╦  ╦ ╦ ╔╦╗ ╔═╗ ",
        " ║  ║ ║ ║║║ ╠═╣ ",
        " ╩═╝╚═╝═╩╝╚═╝ ╩ ",
        fm
    };
    int tw = get_terminal_width();
    blue();
    int pad = print_centered(logo, tw);
    clear_color();

    int pchoice = 0;
    if (matches.size() > 1) pchoice = pager_print(matches, false);

    if (pchoice >= 0) {
        clear_previous_output();
        last_drawn_lines = 0;
        std::string sel =  "Selected: " + matches[pchoice].label ;
        blue();
        std::cout << std::string(pad, ' ') << sel;
        clear_color();
        std::cout << "\n";
        uri = matches[pchoice].uri;
    } else {
        host.closeHost();
        return 0;
    }

    if (!host.init(uri.c_str())) return 1;

    auto presets = host.get_presets(uri.c_str());

    if (!presets.empty()) {
        int choice = -1;
        std::string pset = "Found " + std::to_string(presets.size()) + " presets:";
        blue();
        std::cout << std::string(pad, ' ') << pset;
        clear_color();
        std::cout << "\n";
        last_drawn_lines = 0;
        choice = pager_print(presets, true);
        if (choice >= 0 && choice < static_cast<int>(presets.size())) {
            clear_previous_output();
            preset_uri = presets[choice].uri;
            preset_label = presets[choice].label;
            std::string prset =  "Loading preset: " + presets[choice].label;
            blue();
            std::cout << std::string(pad, ' ') << prset << "\n";
            clear_color();
        } else if (choice == -2) {
            host.closeHost();
            return 0;
        }
    }

    if (!preset_uri.empty()) host.apply_preset(preset_uri, preset_label);
    if (!host.initUi()) return 1;

    host.run_ui_loop();

    return 0;
}
