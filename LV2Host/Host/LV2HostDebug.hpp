
/*
 * LV2HostDebug.hpp.hpp
 *
 * SPDX-License-Identifier:  BSD-3-Clause
 *
 * Copyright (C) 2026 brummer <brummer@web.de>
 */

/****************************************************************
    LV2Host debug atom ports
****************************************************************/

    static void indent(int n) {
        for (int i = 0; i < n; ++i)
            fprintf(stderr, "\t");
    }

    static void print_hex(const uint8_t* buf, uint32_t size) {
        for (uint32_t i = 0; i < size; ++i)
            fprintf(stderr, "%02X", buf[i]);
    }

    // URI prefix shortening
    const char* shorten_uri(const char* uri) {

        if (!uri) return "unknown";

        struct Prefix {
            const char* base;
            const char* shortname;
        };

        static const Prefix prefixes[] = {
            {"http://lv2plug.in/ns/ext/patch#", "patch:"},
            {"http://lv2plug.in/ns/ext/atom#",  "atom:"},
            {"http://lv2plug.in/ns/ext/midi#",  "midi:"},
            {"http://www.w3.org/1999/02/22-rdf-syntax-ns#", "rdf:"},
            {"http://www.w3.org/2001/XMLSchema#", "xsd:"},
        };

        for (const auto& p : prefixes) {
            const size_t len = strlen(p.base);
            if (!strncmp(uri, p.base, len)) {
                static char buf[256];
                snprintf(buf, sizeof(buf), "%s%s", p.shortname, uri + len);
                return buf;
            }
        }

        // fallback: full URI in <>
        static char buf[512];
        snprintf(buf, sizeof(buf), "<%s>", uri);
        return buf;
    }

    // atom value printer
    void dump_atom_value(const LV2_Atom* atom) {

        const char* type_uri = unm.unmap(unm.handle, atom->type);
        const char* type = shorten_uri(type_uri);
        if (atom->type == urids.atom_URID) {
            LV2_URID id = *(const LV2_URID*)LV2_ATOM_BODY(atom);
            const char* uri = unm.unmap(unm.handle, id);
            fprintf(stderr, "%s", shorten_uri(uri));
        } else if (atom->type == urids.atom_Bool) {
            fprintf(stderr, "\"%d\"^^%s",
                *(const int32_t*)LV2_ATOM_BODY(atom), type);
        } else if (atom->type == urids.atom_Int) {
            fprintf(stderr, "\"%d\"^^%s",
                *(const int32_t*)LV2_ATOM_BODY(atom), type);
        } else if (atom->type == urids.atom_Float) {
            fprintf(stderr, "\"%g\"^^xsd:float",
                *(const float*)LV2_ATOM_BODY(atom));
        } else if (atom->type == urids.atom_String ||
                 atom->type == urids.atom_Path) {
            fprintf(stderr, "\"%s\"", (const char*)LV2_ATOM_BODY(atom));
        } else if (atom->type == urids.midi_Event) {
            fprintf(stderr, "\"");
            print_hex((const uint8_t*)LV2_ATOM_BODY(atom), atom->size);
            fprintf(stderr, "\"^^midi:MidiEvent");
        } else {
            fprintf(stderr, "%s", type);
        }
    }

    // object printer
    void dump_atom_object(const LV2_Atom_Object* obj) {

        const char* otype_uri = unm.unmap(unm.handle, obj->body.otype);
        const char* otype = shorten_uri(otype_uri);
        fprintf(stderr, "[]\n");
        indent(1);
        fprintf(stderr, "a %s", otype);
        LV2_ATOM_OBJECT_FOREACH(obj, prop) {
            const char* key_uri = unm.unmap(unm.handle, prop->key);
            const char* key = shorten_uri(key_uri);
            fprintf(stderr, " ;\n");
            indent(1);
            fprintf(stderr, "%s ", key);
            dump_atom_value(&prop->value);
        }
        fprintf(stderr, " .\n");
    }

    // main atom dispatcher
    void dump_atom(const LV2_Atom* atom) {
        //unwrap eventTransfer envelope
        if (atom->type == urids.atom_eventTransfer) {
            const LV2_Atom* inner = (const LV2_Atom*)LV2_ATOM_BODY(atom);
            dump_atom(inner);
            return;
        }

        // Sequence
        if (atom->type == urids.atom_Sequence) {
            const LV2_Atom_Sequence* seq = (const LV2_Atom_Sequence*)atom;
            LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
                dump_atom(&ev->body);
            }
            return;
        }

        // Object
        if (atom->type == urids.atom_Object || atom->type == urids.atom_Blank) {
            dump_atom_object((const LV2_Atom_Object*)atom);
            return;
        }
        // Raw value
        fprintf(stderr, "[]\n");
        indent(1);
        fprintf(stderr, "rdf:value ");
        dump_atom_value(atom);
        fprintf(stderr, " .\n");
    }

    //event wrapper
    void dump_atom_event(const LV2_Atom_Event* ev, const char* dir, bool color = false) {
        if (color) fprintf(stderr, "\033[1;34m");
        fprintf(stderr, "\n## %s (%u bytes) ##\n", dir, ev->body.size);
        dump_atom(&ev->body);
        if (color) fprintf(stderr, "\033[0m");
    }
