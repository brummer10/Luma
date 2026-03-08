# LV2Host

`LV2Host` is the core hosting library used by **Luma**.  
It provides a small, modular LV2 hosting implementation with pluggable engines and UI backends.

The design focuses on **clarity, separation of concerns, and minimal LV2 hosting logic**.

---

# Overview

The library is structured into four main subsystems:

```
LV2Host
 ├── Engines     (audio backends)
 ├── BackEnds    (UI implementations)
 ├── Host        (core LV2 hosting logic)
 └── InterFaces  (abstract interfaces)
```

Multiple plugin instances are managed by `MultiHost`.

```
MultiHost
 └── multiple LV2Host instances
```

Shared infrastructure:

- shared UI thread
- shared worker thread

---

# Directory Structure

## Engines

Audio processing backends implementing the `IDspEngine` interface.

```
Engines/
 ├── JackEngine.hpp
 ├── InternalEngine.hpp
 └── DummyEngine.hpp
```

- **JackEngine**  
  Real JACK audio backend.

- **InternalEngine**  
  Host-controlled processing engine.

- **DummyEngine**  
  Debug/testing engine without real audio.

---

## BackEnds

LV2 UI backend implementations.

```
BackEnds/
 ├── XputtyUiBackend
 ├── X11UiBackend
 ├── GTK2UiBackend
 ├── GTK2inX11UiBackend -> do not work!
 ├── StubGuiBackend
 └── NoGuiBackend
```

Responsibilities:

- creating plugin UIs
- event forwarding
- resize handling
- drag & drop support
- UI idle handling

Backends communicate with the host through `IHostUiBridge`.

---

## Host

Core LV2 hosting implementation.

```
Host/
 ├── LV2Host.cpp
 ├── LV2HostContext.hpp
 ├── LV2HostPorts.hpp
 ├── LV2HostState.hpp
 ├── LV2HostWorker.hpp
 ├── LV2PluginRegistry.hpp
 ├── LV2HostTypes.hpp
 ├── LV2HostDebug.hpp
 ├── URIDs.h
 └── lv2_ringbuffer.h
```

Main responsibilities:

- LV2 plugin instantiation
- feature negotiation
- URID mapping
- Atom message handling
- worker thread support
- state save / restore
- preset load / save
- UI ↔ DSP communication

`LV2HostContext` contains shared resources such as the LV2 world and plugin registry.

---

## InterFaces

Abstract interfaces used to decouple host components.

```
InterFaces/
 ├── IDspEngine.hpp
 ├── IUiBackend.hpp
 └── IHostUiBridge.hpp
```

These interfaces allow:

- interchangeable audio engines
- interchangeable UI backends
- clean separation between DSP and UI systems

---

# Key Classes

### LV2Host

Represents a single plugin instance.

Responsibilities:

- plugin lifecycle
- DSP processing
- UI integration
- state handling
- worker scheduling

---

### MultiHost

Manages multiple plugin instances and shared infrastructure.

Responsibilities:

- instance lifetime
- shared UI thread

---

### LV2HostContext

Global shared context containing:

- Lilv world
- plugin registry
- URID mapping
- common LV2 features
- shared worker thread

---

# Design Goals

The LV2Host library aims to be:

- **minimal**
- **readable**
- **correct**
- **easy to experiment with**

It intentionally avoids the complexity of full DAW architectures while still supporting real-world LV2 plugins and extensions.

---

# Used by

This library is used by the **Luma LV2 host**.
