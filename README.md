# CS2 External Mod Menu
> Aimbot + ESP | External | DX11 Overlay | ImGui

---

## Features

| Feature | Details |
|---|---|
| **Aimbot** | FOV-based, smooth, silent aim toggle, bone selector |
| **ESP Boxes** | Corner-bracket style, visible/invisible color split |
| **Health Bar** | Left-side vertical bar, color-coded |
| **Name Tag** | Player name above box |
| **Snaplines** | Bottom-center to entity foot |
| **Mod Menu** | ImGui tabbed UI, INSERT to toggle |

---

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 (MSVC)
- CMake 3.20+
- [ImGui](https://github.com/ocornut/imgui) — clone into `imgui/` folder
- [cs2-dumper](https://github.com/a2x/cs2-dumper) — for current offsets

---

## Build

```bash
# 1. Clone ImGui
git clone https://github.com/ocornut/imgui imgui

# 2. Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# 3. Build
cmake --build build --config Release
```

Or open in Visual Studio 2022 → Open Folder → build with CMake.

---

## Usage

1. Launch CS2
2. Run `CS2ModMenu.exe` as Administrator
3. `INSERT` — toggle menu open/close
4. `LMB` (hold) — aimbot active

---

## Offset Updates

Offsets are in `src/main.cpp` under `namespace offsets`.  
Use [cs2-dumper](https://github.com/a2x/cs2-dumper) after game updates:

```cpp
namespace offsets {
    constexpr uintptr_t dwLocalPlayerPawn = 0x1815988; // update here
    constexpr uintptr_t dwEntityList      = 0x18C4AB8;
    constexpr uintptr_t dwViewMatrix      = 0x19247C0;
    // ...
}
```

---

## Project Structure

```
cs2_mod/
├── src/
│   └── main.cpp          # all logic — memory, aimbot, ESP, overlay
├── imgui/                # clone ImGui here
├── CMakeLists.txt
└── README.md
```

---

## Notes

- External only — no injection, no driver
- Run as Administrator (required for `OpenProcess`)
- Anti-cheat status: use on private/offline servers only
- Offsets break on game updates — redump with cs2-dumper
MY DISCORD = sourcecodelibbsd.so
