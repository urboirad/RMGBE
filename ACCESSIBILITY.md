# Accessibility

RMGBE (Rick's Minimalist Gap Buffer Editor) is a small text editor built from scratch in C and OpenGL. We want it to be usable by as many people as possible, and we're upfront about where it falls short today.

This document describes what RMGBE currently offers, its known limitations, how to report accessibility problems, and what contributors should keep in mind.

## What RMGBE offers today

### Keyboard-driven editing

Core editing can be done entirely from the keyboard, with vim-style modal editing and standard shortcuts:

- Normal and insert modes, plus visual mode (`i`, `Escape`, `v`)
- Cursor movement with `h` `j` `k` `l` or the arrow keys
- Save (`Ctrl+S`), new file (`Ctrl+N`), undo/redo (`Ctrl+Z` / `Ctrl+Y`), copy/paste (`Ctrl+C` / `Ctrl+V`)
- Search (`Ctrl+F`) with next/previous match (`F3` / `Shift+F3`)
- Switching focus between the editor and the built-in terminal (`Ctrl+Tab`)

See the [README](README.md#controls) for the full list of controls.

### Adjustable appearance

The built-in theme editor (click **Theme** in the toolbar) lets you customize how RMGBE looks:

- **Colors** for the editor, syntax highlighting, file panel, terminal, modals, and search results
- **Background gradient** (vertical, horizontal, or none). Choose none for a flat, solid background.
- **Font size** from 6 to 72 pt, with live preview
- **Custom fonts** by loading any `.ttf` file, for example a font designed for dyslexia or low vision
- **Saving and sharing themes** as plain-text `.rmgtheme` files

Color values can be entered as hex (`#FF5500`) or RGB (`255,85,0`), so exact contrast ratios can be set precisely.

### Other design choices that help

- Smooth, predictable cursor movement
- No required network connection, accounts, or telemetry-driven prompts
- No flashing or blinking content intended to draw attention (if you find any, please report it)

## Known limitations

RMGBE draws its entire interface itself using OpenGL rather than native operating system controls. This keeps it small and portable, but it has real accessibility costs:

- **Screen readers are not supported.** The editor does not expose its content or interface to platform accessibility APIs (such as UI Automation on Windows, NSAccessibility on macOS, or AT-SPI on Linux). Screen readers like NVDA, JAWS, VoiceOver, and Orca will not be able to read the editor.
- **No built-in high-contrast or dark/light mode detection.** RMGBE does not follow operating system theme or contrast settings. You can approximate this by creating a high-contrast `.rmgtheme`.
- **No built-in text-to-speech, braille display support, or audio cues.**
- **Some features rely on the mouse.** For example, opening files from the sidebar is done by clicking. Not every interface element is guaranteed to be reachable by keyboard alone.
- **Modal editing has a learning curve.** The vim-style modes can be confusing or tiring for some users, and there is currently no option to disable normal mode and edit in insert mode only.
- **Key bindings are not remappable.**
- **No IME or complex text input guarantees.** Input methods for some languages, and right-to-left or complex-script text, may not work correctly.
- **No OS-level text scaling.** Use the font size setting in the theme editor instead.

If one of these gaps blocks you, we'd like to hear about it. It helps us decide what to prioritize.

## Reporting accessibility issues

Please open an issue at [github.com/urboirad/RMGBE/issues](https://github.com/urboirad/RMGBE/issues) and include **"Accessibility"** in the title. Helpful details include:

- What you were trying to do
- What happened and what barrier you ran into
- Your operating system and version
- Any assistive technology you use (screen magnifier, voice control, switch device, etc.) and its version
- Your theme or font settings, if relevant
- Suggestions for a fix, if you have one (not required)

You don't need to be an accessibility expert to report a problem. Describing your experience is enough.

## Tips for a more accessible setup

- **Low vision:** Increase the font size and load a font you find easy to read. Pick high-contrast text and background colors, and set the background gradient to none.
- **Light sensitivity:** Use a dark theme with softer contrast, or a muted light theme, and remove the gradient.
- **Color vision deficiency:** Adjust the syntax colors so token types are distinguishable by brightness as well as hue. Avoid pairing red and green as the only difference.
- **Reduced mouse use:** Use the keyboard shortcuts for saving, searching, creating files, and switching to the terminal.

## Guidelines for contributors

When you add or change features, please keep accessibility in mind:

- **Keyboard first.** Every new feature should be reachable by keyboard. Don't add mouse-only interactions.
- **Don't rely on color alone.** If color conveys meaning (for example, the current search match), add a second cue such as position, outline, or brightness difference.
- **Keep it themeable.** New UI elements should have configurable colors that appear in the theme editor and in `.rmgtheme` files, rather than hard-coded values.
- **Respect font size.** New text should scale with the font size setting, and layouts shouldn't break at large sizes (up to 72 pt) or small ones (down to 6 pt).
- **Check contrast.** The default theme and any themes you contribute should aim for at least a 4.5:1 contrast ratio between text and its background, as recommended by [WCAG 2.2](https://www.w3.org/TR/WCAG22/).
- **No flashing.** Avoid anything that flashes more than three times per second.
- **Be gentle with motion.** Smooth cursor movement and animations should be short, and ideally something that can be turned off.
- **Document shortcuts.** Add new key bindings to the README controls table.
- **Test without a mouse.** Try your change using only the keyboard.

Contributions that improve accessibility are very welcome, including work toward platform accessibility API support, remappable keys, or an option to disable modal editing. See [CONTRIBUTING.md](CONTRIBUTING.md) for how to get started.

## Our commitment

RMGBE is a small project maintained by an individual, so we can't promise timelines. We do commit to reading accessibility reports, being honest about what we can and can't fix, and welcoming contributions that make the editor easier to use for more people.

*This document reflects the state of the project at the time of writing and will be updated as RMGBE changes.*
