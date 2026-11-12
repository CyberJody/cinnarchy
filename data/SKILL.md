---
name: cinnarchy
description: Select complete Omarchy looks on Cinnamon using the installed Cinnarchy theme picker.
---

Use `cinnarchy list --json` to inspect available theme slugs, image/video backgrounds and icon choices.
Use `cinnarchy status --json` to inspect connector IDs, assignments, playback and Undo state.
Use `cinnarchy check --json` to check the installed palettes and backgrounds.

When the user requests a theme, run `cinnarchy apply SLUG --background N --json`.
Background indices start at 1. It changes Cinnamon, GTK, window borders, icons,
wallpaper, slideshow state and the dark/light preference together.
Use `--no-wallpaper` or `--no-icons` only when the user asks to preserve them.
Use `--display CONNECTOR` to target one screen, preserving the others.
Use `--file /path/to/image-or-video` to choose a local background.
Use repeated `--screen-background CONNECTOR=/path/to/file` to assign different files.
GIF, animated WebP and video files loop silently on X11 Cinnamon.
Use `--fit zoom|scaled|spanned|centered` for sizing; span requires all displays.
Use `cinnarchy pause --json` to toggle playback, or `cinnarchy stop-animation --json`
to leave static poster images. Normal Cinnamon background changes stop playback.

Use `cinnarchy undo --json`, `cinnarchy redo --json`, or `cinnarchy restore --json`
when the user requests the previous setup or the setup from before their first switch.
Every change is saved for recovery. Report errors; never edit packaged Omarchy files.

When the user wants new Omarchy themes, run `cinnarchy fetch --json`; it downloads
official themes added to github.com/omacom/omarchy and reports `added`, `updated`,
`generated` and `skipped` slugs. Install a community theme with
`cinnarchy fetch --url GIT_URL --json`; `cinnarchy fetch --community --json` lists
the community themes on omarchy.org with their repository URLs. Fetched themes
then appear in `list` and can be applied like the others.

To open the picker for the user, run `cinnarchy open`.
