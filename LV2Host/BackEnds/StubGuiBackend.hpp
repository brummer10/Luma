#pragma once

#include "IUiBackend.hpp"
#include "IHostUiBridge.hpp"

#include <X11/Xlib.h>
#include <vector>
#include <string>

class StubGuiBackend : public IUiBackend {
public:
    explicit StubGuiBackend();
    ~StubGuiBackend() override;

    void attach_bridge(IHostUiBridge* b) override;

    const char* lv2_ui_uri() const override { return nullptr; }

    bool create_ui(int w, int h) override;
    void close_window() override;
    void embed_native(void*) override {}
    void resize(int, int) override {}
    void finalize_window(const char* title) override;
    void poll_events() override;
    void set_preset_name(const std::string pname) override;
    void set_close_callback(std::function<void()> cb) override {
        close_cb = std::move(cb);
    }

    void* native_window() override { return (void*)window; }

private:
    struct Slider {
        uint32_t control_index;
        int x;
    };

    struct Meter {
        uint32_t meter_index;
        int x;
    };

    void rebuild_layout();
    void draw();
    void draw_slider(const Slider& s);
    void draw_meter(const Meter& m);

private:
    IHostUiBridge* bridge = nullptr;

    Display* display = nullptr;
    Window window = 0;
    Atom wm_delete_ = None;
    Atom wm_protocols = None;
    GC gc = 0;

    std::function<void()> close_cb;

    std::vector<Slider> sliders;
    std::vector<Meter> meters;

    int width = 640;
    int height = 240;
};
