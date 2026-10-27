# Contributing to RMGBE

Thanks for your interest in contributing to **RMGBE** (Rick's Minimalist Gap Buffer Editor)! This is a small, lightweight text editor written from scratch in C and OpenGL, and contributions of all sizes are welcome, from typo fixes to new features.

Please read this guide before opening an issue or pull request. By participating, you agree to follow our [Code of Conduct](CODE_OF_CONDUCT.md).

## Table of Contents

- [Project Philosophy](#project-philosophy)
- [Ways to Contribute](#ways-to-contribute)
- [Getting Started](#getting-started)
- [Project Layout](#project-layout)
- [Making Changes](#making-changes)
- [Coding Style](#coding-style)
- [Testing Your Changes](#testing-your-changes)
- [Submitting a Pull Request](#submitting-a-pull-request)
- [Reporting Bugs](#reporting-bugs)
- [Suggesting Features](#suggesting-features)
- [Themes](#themes)
- [License](#license)

## Project Philosophy

RMGBE is intentionally **minimal**. When deciding whether a change belongs, keep these goals in mind:

- **Small and fast.** The release build is around 1.4 MB. Avoid changes that bloat the binary or slow startup.
- **Few dependencies.** The editor is built on C, OpenGL, and GLFW. New third-party dependencies need a strong justification.
- **Cross-platform.** Everything should work on Windows, macOS, and Linux. Don't introduce platform-specific behavior without guarding it properly.
- **Simple, readable C.** Prefer straightforward code over clever abstractions.

If you're unsure whether a feature fits, open an issue to discuss it before investing time in a large pull request.

## Ways to Contribute

- Report bugs and crashes
- Suggest or discuss features
- Fix bugs or implement features
- Improve documentation (README, comments, this guide)
- Add syntax highlighting for more languages
- Create and share `.rmgtheme` themes
- Test on different platforms and report your results

## Getting Started

### 1. Fork and clone

```bash
git clone https://github.com/<your-username>/RMGBE.git
cd RMGBE
git remote add upstream https://github.com/urboirad/RMGBE.git
```

### 2. Install prerequisites

**Windows**
- [MinGW](https://www.mingw-w64.org/) with GCC (GLFW is bundled in `lib/`)

**Linux**
```bash
sudo apt install libglfw3-dev cmake
```

**macOS**
```bash
brew install glfw cmake
```

CMake 3.15 or newer is required on Linux and macOS.

### 3. Build

**Windows**
```
build.bat debug      # with debug symbols (recommended while developing)
build.bat release    # optimized build
```
Output: `output/RMGBE.exe`

**Linux / macOS**
```bash
make debug           # recommended while developing
make release         # optimized build
```
Output: `build-debug/RMGBE` or `build-release/RMGBE`

## Project Layout

| Path | Purpose |
|------|---------|
| `main.c` | Program entry point |
| `src/` | Editor source code |
| `include/` | Header files |
| `lib/` | Bundled libraries (e.g., GLFW for Windows) |
| `vendor/` | Third-party code |
| `assets/` | Embedded resources such as the built-in font |
| `.github/workflows/` | CI configuration |
| `CMakeLists.txt`, `Makefile`, `build.bat` | Build scripts |
| `compile_flags.txt` | Compiler flags for editor tooling (e.g., clangd) |

## Making Changes

1. **Sync with upstream** before starting:
   ```bash
   git checkout main
   git pull upstream main
   ```
2. **Create a branch** with a descriptive name:
   ```bash
   git checkout -b fix/search-highlight-crash
   ```
   Suggested prefixes: `fix/`, `feat/`, `docs/`, `refactor/`, `theme/`.
3. **Keep changes focused.** One logical change per pull request. Avoid mixing unrelated refactors with bug fixes.
4. **Commit clearly.** Write short, imperative commit messages that explain *what* and *why*:
   ```
   Fix cursor drift after undoing a multi-line paste
   ```

## Coding Style

Please match the style of the surrounding code. In general:

- Write **C** (not C++) for the editor core, and keep it portable across GCC, Clang, and MinGW.
- Use consistent indentation and brace style with the existing files.
- Use clear, descriptive names for functions and variables.
- Keep functions short and single-purpose where practical.
- Check the return values of allocation, file I/O, and other fallible calls. Free what you allocate.
- Be careful with the gap buffer: watch for off-by-one errors, buffer growth, and gap movement edge cases.
- Comment *why*, not *what*. Code should be self-explanatory about the "what".
- Don't commit commented-out code, stray debug prints, or build output.
- Don't add compiler warnings. Aim for a clean build with warnings enabled.

## Testing Your Changes

There is no automated test suite yet, so manual testing matters. Before submitting, please:

- Build in **both** debug and release modes if possible.
- Exercise the feature or fix you changed, plus nearby behavior.
- Try the common editing paths: insert and normal mode, undo/redo, copy/paste, search (`Ctrl+F`, `F3`), saving, opening files from the sidebar, and the built-in terminal.
- Test edge cases: empty files, very long lines, large files, files without a trailing newline, and Unicode or unusual input where relevant.
- If your change touches rendering, input, or file paths, test on more than one OS when you can, and say in your PR which platforms you tested.

Contributions that add automated tests are especially welcome.

## Submitting a Pull Request

1. Push your branch to your fork.
2. Open a pull request against `main` on `urboirad/RMGBE`.
3. In the description, include:
   - **What** the change does and **why**
   - Related issue numbers (e.g., `Closes #12`)
   - **How you tested it** and on which platform(s)
   - Screenshots or short recordings for any visible UI change
4. Make sure the project still builds cleanly and CI passes.
5. Respond to review feedback. Pushing follow-up commits to the same branch updates the PR.

Pull requests may be asked to change scope, style, or approach to keep the project minimal. This is not a judgment of the work, and it helps to discuss large changes in an issue first.

## Reporting Bugs

Before filing, search [existing issues](https://github.com/urboirad/RMGBE/issues) to avoid duplicates. A good bug report includes:

- **Operating system** and version
- **Build type** (debug or release) and how you built it
- **Steps to reproduce**, as minimal as possible
- **Expected behavior** vs. **actual behavior**
- Any crash output, error messages, or screenshots
- A sample file, if the bug depends on file contents

## Suggesting Features

Open an issue describing:

- The problem you're trying to solve
- Your proposed solution and any alternatives you considered
- How it fits with the project's minimal philosophy

## Themes

Themes are plain-text `.rmgtheme` files. To share one, include the file in a pull request or attach it to an issue or discussion. Please use a clear `name = ...` entry, and check that the text remains readable against the background in all UI areas (editor, file panel, terminal, search bar, and modals).

## License

RMGBE is released under a custom non-commercial license. Please read the [LICENSE](LICENSE) before contributing. By submitting a contribution, you agree that your work will be distributed under the same license as the project.

---

Questions? Open an issue and ask. Thanks for helping make RMGBE better!
