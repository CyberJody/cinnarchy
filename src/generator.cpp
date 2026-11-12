#include "generator.h"
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cmath>

namespace ThemeGenerator {
namespace {
const QString darkResource = "resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained-dark.css";
const QString lightResource = "resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained.css";

bool isHex(const QString &value) {
    static const QRegularExpression hex("^#[0-9a-fA-F]{6}$");
    return hex.match(value).hasMatch();
}
struct Rgb { int r = 0, g = 0, b = 0; };
Rgb rgb(const QString &hex) {
    const QColor c(hex);
    return {c.red(), c.green(), c.blue()};
}
QString hexOf(const Rgb &c) { return QString::asprintf("#%02x%02x%02x", c.r, c.g, c.b); }

// Matches the bundled themes: IEEE double, rounded half to even.
Rgb mix(const Rgb &a, const Rgb &b, double t) {
    auto channel = [t](int x, int y) { return int(std::nearbyint(x * (1 - t) + y * t)); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}
// Omarchy's mix_color: rounded half up.
QString omarchyMix(const QString &start, const QString &end, double amount) {
    const Rgb a = rgb(start), b = rgb(end);
    auto channel = [amount](int x, int y) { return int(std::floor(x * (1 - amount) + y * amount + .5)); };
    return hexOf({channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)});
}
double luminance(const Rgb &c) {
    auto linear = [](int v) { const double s = v / 255.0; return s <= .04045 ? s / 12.92 : std::pow((s + .055) / 1.055, 2.4); };
    return .2126 * linear(c.r) + .7152 * linear(c.g) + .0722 * linear(c.b);
}
double contrast(const Rgb &a, const Rgb &b) {
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
}

Palette parseColorsToml(const QByteArray &text) {
    Palette result;
    static const QRegularExpression validKey("^[A-Za-z0-9_-]+$");
    for (const auto &raw : QString::fromUtf8(text).split('\n')) {
        const int split = raw.indexOf('=');
        if (split < 0) continue;
        QString key = raw.left(split);
        key.remove('"').remove('\'').remove(' ').remove('\t');
        if (key.isEmpty() || key.startsWith('#') || !validKey.match(key).hasMatch()) continue;
        QString value = raw.mid(split + 1);
        const int quote = value.indexOf(QRegularExpression("[\"']"));
        if (quote >= 0) {
            value = value.mid(quote + 1);
            const int end = value.indexOf(QRegularExpression("[\"']"));
            if (end >= 0) value = value.left(end);
        } else {
            value = value.trimmed();
        }
        result[key] = value;
    }
    return result;
}

namespace {
// Port of bin/omarchy-theme-colors-from-alacritty.
Palette fromAlacritty(const QByteArray &text) {
    QMap<QString, QString> direct, dotted;
    QString section;
    static const QRegularExpression sectionLine(R"(^\[([^\]]*)\]\s*$)");
    static const QRegularExpression quoted(R"re(^["'](0[xX]|#)?[0-9a-fA-F]{6}(["'](\s*#.*)?)?$)re");
    static const QRegularExpression bare(R"(^(0[xX])?[0-9a-fA-F]{6}(\s*#.*)?$)");
    static const QRegularExpression six("[0-9a-fA-F]{6}");
    for (const auto &raw : QString::fromUtf8(text).split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith('[')) {
            const auto match = sectionLine.match(line);
            section = match.hasMatch() ? match.captured(1) : QString();
            continue;
        }
        if (section.isEmpty()) continue;
        const int split = raw.indexOf('=');
        if (split < 0) continue;
        const QString key = raw.left(split).trimmed(), value = raw.mid(split + 1).trimmed();
        if (key.isEmpty() || key.startsWith('#')) continue;
        if (!quoted.match(value).hasMatch() && !bare.match(value).hasMatch()) continue;
        const QString hex = "#" + six.match(value).captured(0).toLower();
        const QString path = section + "." + key;
        auto &target = section == "colors" && key.contains('.') ? dotted : direct;
        if (!target.contains(path)) target[path] = hex;
    }
    for (auto it = dotted.begin(); it != dotted.end(); ++it)
        if (!direct.contains(it.key())) direct[it.key()] = it.value();
    const QStringList names{"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"};
    QStringList color(16);
    for (int i = 0; i < 8; ++i) {
        color[i] = direct.value("colors.normal." + names[i]);
        if (color[i].isEmpty()) return {};
    }
    for (int i = 0; i < 8; ++i) color[i + 8] = direct.value("colors.bright." + names[i], color[i]);
    const QString background = direct.value("colors.primary.background", color[0]);
    const QString foreground = direct.value("colors.primary.foreground", color[7]);
    color[0] = background;
    color[7] = foreground;
    Palette result{{"accent", color[4]}, {"selection", direct.value("colors.selection.background", foreground)},
                   {"background", background}, {"foreground", foreground}};
    for (int i = 0; i < 16; ++i) result["color" + QString::number(i)] = color[i];
    return result;
}
}

Palette readThemeColors(const QString &themeDir, QString &error) {
    QFile colors(themeDir + "/colors.toml"), alacritty(themeDir + "/alacritty.toml");
    Palette raw;
    if (colors.open(QIODevice::ReadOnly)) raw = parseColorsToml(colors.read(256 * 1024));
    else if (alacritty.open(QIODevice::ReadOnly)) raw = fromAlacritty(alacritty.read(256 * 1024));
    if (raw.isEmpty()) {
        error = QObject::tr("%1 has no usable colors.toml or alacritty.toml.").arg(QFileInfo(themeDir).fileName());
        return {};
    }
    return resolve(raw, QFileInfo::exists(themeDir + "/light.mode"));
}

// Port of resolve_theme_colors in Omarchy's bin/omarchy-theme-color.
Palette resolve(const Palette &raw, bool lightModeFile) {
    Palette c = raw;
    auto has = [&](const QString &k) { return !c.value(k).isEmpty(); };
    auto alias = [&](const QString &k, const QString &fallback) { if (!has(k)) c[k] = c.value(fallback); };
    const QList<QPair<QString, QString>> legacyPalette{
        {"background", "bg"}, {"dark_background", "dark_bg"}, {"darker_background", "darker_bg"},
        {"lighter_background", "lighter_bg"}, {"foreground", "fg"}, {"dark_foreground", "dark_fg"},
        {"light_foreground", "light_fg"}, {"bright_foreground", "bright_fg"}};
    for (const auto &[key, shortName] : legacyPalette) alias(key, shortName);
    alias("background", "color0");
    alias("foreground", "color7");
    if (has("background")) c["color0"] = c["background"];
    if (has("foreground")) c["color7"] = c["foreground"];
    const QList<QPair<QString, QString>> legacy{
        {"red", "color1"}, {"green", "color2"}, {"yellow", "color3"}, {"blue", "color4"}, {"magenta", "color5"},
        {"cyan", "color6"}, {"bright_red", "color9"}, {"bright_green", "color10"}, {"bright_yellow", "color11"},
        {"bright_blue", "color12"}, {"bright_magenta", "color13"}, {"bright_cyan", "color14"}};
    for (const auto &[key, ansi] : legacy) alias(key, ansi);
    alias("magenta", "purple");
    alias("bright_magenta", "bright_purple");
    auto either = [&](const QString &a, const QString &b) { return has(a) ? c.value(a) : c.value(b); };
    if (!has("light_foreground")) c["light_foreground"] = either("color7", "foreground");
    if (!has("bright_foreground")) c["bright_foreground"] = either("color15", "foreground");
    if (!has("lighter_background")) c["lighter_background"] = either("color0", "background");
    if (!has("dark_foreground")) c["dark_foreground"] = either("color8", "foreground");
    if (!has("muted")) c["muted"] = either("color8", "dark_foreground");
    if (!has("selection"))
        c["selection"] = has("selection_background") ? c["selection_background"] : either("color8", has("color0") ? "color0" : "background");
    alias("orange", "yellow");
    auto derive = [&](const QString &key, const QString &from, const QString &to, double amount) {
        if (!has(key) && isHex(c.value(from))) c[key] = omarchyMix(c.value(from), to, amount);
    };
    derive("brown", "orange", "#000000", .5);
    derive("dark_background", "background", "#000000", .25);
    derive("darker_background", "background", "#000000", .5);
    for (const auto &key : {"red", "yellow", "green", "cyan", "blue", "magenta"})
        derive(QString("bright_") + key, key, "#ffffff", .2);
    alias("accent", "color4");
    alias("accent", "blue");

    QString mode = has("mode") ? c["mode"] : c.value("theme_type");
    if (mode.isEmpty()) {
        if (lightModeFile) mode = "light";
        else if (isHex(c.value("background"))) {
            const Rgb bg = rgb(c["background"]);
            mode = bg.r + bg.g + bg.b > 382 ? "light" : "dark";
        } else mode = "dark";
    }
    c["mode"] = mode == "light" ? "light" : "dark";
    for (auto it = c.begin(); it != c.end();) {
        if (it.key() == "mode") { ++it; continue; }
        if (isHex(it.value())) { it.value() = it.value().toLower(); ++it; }
        else it = c.erase(it);
    }
    return c;
}

QJsonObject roles(const Palette &p, QString &error) {
    for (const auto &key : {"background", "foreground", "accent", "lighter_background", "dark_background",
                            "darker_background", "selection", "red", "yellow", "green"}) {
        if (!isHex(p.value(key))) {
            error = QObject::tr("The palette lacks a %1 color.").arg(key);
            return {};
        }
    }
    const Rgb bg = rgb(p["background"]), fg = rgb(p["foreground"]), accent = rgb(p["accent"]), surface = rgb(p["lighter_background"]);
    const bool readable = contrast(accent, bg) >= 4.5;
    const Rgb black{0, 0, 0}, white{255, 255, 255};
    const Rgb onAccent = readable ? bg : contrast(white, accent) > contrast(black, accent) ? white : black;
    const QString mode = p.value("mode") == "light" ? "light" : "dark";
    return {{"bg", hexOf(bg)}, {"fg", hexOf(fg)}, {"accent", hexOf(accent)}, {"surface", hexOf(surface)},
            {"panel", p["dark_background"]}, {"deep", p["darker_background"]},
            {"hover", hexOf(mix(surface, fg, .10))}, {"pressed", hexOf(mix(surface, fg, .18))},
            {"border", hexOf(mix(bg, fg, .22))}, {"muted", hexOf(mix(bg, fg, .60))},
            {"selection", p["selection"]}, {"on_selection", hexOf(fg)}, {"on_accent", hexOf(onAccent)},
            {"link", hexOf(readable ? accent : mix(accent, fg, .55))},
            {"error", p["red"]}, {"warning", p["yellow"]}, {"success", p["green"]},
            {"mode", mode}, {"gtk_resource", mode == "light" ? lightResource : darkResource}};
}

QByteArray render(const QByteArray &templ, const QJsonObject &r, const QString &name, const QString &title) {
    static const QRegularExpression token(R"(\{\{([a-z_]+)(?::([a-z_]+))?(?::([a-z_]+):([0-9.]+))?\}\})");
    const QString text = QString::fromUtf8(templ);
    QString out;
    out.reserve(text.size());
    qsizetype last = 0;
    for (auto it = token.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const QString key = m.captured(1);
        if (key == "mix") out += hexOf(mix(rgb(r[m.captured(2)].toString()), rgb(r[m.captured(3)].toString()), m.captured(4).toDouble()));
        else if (key == "name") out += name;
        else if (key == "title") out += title;
        else if (m.captured(2) == "rgb") {
            const Rgb c = rgb(r[key].toString());
            out += QString("%1, %2, %3").arg(c.r).arg(c.g).arg(c.b);
        } else out += r[key].toString();
    }
    out += text.mid(last);
    return out.toUtf8();
}

QString titleFor(const QString &slug) {
    QStringList words = slug.split('-', Qt::SkipEmptyParts);
    for (auto &w : words) w[0] = w[0].toUpper();
    return words.join(' ');
}
QString slugFor(const QString &omarchyName) {
    QString slug = omarchyName.toLower();
    slug.replace(QRegularExpression("[^a-z0-9]+"), "-");
    slug.replace(QRegularExpression("^-+|-+$"), "");
    return slug;
}
bool validSlug(const QString &slug) {
    static const QRegularExpression valid("^[a-z0-9]+(-[a-z0-9]+)*$");
    return slug.size() <= 64 && valid.match(slug).hasMatch();
}

bool generate(const QString &themeRoot, const QString &slug, const QString &title,
              const Palette &palette, const QJsonObject &source, QString &error) {
    if (!validSlug(slug)) { error = QObject::tr("Invalid theme name: %1").arg(slug); return false; }
    const QString name = "Omarchy-" + slug, target = themeRoot + "/" + name;
    if (QDir(":/themes/" + name).exists()) { error = QObject::tr("%1 is a bundled theme.").arg(name); return false; }
    if (QFileInfo(target).isSymLink()) { error = QObject::tr("The theme directory is a symbolic link: %1").arg(target); return false; }
    if (QDir(target).exists() && !QFileInfo::exists(target + "/.omarchy-cinnamon")) {
        error = QObject::tr("An unrelated folder already uses this theme name: %1").arg(target);
        return false;
    }
    const QJsonObject r = roles(palette, error);
    if (r.isEmpty()) return false;
    if (!QDir().mkpath(themeRoot)) { error = QObject::tr("Cannot create the theme directory: %1").arg(themeRoot); return false; }
    QTemporaryDir temp(themeRoot + "/.cinnarchy-XXXXXX");
    if (!temp.isValid()) { error = QObject::tr("Cannot stage the theme files."); return false; }
    auto save = [&](const QString &relative, const QByteArray &bytes) {
        const QString path = temp.path() + "/" + relative;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile out(path);
        return out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() && out.commit();
    };
    QDirIterator files(":/template", QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = files.next(), relative = path.mid(QString(":/template/").size());
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) { error = QObject::tr("Cannot copy theme files."); return false; }
        const QByteArray bytes = input.readAll();
        const bool text = relative.endsWith(".css") || relative.endsWith(".svg") || relative.endsWith(".xml")
                          || relative.endsWith("gtkrc") || relative == "index.theme";
        if (!save(relative, text ? render(bytes, r, name, title) : bytes)) { error = QObject::tr("Cannot save theme files."); return false; }
    }
    QJsonObject colors;
    for (auto it = palette.begin(); it != palette.end(); ++it) colors[it.key()] = it.value();
    const QJsonObject meta{{"slug", slug}, {"title", title}, {"palette", colors}, {"roles", r}, {"source", source}};
    if (!save("palette.json", QJsonDocument(meta).toJson())) { error = QObject::tr("Cannot save theme files."); return false; }
    QString previous;
    if (QDir(target).exists()) {
        previous = target + ".old-" + QDateTime::currentDateTimeUtc().toString("yyyyMMddhhmmsszzz");
        if (!QDir().rename(target, previous)) { error = QObject::tr("Cannot replace the existing theme."); return false; }
    }
    if (!QDir().rename(temp.path(), target)) {
        if (!previous.isEmpty()) QDir().rename(previous, target);
        error = QObject::tr("Cannot install the theme.");
        return false;
    }
    temp.setAutoRemove(false);
    if (!previous.isEmpty()) QDir(previous).removeRecursively();
    return true;
}
}
