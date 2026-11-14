# Changelog

## 1.3.2 — 2026-11-14

- Removing Cinnarchy now puts the desktop back. The installer packages restore
  the setup from before the first theme was applied, stop animated
  backgrounds and remove the login entry, for whoever is logged in. Before,
  an uninstall left the Omarchy look in place with nothing to change it back.
  Without a saved setup the system's default look is used. `cinnarchy reset`
  does the same by hand.
- The menu shows Cinnarchy's own icon right after installing. Cinnamon showed
  a generic gear until the next login.
- The install instructions cover the "Failed to download package files"
  message on a new Linux Mint installation, and state what Cinnarchy needs:
  Cinnamon's regular X11 session and a screen of 1024×768 or larger. Wayland
  sessions are not supported. They also say that the Debian 13 installer has
  not been tested on LMDE 7 itself.

## 1.3.1 — 2026-11-12

- Animated backgrounds now come back after logging in even when the desktop is
  still starting: a player that cannot start is retried for about two
  minutes instead of giving up. Found on a fresh Linux Mint 22.3 install.
- Desktop icons keep working while an animated background plays. Nemo treated
  the video window as another desktop program and skipped drawing icons when
  it started after the video; Cinnarchy now adds its window to Nemo's
  `ignored-desktop-handlers` setting.
- Each player starts only after Cinnamon has taken charge of its window, to
  stop one display staying black while another plays.

## 1.3.0 — 2026-11-12

- Video controls for animated backgrounds: **Speed** from 0.25× to 2× and
  **Repeat**, which rests the animation on its last frame for 5 seconds up to
  an hour between loops, or plays it once. Each display keeps its own choice,
  a playing background changes at once, and the preview follows the speed.
  `cinnarchy apply` accepts `--speed` and `--repeat-after`.
- After an upgrade, a live wallpaper started by the older version is handed
  over to the new player the next time it is changed.
- Installer packages: each release now has a `.deb` for Debian 13 / LMDE 7 and
  one for Linux Mint 22 / Ubuntu 24.04, built and install-tested by CI.
- Video previews support the Qt 6.4 version included in Linux Mint 22.
  Its installer includes the GStreamer plugins needed for video playback.
- Licensing is easier to find: `LICENSE` is the GNU GPL version 2 text (the
  project is GPL version 2 or later, as before) and `NOTICE` lists the
  Cinnamon and Omarchy material with its MIT notice. The package no longer
  recommends the locally built backgrounds package.

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
