
/*
 * LV2HostTypes.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once

#include <string>

/****************************************************************
    Generic URI + Label pair
    Used by:
        - IHostUiBridge
        - LV2PluginRegistry
        - host front-end
****************************************************************/

#define LV2_UI__INTERNAL "http://lv2plug.in/ns/extensions/ui#internalUi"

struct InfoPair {
    std::string uri;
    std::string label;
};

struct EnumPair {
    float val;
    std::string label;
};
