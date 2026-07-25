# RMGBE

Rick's Minimal Gap Buffer Editor — a lightweight text editor built from scratch in C and OpenGL.

Features syntax highlighting for C/C++, a built-in terminal, file browser sidebar, smooth cursor movement, and vim-style keybindings (H/J/K/L). Runs on Windows, macOS, and Linux.

## Building

Requires [CMake](https://cmake.org/) 3.15+ and [GLFW](https://www.glfw.org/).

**Linux:**
```sh
sudo apt install libglfw3-dev cmake
```

**macOS:**
```sh
brew install glfw cmake
```

**Windows:** GLFW is included as a static library in `lib/`, no extra dependencies needed.

### Build commands

```sh
# Release build (optimized, ~869 KB)
make release

# Debug build (with debug symbols)
make debug
```

Or directly with CMake:
```sh
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

The executable will be in `build-release/` (or `build-debug/`).

## Controls

| Key | Normal Mode | Insert Mode |
|-----|-------------|-------------|
| `i` | Enter insert mode | — |
| `Escape` | — | Back to normal mode |
| `h` `j` `k` `l` | Move cursor | — |
| Arrow keys | Move cursor | Move cursor |
| `Ctrl+S` | Save file | Save file |
| `Ctrl+C` / `Ctrl+V` | Copy / Paste | Copy / Paste |
| `Ctrl+A` | Jump to start of line | Jump to start of line |
| `v` | Enter visual mode | — |
| `x` | Delete character under cursor | — |
| `o` | Open line below and enter insert mode | — |
| `Ctrl+Tab` | Switch focus between editor and terminal | — |

Scroll wheel scrolls the editor. Click files in the sidebar to open them.
