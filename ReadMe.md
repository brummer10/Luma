# Luma

**Luma** is a minimal LV2 host for Linux that runs LV2 plugins with X11 user interfaces using JACK for audio. It is designed to be small, simple, and easy to understand — a compact reference implementation that still supports real-world LV2 features like presets and plugin state.

Luma focuses on correctness and clarity rather than feature bloat. It is useful as:

* a lightweight LV2 plugin launcher
* a reference host implementation
* a development and debugging tool for LV2 plugins
* a minimal example of LV2 + JACK + X11 integration

---

## Features

* Loads and runs LV2 plugins
* Supports X11-based LV2 UIs
* Supports drag and drop
* JACK audio integration
* Preset discovery and interactive preset selection
* Full LV2 state and preset restore support
* Atom and control port handling
* Worker thread support (LV2 Worker extension)
* Minimal CLI interface
* Very small and readable codebase

---

## Requirements

Luma depends on:

* Linux
* JACK
* X11
* Lilv (LV2 host library)
* A C++ compiler (g++ or clang++)

Install dependencies on Debian/Ubuntu:

```
sudo apt install libjack-jackd2-dev liblilv-dev libx11-dev pkg-config
```

---

## Building

Compile using:

```
make
```

Or manually:

```
g++ main.cpp -o luma `pkg-config --cflags --libs jack lilv-0 x11` -ldl
```

---
## Usage

Start **Luma** by passing a plugin URI, plugin name, or a search string:

```
./luma <string>
```

If you start Luma **without arguments**, it shows a paged list of all installed LV2 plugins:

```
./luma
```

The built-in terminal browser lets you navigate through the list page by page and select a plugin interactively.

Controls:

* **ENTER** → next page
* **↑/↓** → page navigation
* **number** → select plugin
* **q** → quit

After selecting a plugin, Luma displays available presets (if any) and lets you choose one in the same way. Press **ENTER** to start the plugin with its default state.

Examples:

Search by name:

```
./luma neural
```

Start directly from URI:

```
./luma urn:brummer:neuralrack
```

If multiple plugins match the search string, Luma shows an interactive selection menu.

### Example: passing a search string

```
./luma neural
```

If multiple plugins are found, Luma lists them and prompts you to select one:

```
Find 6 matches:
[0] AIDA-X                [3] NeuralCapture
[1] Neural Amp Modeler    [4] Neural Record
[2] Neural Capture        [5] Neuralrack

ENTER = next page | number = select plugin | q = quit
>
```

### Example: passing a URI

```
./luma urn:brummer:neuralrack
```

### Example: passing a name

```
./luma neuralrack
```

If only one plugin matches the search string, Luma presents a list of available presets (if any) and prompts you to select one:

```
[0] OrangeCrunch
[1] PreEQ

Select preset (ENTER = default):
```

* Enter a preset number to load it
* Or press ENTER to start with the default state

If no presets are available, the plugin starts with its default state.

---

## How It Works

Luma:

1. Loads the LV2 plugin using Lilv
2. Instantiates the plugin with the required LV2 features
3. Connects JACK audio ports
4. Loads and restores preset state (if selected)
5. Launches the plugin’s X11 UI
6. Runs an event loop that synchronizes DSP and UI

The host implements enough of the LV2 specification to run modern plugins while remaining intentionally compact and readable.

---

## Limitations

* Only X11 UIs are supported
* No session management
* No plugin graph or routing system
* No plugin chaining
* No built-in preset saving (loading only)

Luma is intentionally minimal.

---

## License

Luma is released under the permissive BSD-3-Clause license.

---

## Acknowledgements

Built on top of:

* LV2 and Lilv
* JACK Audio Connection Kit
* X11

Thanks to the LV2 community for documentation and examples.

---

## Author

Luma is a minimal LV2 host created as a compact reference implementation and experimentation platform.

---

Enjoy summoning your plugins 🧙
