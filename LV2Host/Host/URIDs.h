
/*
 * URIDs.h
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */


#pragma once

#include <lv2/urid/urid.h>
#include <lv2/atom/atom.h>
#include <lv2/parameters/parameters.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/patch/patch.h>
#include <lv2/midi/midi.h>

/****************************************************************
        URIDs.h - urids used by a  Host

****************************************************************/

struct URIDs {
    LV2_URID atom_eventTransfer;
    LV2_URID atom_Sequence;
    LV2_URID atom_Object;
    LV2_URID atom_Float;
    LV2_URID atom_Int;
    LV2_URID atom_Double;
    LV2_URID atom_Bool;
    LV2_URID midi_Event;
    LV2_URID buf_maxBlock;
    LV2_URID atom_Path;
    LV2_URID atom_String;
    LV2_URID patch_Get;
    LV2_URID patch_Set;
    LV2_URID patch_property;
    LV2_URID patch_value;
    LV2_URID atom_URID;
    LV2_URID atom_Blank;
    LV2_URID atom_Chunk;
    LV2_URID param_sampleRate;
};

