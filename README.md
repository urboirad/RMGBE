# RMGBE

Rick's Minimal Gap Buffer Editor - a lightweight text editor built from scratch in C and OpenGL.

Features syntax highlighting for C/C++, a built-in terminal, file browser sidebar, smooth cursor movement, and vim-style keybindings (H/J/K/L). Runs on Windows, macOS, and Linux.

## Building

**Windows:**
Requires [MinGW](https://www.mingw-w64.org/) with GCC. GLFW is included in `lib/`.
```sh
build.bat release    # optimized (~1.4 MB)
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
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo | Undo / Redo |
| `Ctrl+F` | Open search bar | Open search bar |
| `F3` / `Shift+F3` | Find next / previous match | Find next / previous match |
| `Ctrl+C` / `Ctrl+V` | Copy / Paste | Copy / Paste |
| `Ctrl+A` | Jump to start of line | Jump to start of line |
| `v` | Enter visual mode | — |
| `x` | Delete character under cursor | — |
| `o` | Open line below and enter insert mode | — |
| `Ctrl+N` | New file | New file |
| `Ctrl+Tab` | Switch focus between editor and terminal | — |

Scroll wheel scrolls the editor. Click files in the sidebar to open them.

## Theme Editor

Click **Theme** in the toolbar to open the theme editor. You can customize:

- **Editor colors**: background, text, cursor, toolbar, buttons, status bar
- **Syntax colors**: keywords, types, strings, numbers, comments, preprocessor directives, operators
- **File panel colors**: background, selection, text, folder text
- **Terminal colors**: background, border, output text, input background
- **Modal colors**: about/theme-editor panel background and border
- **Search colors**: search bar background, match highlight, current match highlight
- **Background gradient**: choose vertical, horizontal, or no gradient
- **Font size**: adjust the editor font size (6–72pt, live preview)
- **Font file**: load a custom `.ttf` font or use the built-in embedded font

### How it works

1. Click any color in the scrollable list to select it
2. Type a hex value (`#FF5500`) or RGB values (`255,85,0`) in the input field
3. Press Enter to apply the change live

### Theme files (.rmgtheme)

Themes can be saved and shared as `.rmgtheme` files - simple text files with hex color values:

```
name = My Theme
background_gradient = vertical
font_size = 16.0
background = #0d1117
background_end = #161b22
text = #e6edf3
toolbar = #010409
button = #8957e5
cursor = #1f6feb
statusbar = #010409
syntax_default = #e6edf3
keyword = #ff7b72
type = #ffa657
string = #a5d6ff
number = #79c0ff
comment = #8b949e
preprocessor = #d2a8ff
operator = #ff7b72
fp_background = #1f1f26
fp_selection = #336699
fp_text = #d9d9d9
fp_folder = #e6bf4d
term_background = #14141a
term_border = #3399ff
term_text = #cce6cc
term_input_bg = #0d0d12
modal_background = #1e1e1e
modal_border = #666673
search_bar_bg = #26262e
search_match = #ffbf00
search_match_cur = #ffd900
```

Use **Load...** to import a theme file and **Save As...** to export your current theme. Use **Reset** to restore the default colors.

## License

This project is open source under a custom non-commercial license. You can use, study, and modify the code, but you **cannot sell** the software or release forks without crediting the original project. See [LICENSE](LICENSE) for full details.
