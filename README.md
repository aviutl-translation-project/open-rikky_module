# open-rikky_module

Open-source C rebuild of **rikky_module** (v1.4b) and **rikky_memory** (v0.6b) by rikky for AviUtl 1.10 + ExEdit 0.92.

Also works with [AviUtl64](https://github.com/aviutl-translation-project/aviutl64) via 32-bit plugin bridge.

## Installation

Copy `rikky_memory.auf` and `rikky_module.dll` into your AviUtl folder.

## Building

### Requirements

1. Windows 10 or 11
2. Visual Studio 2019 (or newer) with C++ x86 build tools
3. CMake 3.16+

### Instructions

```powershell
cmake -B build -A Win32
cmake --build build --config Release
```

Binaries will output as `rikky_memory.auf` and `rikky_module.dll` in `build/Release`.

