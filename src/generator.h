#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>

// Builds a Cinnamon/GTK theme folder from an Omarchy colors.toml palette.
// The template under :/template/ was extracted from the bundled themes by
// bin/make-theme-template; filling it with a bundled palette reproduces that
// bundled theme byte for byte.
namespace ThemeGenerator {
using Palette = QMap<QString, QString>;

// key = "value" pairs from colors.toml, as Omarchy's omarchy-theme-color reads them.
Palette parseColorsToml(const QByteArray &text);
// Colours from an Omarchy theme folder: colors.toml, else alacritty.toml.
Palette readThemeColors(const QString &themeDir, QString &error);
// Omarchy's alias and fallback cascade (bin/omarchy-theme-color), including mode.
Palette resolve(const Palette &raw, bool lightModeFile);
// Cinnamon roles for a resolved palette; empty with error when keys are missing.
QJsonObject roles(const Palette &palette, QString &error);
QByteArray render(const QByteArray &templ, const QJsonObject &roles, const QString &name, const QString &title);

QString titleFor(const QString &slug);
// Omarchy theme names allow [a-z0-9_][a-z0-9._+-]*; Cinnamon slugs are kebab-case.
QString slugFor(const QString &omarchyName);
bool validSlug(const QString &slug);

// Writes <themeRoot>/Omarchy-<slug> atomically. An existing folder is replaced
// only when this generator owns it (its marker) and it is not a bundled theme.
bool generate(const QString &themeRoot, const QString &slug, const QString &title,
              const Palette &palette, const QJsonObject &source, QString &error);
}
