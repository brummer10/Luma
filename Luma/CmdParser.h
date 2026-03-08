
/*
 * CmdParser.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


/****************************************************************
        CmdParser.h  parse command-line args
                        
****************************************************************/

#pragma once

#include <optional>
#include <string>
#include <iostream>
#include <cstring>
#include <cstdlib>

struct CmdParser {

    struct CmdOptions {
        std::optional<std::string> uri;
        std::optional<std::string> preset;
    } opts;


    void printUsage(const char* progName) {
        std::cout
            << "\nUsage: " << progName << " [options]\n"
            << "    Options:\n"
            << "      -h, --help             print this help and exit\n"
            << "      -u, --uri <name>       plugin uri to load\n"
            << "      -p, --preset <name>    preset uri or Label to load\n\n"
            << "      default: (no option)   show plugin list\n\n";
    }

    static bool parseFloat(const char* str, float& out) {
        char* end = nullptr;
        out = std::strtof(str, &end);
        return end != str && *end == '\0';
    }

    static bool parseInt(const char* str, int& out) {
        char* end = nullptr;
        out = (int)std::strtod(str, &end);
        return end != str && *end == '\0';
    }

    bool parseCmdLine(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const char* arg = argv[i];

            if (std::strcmp(arg, "-u") == 0 || std::strcmp(arg, "--uri") == 0) {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --uri requires a string\n";
                    return false;
                }
                opts.uri = argv[++i];
            } else if (std::strcmp(arg, "-p") == 0 || std::strcmp(arg, "--preset") == 0) {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --preset requires a string\n";
                    return false;
                }
                opts.preset = argv[++i];
            } else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
                return false;
            }
        }
        return true;
    }

};
