#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Downloads Omarchy themes and builds their Cinnamon themes. Everything here
// blocks; the picker runs it on a worker thread.
namespace ThemeFetch {
inline const QString officialRepo = "https://github.com/omacom/omarchy.git";
inline const QString communityCatalog = "https://omarchy.org/themes/";

struct Paths {
    QString themeRoot;      // generated Cinnamon themes (~/.themes)
    QString userThemeRoot;  // downloaded Omarchy themes (~/.config/omarchy/themes)
    QStringList stockRoots; // every place Omarchy themes are already installed
};

struct Result {
    bool ok = true;
    QString error;
    QStringList added, updated, generated, skipped;
    QStringList backgrounds; // bundled themes whose official backgrounds were downloaded
    QJsonObject toJson() const;
};

// Themes in the official repository that are not installed yet, plus any
// installed Omarchy theme that still lacks its Cinnamon theme.
Result syncOfficial(const Paths &paths, const QString &repo = officialRepo);
// A community theme from its git repository, like `omarchy theme install`.
Result installRepository(const Paths &paths, const QString &url);
// Cinnamon themes for installed Omarchy themes that have none.
Result generateMissing(const Paths &paths);
// Bundled themes with no backgrounds installed anywhere (no Omarchy on this system).
QStringList missingBackgrounds(const Paths &paths);
// Official backgrounds and icon choices for those themes; every one when slugs is empty.
Result downloadBackgrounds(const Paths &paths, const QStringList &slugs = {}, const QString &repo = officialRepo);
// Makes running downloads stop promptly, e.g. when the picker closes.
void requestCancel(bool cancel);

// Theme name Omarchy derives from a repository URL ("" when unusable).
QString nameFromUrl(const QString &url);
// Refuses option-like URLs and git transport helpers before anything runs.
bool safeUrl(const QString &url, QString &error);

// The community theme list from omarchy.org: [{name, url, image, slug}].
QJsonArray community(QString &error, const QString &catalog = communityCatalog);
QJsonArray parseCommunity(const QString &html, const QString &base);
}
