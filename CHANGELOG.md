# Changelog

## 1.3.0 — 2026-11-12

- Video controls for animated backgrounds: **Speed** from 0.25× to 2× and
  **Repeat**, which rests the animation on its last frame for 5 seconds up to
  an hour between loops, or plays it once. Each display keeps its own choice,
  a playing background changes at once, and the preview follows the speed.
  `cinnarchy apply` accepts `--speed` and `--repeat-after`.
- After an upgrade, a live wallpaper started by the older version is handed
  over to the new player the next time it is changed.

## 1.2.1 — 2026-11-12

- Works without Omarchy installed: **Download backgrounds** fetches a built-in
  theme's official backgrounds (or every theme's) from Omarchy's repository;
  `cinnarchy fetch --backgrounds` does the same. Until then a theme keeps your
  current background instead of failing to apply.
- Built-in themes carry their Omarchy 4.5.0 icon choice, so they no longer
  need an installed `icons.theme`.
- Fits small screens: the window opens inside the available screen, its options
  scroll when short, and the Apply/Undo row always stays visible.
- Space now presses the button or checkbox that has keyboard focus; it still
  applies the theme otherwise. Cards show a focus outline for keyboard use.
- One consistent button style; the sidebar shows the version next to
  **Report a problem**.
- Closing the picker stops a download in progress instead of waiting for it.

## 1.2.0 — 2026-11-12

- **Get more themes…** downloads official themes added to
  [omacom/omarchy](https://github.com/omacom/omarchy) since this release, and
  installs community themes from [omarchy.org/themes](https://omarchy.org/themes/)
  or any theme's git URL. New `cinnarchy fetch` command.
- Downloaded and hand-installed Omarchy themes get a matching Cinnamon/GTK theme,
  generated from the same template as the bundled themes.
- **Report a problem** opens a GitHub issue with the version and system filled in.
- Removed the theme search box and the keyboard shortcuts window.
- Desktop shortcuts no longer fire while typing in the Get more themes dialog.
- Requires `git` for downloads.

## 1.1.0 — 2026-11-10

- Per-display and animated wallpapers, Undo/Redo/Restore, Danish translation,
  Debian packaging. See VALIDATION.txt.
