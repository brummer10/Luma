
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

#include "LV2Host.hpp"
#include "JackEngine.hpp"
#ifndef NOGUI
#include "X11UiBackend.cpp"
#include "StubGuiBackend.cpp"
#if defined(HAVE_GTK2)
#include "GTK2UiBackend.cpp"
#endif
#endif

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
#include <atomic>
#include <sys/signalfd.h>
#include <csignal>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>
#include <cctype>

class MultiHost {
public:
    LV2Host* create_instance() {
        auto host = std::make_unique<LV2Host>();
        auto ptr = host.get();
        hosts.push_back(std::move(host));
        return ptr;
    }

    void shutdown_all() {
        for (auto& h : hosts)
            h->request_shutdown();
        hosts.clear();
    }

  //  ~MultiHost() {
   //     shutdown_all();
  //  }

private:
    std::vector<std::unique_ptr<LV2Host>> hosts;
};

MultiHost mh;

std::atomic<bool> shutdown{false};
bool run = true;
int signal_fd = -1;

struct AltScreenGuard {
    AltScreenGuard() {
        std::cout << "\033[?1049h" << "\033[H" << std::flush;
    }
    ~AltScreenGuard() {
        std::cout << "\033[?1049l" << std::flush;
    }
};

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
    char ch = 0;
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


KeyResult read_key()
{
    fd_set rfds;

    for (;;) {
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(signal_fd, &rfds);

        int maxfd = std::max(STDIN_FILENO, signal_fd) + 1;
        int r = select(maxfd, &rfds, nullptr, nullptr, nullptr);

        if (r < 0) {
            if (errno == EINTR) continue;
            perror("select");
            return {};
        }

        if (FD_ISSET(signal_fd, &rfds)) {
            struct signalfd_siginfo fdsi;
            ssize_t s = read(signal_fd, &fdsi, sizeof(fdsi));
            if (s == sizeof(fdsi)) {
                if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM ||
                        fdsi.ssi_signo == SIGHUP || fdsi.ssi_signo == SIGQUIT) {
                    shutdown.store(true, std::memory_order_release);
                    mh.shutdown_all();
                    return { KEY_QUIT, -1 };
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char c;
            if (read(STDIN_FILENO, &c, 1) != 1) return {};

            if (c == '\n') return { KEY_ENTER, -1 };
            if (c == 127 || c == '\b') return { KEY_BACKSPACE, -1 };
            if (c == 'q' || c == 'Q') return { KEY_QUIT, -1 };
            if (isdigit(c)) return { KEY_DIGIT, c - '0' };
            if (std::isprint(c)) return { KEY_NONE, -1, c };

            if (c == '\033') {
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) != 1) return {};
                if (read(STDIN_FILENO, &seq[1], 1) != 1) return {};

                if (seq[0] == '[') {
                    if (seq[1] == 'A') return { KEY_UP, -1 };
                    if (seq[1] == 'B') return { KEY_DOWN, -1 };
                }
            }
        }
    }
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
    last_drawn_lines = 0;
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

static std::vector<LV2Host::InfoPair> filter_matches(
    const std::vector<LV2Host::InfoPair>& all, const std::string& search) {

    if (search.empty()) return all;
    std::vector<LV2Host::InfoPair> result;
    std::string needle = search;
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    for (auto& m : all) {
        std::string hay = m.label;
        std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
        if (hay.find(needle) != std::string::npos)
            result.push_back(m);
    }
    return result;
}

// returns selected index, or -1 if none selected
int pager_print(const std::vector<LV2Host::InfoPair>& matches, bool allow_default) {

    if (matches.empty()) return -1;

    const int term_width = get_terminal_width();
    const int max_col_width = 30;
    RawTerminal raw;
    std::string number_buffer;
    int selected_index = -1;
    std::string search_buffer;
    size_t index = 0;

    while (true) {
        if (!number_buffer.empty()) {
            try {
                selected_index = std::stoi(number_buffer);
            } catch (...) {
                selected_index = -1;
            }
        } else {
            selected_index = -1;
        }
        auto filtered = filter_matches(matches, search_buffer);
        clear_previous_output();
        if (filtered.empty()) {
            red();
            std::cout << "No matches for: " << search_buffer << "\n";
            clear_color();
            last_drawn_lines = 1;
        }

        // --- layout calc ---
        size_t longest = 0;
        for (auto& m : filtered)
            longest = std::max(longest, ellipsize_middle(m.label, max_col_width).size());

        const int col_width = std::min<int>(longest + 8, max_col_width + 8);
        int cols = std::max(1, term_width / col_width);
        const int rows = 10;
        const int per_page = rows * cols;
        if (index >= filtered.size()) index = 0;
        size_t end = std::min(index + per_page, filtered.size());
        size_t count = end - index;
        size_t r = (count + cols - 1) / cols;
        int drawn = 0;
        // --- print grid ---
        for (size_t row = 0; row < r; ++row) {
            for (int col = 0; col < cols; ++col) {
                size_t i = index + row + col * r;
                if (i >= end) continue;
                std::string label = ellipsize_middle(filtered[i].label, max_col_width);
                std::ostringstream cell;
                cell << "[" << i << "] " << label;
                if ((int)i == selected_index) std::cout << "\033[7m";
                std::cout << std::left << std::setw(col_width) << cell.str();
                if ((int)i == selected_index) std::cout << "\033[0m";
            }
            std::cout << "\n";
            drawn++;
        }

        std::cout << "\n↑/↓ scroll | Number select | ENTER confirm | q quit\n";
        std::cout << "Search: /" << search_buffer << "\n";

        if (!number_buffer.empty())
            std::cout << "Selection: " << number_buffer << "\n";
        else
            std::cout << "Selection: _\n";

        drawn += 4;
        last_drawn_lines = drawn;

        KeyResult k = read_key();

        // ---- quit ----
        if (k.key == KEY_QUIT) {
            clear_previous_output();
            return -2;
        }

        // ---- search input ----
        if (k.ch != 0) {
            search_buffer += k.ch;
            number_buffer.clear();
            index = 0;
            continue;
        }

        if (k.key == KEY_BACKSPACE) {
            if (!search_buffer.empty()) {
                search_buffer.pop_back();
                index = 0;
                continue;
            }
            if (!number_buffer.empty()) {
                number_buffer.pop_back();
                continue;
            }
        }

        // ---- digit selection ----
        if (k.key == KEY_DIGIT) {
            number_buffer += char('0' + k.digit);
            continue;
        }

        // ---- confirm ----
        if (k.key == KEY_ENTER) {

            if (!number_buffer.empty()) {
                int n = std::stoi(number_buffer);
                if (n >= 0 && n < (int)filtered.size())
                {
                    // map back to original vector
                    auto& selected = filtered[n];
                    for (size_t i = 0; i < matches.size(); ++i)
                        if (matches[i].uri == selected.uri)
                            return i;
                }
                number_buffer.clear();
                continue;
            }

            if (allow_default && index == 0)
                return -1;

            if (index + per_page < filtered.size())
                index += per_page;

            continue;
        }

        // ---- scroll ----
        if (k.key == KEY_DOWN) {
            if (index + per_page < filtered.size())
                index += per_page;
        }

        if (k.key == KEY_UP) {
            if (index >= (size_t)per_page)
                index -= per_page;
        }
    }
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

    #ifndef NOGUI
    if (0 == XInitThreads())
        std::cerr << "Warning: XInitThreads() failed\n";
    #endif
    #ifndef DEBUG
    AltScreenGuard screen;
    #endif

    std::string uri = (argc > 1) ? argv[1] : "";
    std::string preset_uri;
    std::string preset_label;

    sigset_t mask;
    sigemptyset(&mask);

    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGQUIT);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
        perror("sigprocmask");
        return 1;
    }

    signal_fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (signal_fd == -1) {
        perror("signalfd");
        return 1;
    }

    std::vector<std::string> logo = {
        " ╦  ╦ ╦ ╔╦╗ ╔═╗ ",
        " ║  ║ ║ ║║║ ╠═╣ ",
        " ╩═╝╚═╝═╩╝╚═╝ ╩ "
    };
    int tw = get_terminal_width();
    blue();
    int pad = print_centered(logo, tw);
    clear_color();

while (run) {
    
    LV2Host* host = mh.create_instance();
    host->set_engine(std::make_unique<JackEngine>());
    #ifndef NOGUI
    host->register_ui_backend(std::make_shared<X11UiBackend>());
    host->register_ui_backend(std::make_shared<StubGuiBackend>());
    #if defined(HAVE_GTK2)
    host->register_ui_backend(std::make_shared<Gtk2UiBackend>());
    #endif
    #endif
    
    auto matches = host->find_plugin_matches(uri);
    if (matches.empty()) {
        red();
        std::cerr << "No plugin found\n"; 
        clear_color();
        host->closeHost();
        std::cout << "\033[?1049l";
        return 1;
    }

    blue();
    std::string fm =  "Found " + std::to_string(matches.size()) + " Plugins:\n";
    std::cout << std::string(pad, ' ') << fm;
    clear_color();

    int pchoice = 0;
    if (matches.size() > 1) pchoice = pager_print(matches, false);

    if (pchoice >= 0) {
        last_drawn_lines += 1;
        clear_previous_output();
        last_drawn_lines = 0;
        std::string sel =  "Selected: " + matches[pchoice].label ;
        blue();
        std::cout << std::string(pad, ' ') << sel;
        clear_color();
        std::cout << "\n";
        uri = matches[pchoice].uri;
    } else {
        // only request shutdown when not already removed from MultiHost (ctrl+c)
        if (!shutdown.load()) host->request_shutdown();
        break;
    }

    if (!host->init(uri.c_str())) {
        std::cout << "ERROR: Fail to instantiate " << uri << "\n";
        uri = "";
        continue;
    }

    auto presets = host->get_presets(uri.c_str());

    if (!presets.empty()) {
        int choice = -1;
        std::string pset = "Found " + std::to_string(presets.size()) + " presets:";
        blue();
        std::cout << std::string(pad, ' ') << pset;
        clear_color();
        std::cout << "\n";
        last_drawn_lines += 1;
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
            clear_previous_output();
            // only request shutdown when not already removed from MultiHost (ctrl+c)
            if (!shutdown.load()) host->request_shutdown();
            break;
        }
    }
    clear_previous_output();
    std::cout << std::flush;
    if (!preset_uri.empty()) host->apply_preset(preset_uri, preset_label);
    if (!host->initUi()) return 1;

    host->startUi();
    uri = "";
    //host.run_ui_loop();
}
    //mh.shutdown_all();
    if (signal_fd != -1) close(signal_fd);

    return 0;
}
