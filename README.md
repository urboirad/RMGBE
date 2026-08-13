# RMGBE

Rick's Minimal Gap Buffer Editor - a lightweight text editor built from scratch in C and OpenGL.

Features syntax highlighting for C/C++, a built-in terminal, file browser sidebar, smooth cursor movement, and vim-style keybindings (H/J/K/L). Runs on Windows, macOS, and Linux.

## Building

**Windows:**
Requires [MinGW](https://www.mingw-w64.org/) with GCC. GLFW is included in `lib/`.
```sh
build.bat release    # optimized (~869 KB)
build.bat debug      # with debug symbols
```
Output: `output/RMGBE.exe`

**Linux / macOS:**
Requires [CMake](https://cmake.org/) 3.15+ and [GLFW](https://www.glfw.org/).
```sh
# Linux
sudo apt install libglfw3-dev cmake

# macOS
brew install glfw cmake

# Build
make release    # or: make debug
```
Output: `build-release/RMGBE`

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

## License

This project is open source under a custom non-commercial license. You can use, study, and modify the code, but you **cannot sell** the software or release forks without crediting the original project. See [LICENSE](LICENSE) for full details.
