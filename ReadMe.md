# Luma

### The tiny LV2 host you read to understand LV2.

**Luma** is a lightweight, multi-instance LV2 host for Linux focused on **clarity, correctness, and debugging**.

It is capable of running real-world LV2 plugins while remaining small enough to **read and understand as a reference implementation**.

---

# Luma in 30 seconds

**What it is**

A small and clean LV2 host capable of running real plugins with UI support and modern LV2 extensions.

**What makes it different**

Most LV2 hosts are embedded inside large DAWs or modular environments.  
Luma keeps the **core hosting logic simple and visible**, making it easy to understand how LV2 hosting actually works.

**What it supports**

- LV2 plugins with **X11 / GTK2  UIs**
- **Preset load and save**
- **LV2 state store and restore**
- **Shared worker thread**
- **Shared UI thread**
- **Terminal NO-GUI mode**
- **Debug build with DummyEngine**

**Typical uses**

- learning how LV2 hosting works
- debugging LV2 plugins
- testing LV2 extensions
- running plugins headless
- building experimental hosts

---

# Highlights

- lightweight LV2 host
- readable reference implementation
- libxputty LV2 UI frontend
- shared UI + worker threads
- preset + state support
- headless terminal mode
- debug mode with dummy engine

---

# Why Luma Exists

The LV2 ecosystem has many excellent hosts — from full DAWs to modular environments.  
However, in most of them the LV2 hosting logic is deeply embedded inside large and complex codebases.

**Luma exists for a different reason.**

It aims to provide a **small, readable, and correct LV2 host implementation** that focuses on the core mechanics of plugin hosting.

> **Luma is the tiny LV2 host you read to understand LV2.**

The project focuses on:

- clear and minimal host architecture  
- real-world LV2 compatibility  
- debugging visibility  
- readable source code  

Because of this focus, Luma is useful for:

- LV2 plugin developers
- host developers exploring the LV2 API
- debugging plugin/host interaction
- testing LV2 extensions
- running plugins headless

Luma intentionally avoids becoming a DAW.  
Instead it remains a **small LV2 laboratory** where the interaction between host, plugin, UI, and extensions can be easily understood.

---

# Features

## Core Hosting

- Multi-instance LV2 plugin hosting
- JACK audio backend
- Independent plugin engines
- Shared worker thread for all plugin instances
- Shared UI thread for all plugin UIs
- Proper LV2 feature negotiation
- Graceful shutdown handling
- Signal-safe exit

---

## UI Backends

Luma supports multiple LV2 UI types.

Supported systems:

- **libxputty LV2 UI frontend**
- **X11 LV2 UIs**
- **GTK2 LV2 UIs**
- **NO-GUI terminal mode**

Capabilities:

- Drag & Drop support
- Resize handling
- UI idle interface support
- Event forwarding
- Headless operation

The **NO-GUI mode** allows plugins to run entirely in the terminal, which is useful for:

- SSH sessions
- accessibility workflows
- minimal desktop setups
- debugging environments
- headless systems

---

## LV2 Extension Support

Luma supports many real-world LV2 extensions:

- LV2 State extension
- **State store / restore**
- LV2 Preset discovery
- **Preset load / save**
- LV2 Worker extension
- LV2 Atom support
- LV2 `data-access` extension (required for Calf plugins)
- URID mapping and resolution
- Patch message handling
- Feature injection system

---

## Debug Mode

Luma includes a dedicated **debug build** using a **DummyEngine**.

In debug mode:

- the JACK backend is replaced by a dummy audio engine
- DSP ↔ UI Atom events are logged
- control messages are printed
- Patch:Set messages are decoded
- URIDs are resolved to readable symbols
- LV2 feature negotiation becomes visible

This makes Luma useful as:

- an LV2 debugging tool
- a development inspection host
- a protocol analysis tool

---

# Feature Matrix

| Feature | Supported |
|--------|-----------|
| Multi-instance hosting | ✅ |
| JACK backend | ✅ |
| libxputty UI frontend | ✅ |
| X11 LV2 UI | ✅ |
| GTK2 LV2 UI | ✅ |
| NO-GUI terminal mode | ✅ |
| Shared UI thread | ✅ |
| Shared worker thread | ✅ |
| LV2 State | ✅ |
| LV2 State save | ✅ |
| LV2 State restore | ✅ |
| LV2 Preset discovery | ✅ |
| LV2 Preset load | ✅ |
| LV2 Preset save | ✅ |
| LV2 Worker | ✅ |
| LV2 Atom | ✅ |
| LV2 data-access | ✅ |
| URID mapping | ✅ |
| Resize handling | ✅ |
| Debug Atom logging | ✅ |
| Dummy audio engine | ✅ |
| Plugin graph / routing | ❌ |
| Session management | ❌ |
| Qt UI backend | ❌ |

---

# Requirements

- Linux
- JACK
- X11
- GTK2 (for GTK-based UIs)
- Lilv
- libxputty
- C++17 compatible compiler

Example installation (Debian / Ubuntu):

```bash
sudo apt install \
    libjack-jackd2-dev \
    liblilv-dev \
    libx11-dev \
    libgtk2.0-dev \
    pkg-config
```
# Building

Luma provides several build targets.

### Default build (libxputty frontend)

    make

Builds Luma with the **libxputty based LV2 UI frontend**.

---

### NO-GUI build

    make nogui

Builds a **terminal-only version** without graphical UI support.

Useful for:

- headless systems
- SSH sessions
- debugging
- automated testing

---

### Debug build

    make debug

Builds Luma with the **DummyEngine** and extended debug logging.

---

# Usage

Run Luma:

    ./luma

or launch a plugin directly:

    ./luma <plugin-name-or-uri>

If no plugin is specified, the **interactive browser** opens.

---

## Browser Controls

    ENTER   → next page
    ↑ / ↓   → page navigation
    number  → select plugin
    q       → quit

---

## Preset Selection

If presets are available:

    [0] OrangeCrunch
    [1] PreEQ

    Select preset (ENTER = default):

- number → load preset  
- ENTER → default state  

---

# Architecture Overview

Luma uses a modular architecture with shared infrastructure.

## Host Instance

Each plugin instance consists of:

    LV2Host
     ├── Engine (JACK or DummyEngine)
     ├── UI Backend
     ├── Atom Buffers
     └── State / Preset handling

---

## Global Threads

All plugin instances share common infrastructure:

    MultiHost
     ├── shared UI thread
     ├── shared worker thread
     └── multiple LV2Host instances

This keeps the system lightweight while supporting multiple plugins simultaneously.

---

# Design Philosophy

Luma intentionally avoids becoming a DAW.

Instead it focuses on:

- correctness over feature bloat  
- minimal and readable code  
- real-world LV2 compliance  
- debugging visibility  
- predictable behavior  

It is designed as a **compact and understandable LV2 reference host**.

---

# License

BSD-3-Clause

---

# Author

Luma is a compact LV2 host created as a **reference implementation and experimentation platform**.

---

Enjoy summoning your plugins.
