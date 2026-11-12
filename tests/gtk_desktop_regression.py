#!/usr/bin/env python3
"""Check real GTK style resolution for Nemo desktop and application nodes."""
import argparse
from pathlib import Path
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk


def context(provider, nodes):
    path = Gtk.WidgetPath()
    for widget_type, name, classes in nodes:
        index = path.append_type(widget_type)
        path.iter_set_object_name(index, name)
        for css_class in classes:
            path.iter_add_class(index, css_class)
    ctx = Gtk.StyleContext()
    ctx.set_path(path)
    ctx.add_provider(provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
    return ctx


def check(root):
    if not Gtk.init_check()[0]:
        raise RuntimeError('A graphical display is needed to resolve GTK styles')
    themes = sorted(root.glob('Omarchy-*/gtk-3.0/gtk.css'))
    assert themes, f'No themes found in {root}'
    checks = 0
    for css in themes:
        provider = Gtk.CssProvider()
        provider.load_from_path(str(css.resolve()))
        for desktop_class in ('nemo-desktop-window', 'nemo-blank-desktop-window'):
            ctx = context(provider, [(Gtk.Window, 'window', ['background', 'nemo-window', desktop_class])])
            for state in (Gtk.StateFlags.NORMAL, Gtk.StateFlags.BACKDROP):
                ctx.set_state(state)
                color = ctx.get_property('background-color', state)
                assert color.alpha == 0, (css.parent.parent.name, desktop_class, state, color.to_string())
                checks += 1
        desktop = (Gtk.Window, 'window', ['background', 'nemo-window', 'nemo-desktop-window'])
        for nodes in ([desktop, (Gtk.Box, 'box', ['nemo-desktop', 'view'])],
                      [desktop, (Gtk.Notebook, 'notebook', []), (Gtk.Stack, 'stack', [])]):
            ctx = context(provider, nodes)
            for state in (Gtk.StateFlags.NORMAL, Gtk.StateFlags.BACKDROP):
                ctx.set_state(state)
                assert ctx.get_property('background-color', state).alpha == 0, (css, nodes, state)
                checks += 1
        # File-manager windows still need an opaque application background.
        ctx = context(provider, [(Gtk.Window, 'window', ['background', 'nemo-window'])])
        for state in (Gtk.StateFlags.NORMAL, Gtk.StateFlags.BACKDROP):
            ctx.set_state(state)
            assert ctx.get_property('background-color', state).alpha == 1, (css, state)
            checks += 1
    print(f'PASS: {len(themes)} themes, {checks} GTK desktop-transparency and normal-window checks')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--themes-root', type=Path, default=Path(__file__).resolve().parents[1] / 'data/themes')
    args = parser.parse_args()
    check(args.themes_root)
