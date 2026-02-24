# Luma

**Luma** is a lightweight, multi-instance LV2 host for Linux designed for clarity, correctness, and debugging.

It supports real-world LV2 plugins including X11 and GTK2 user interfaces, advanced LV2 extensions such as `data-access`, and a fully functional terminal-based NO-GUI mode.

Luma is intentionally compact, readable, and suitable as both:

- a minimal plugin launcher  
- a reference LV2 host implementation  
- a development and debugging tool  
- a headless LV2 runner  
- a testbed for LV2 UI and Atom interaction  

---

## Features

### Core Hosting
- Loads and runs LV2 plugins
- Multi-instance support
- Independent plugin engines
- JACK audio integration
- Proper LV2 feature negotiation
- Clean shutdown handling
- Signal-safe exit

---

### UI Support

- X11-based LV2 UIs
- GTK2-based LV2 UIs
- Drag and Drop support
- Resize handling
- UI idle interface support
- **NO-GUI mode via terminal interface**
- Basic terminal control interaction (accessibility-friendly)

Luma can run plugins entirely without graphical UI.  
This makes it usable in:

- SSH sessions
- minimal desktop setups
- accessibility-driven workflows
- headless systems

---

### LV2 Extension Support

- LV2 State extension
- LV2 Preset discovery and restore
- LV2 Worker extension
- LV2 Atom support
- LV2 data-access extension (required for Calf plugins)
- URID mapping and resolution
- Patch message handling
- Full feature injection system

---

### Debug Mode

Luma includes a dedicated debug mode using a **DummyEngine**.

In debug mode:

- The real JACK engine is replaced
- All Atom events (DSP → UI and UI → DSP) are logged
- Control change messages are printed
- Patch:Set messages are decoded
- URIDs are resolved to readable symbols
- Feature negotiation becomes visible

This makes Luma useful as:

- an LV2 debugging tool
- a development inspection host
- a protocol analysis tool

---

### CLI Interface

- Interactive terminal plugin browser
- Paged navigation
- Preset selection
- Search by name or URI
- Clean terminal restore on exit

---

## Feature Matrix

| Feature | Supported |
|----------|------------|
| Multi-instance hosting | ✅ |
| JACK audio backend | ✅ |
| X11 UI | ✅ |
| GTK2 UI | ✅ |
| NO-GUI / Terminal mode | ✅ |
| Drag & Drop | ✅ |
| LV2 State | ✅ |
| LV2 Presets (restore) | ✅ |
| LV2 Worker | ✅ |
| LV2 Atom | ✅ |
| LV2 data-access | ✅ |
| URID mapping | ✅ |
| Resize handling | ✅ |
| Debug Atom logging | ✅ |
| Dummy audio engine | ✅ |
| Preset saving | ❌ |
| Plugin graph / routing | ❌ |
| Session management | ❌ |
| Qt UI backend | ❌ |

---

## Requirements

- Linux
- JACK
- X11
- GTK2 (for GTK-based UIs)
- Lilv (LV2 host library)
- C++17 compatible compiler

Install dependencies (Debian/Ubuntu):

```bash
sudo apt install libjack-jackd2-dev liblilv-dev libx11-dev libgtk2.0-dev pkg-config
```

---

## Building

```bash
make
```

Manual:

```bash
g++ main.cpp -o luma \
    `pkg-config --cflags --libs jack lilv-0 x11 gtk+-2.0` \
    -ldl -pthread
```

---

## Usage

Start Luma by passing:

```bash
./luma <string>
```

If no argument is provided:

```bash
./luma
```

You get an interactive plugin browser.

Controls:

- ENTER → next page
- ↑ / ↓ → page navigation
- number → select plugin
- q → quit

---

### Preset Selection

If presets are available:

```
[0] OrangeCrunch
[1] PreEQ

Select preset (ENTER = default):
```

- Enter number → load preset  
- ENTER → default state  

---

## Architecture Overview

Each plugin instance consists of:

```
LV2Host
 ├── Engine (JACK or DummyEngine)
 ├── UI Backend (X11 / GTK2 / NO-GUI)
 ├── Worker Thread
 ├── Atom Buffers
 └── State / Preset Handling
```

Multiple instances are managed by:

```
MultiHost
 └── owns multiple LV2Host instances
```

Each plugin runs independently and can be routed freely via JACK (e.g. qjackctl).

---

## Design Philosophy

Luma intentionally avoids becoming a DAW.

Instead, it focuses on:

- correctness over feature bloat  
- minimal, readable code  
- real-world LV2 compliance  
- debugging visibility  
- predictable behavior  

It is designed as a compact and understandable reference host.

---

## License

BSD-3-Clause

---

## Author

Luma is a compact LV2 host created as a reference implementation and experimentation platform.

---

Enjoy summoning your plugins 🧙
