
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
struct InfoPair {
    std::string uri;
    std::string label;
};
