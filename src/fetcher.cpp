#include "fetcher.h"
#include "generator.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

namespace ThemeFetch {
namespace {
const QString marker = ".cinnarchy-source";

QSet<QString> bundledSlugs() {
    QSet<QString> slugs;
    QFile manifest(":/themes/manifest.json");
    if (manifest.open(QIODevice::ReadOnly))
        for (const auto &v : QJsonDocument::fromJson(manifest.readAll()).array()) slugs << v.toObject().value("slug").toString();
    return slugs;
}
QJsonObject readJson(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}
bool writeJson(const QString &path, const QJsonObject &value) {
    QSaveFile f(path);
    const QByteArray bytes = QJsonDocument(value).toJson();
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

// git never prompts here, and a clone writes no symbolic links: a theme is
// only colours, images and an icon name.
bool git(const QStringList &args, QString &error, QString *output = nullptr, int timeoutMs = 600000) {
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("GIT_TERMINAL_PROMPT", "0");
    env.insert("GIT_ASKPASS", "true");
    env.insert("SSH_ASKPASS", "true");
    env.insert("GIT_SSH_COMMAND", "ssh -oBatchMode=yes");
    env.insert("LC_ALL", "C");
    process.setProcessEnvironment(env);
    process.start("git", QStringList{"-c", "core.symlinks=false", "-c", "protocol.ext.allow=never",
                                     "-c", "protocol.fd.allow=never", "-c", "advice.detachedHead=false"} + args);
    if (!process.waitForStarted(10000)) {
        error = QObject::tr("git is not installed. Install git to fetch themes.");
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        error = QObject::tr("Downloading themes timed out.");
        return false;
    }
    if (output) *output = QString::fromUtf8(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed().split('\n').last();
        error = QObject::tr("git failed: %1").arg(detail.isEmpty() ? args.value(0) : detail);
        return false;
    }
    return true;
}

// Regular files and folders only, without the repository metadata.
bool copyTree(const QString &from, const QString &to, QString &error) {
    QDirIterator it(from, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System, QDirIterator::Subdirectories);
    if (!QDir().mkpath(to)) { error = QObject::tr("Cannot create %1").arg(to); return false; }
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = QDir(from).relativeFilePath(path);
        if (relative == ".git" || relative.startsWith(".git/") || info.isSymLink()) continue;
        if (info.isDir()) { QDir().mkpath(to + "/" + relative); continue; }
        if (!info.isFile()) continue;
        if (!QFile::copy(path, to + "/" + relative)) { error = QObject::tr("Cannot copy %1").arg(relative); return false; }
    }
    return true;
}

// Replace <userThemeRoot>/<slug> with a staged copy of source.
bool installFolder(const Paths &paths, const QString &slug, const QString &source, const QJsonObject &origin, QString &error) {
    const QString target = paths.userThemeRoot + "/" + slug;
    QTemporaryDir stage(paths.userThemeRoot + "/.cinnarchy-stage-XXXXXX");
    if (!stage.isValid()) { error = QObject::tr("Cannot stage the theme files."); return false; }
    const QString staged = stage.path() + "/" + slug;
    if (!copyTree(source, staged, error)) return false;
    if (!writeJson(staged + "/" + marker, origin)) { error = QObject::tr("Cannot save theme files."); return false; }
    const QString previous = stage.path() + "/previous";
    if (QFileInfo::exists(target) && !QDir().rename(target, previous)) { error = QObject::tr("Cannot replace the existing theme."); return false; }
    if (!QDir().rename(staged, target)) {
        if (QFileInfo::exists(previous)) QDir().rename(previous, target);
        error = QObject::tr("Cannot install the theme.");
        return false;
    }
    return true;
}

bool generateFor(const Paths &paths, const QString &slug, const QString &themeDir, const QJsonObject &origin, QString &error) {
    const auto palette = ThemeGenerator::readThemeColors(themeDir, error);
    if (palette.isEmpty()) return false;
    return ThemeGenerator::generate(paths.themeRoot, slug, ThemeGenerator::titleFor(slug), palette, origin, error);
}

bool hasCinnamonTheme(const Paths &paths, const QString &slug) {
    return QFileInfo::exists(paths.themeRoot + "/Omarchy-" + slug + "/palette.json");
}
bool installedAnywhere(const Paths &paths, const QString &slug) {
    for (const auto &root : paths.stockRoots)
        if (QFileInfo(root + "/" + slug).isDir()) return true;
    return QFileInfo(paths.userThemeRoot + "/" + slug).isDir();
}
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }
}

QJsonObject Result::toJson() const {
    return {{"ok", ok}, {"error", error}, {"added", QJsonArray::fromStringList(added)}, {"updated", QJsonArray::fromStringList(updated)},
            {"generated", QJsonArray::fromStringList(generated)}, {"skipped", QJsonArray::fromStringList(skipped)}};
}

bool safeUrl(const QString &url, QString &error) {
    static const QRegularExpression helper("^[A-Za-z0-9][A-Za-z0-9+.-]*::");
    static const QRegularExpression scheme("^([A-Za-z0-9][A-Za-z0-9+.-]*)://");
    static const QStringList transports{"ssh", "git", "git+ssh", "ssh+git", "http", "https", "ftp", "ftps", "file"};
    if (url.isEmpty()) { error = QObject::tr("Enter a git repository URL."); return false; }
    if (url.startsWith('-') || helper.match(url).hasMatch() || url.contains(QRegularExpression("[\\s\\x00-\\x1f]"))) {
        error = QObject::tr("'%1' is not a repository URL.").arg(url);
        return false;
    }
    const auto match = scheme.match(url);
    if (match.hasMatch() && !transports.contains(match.captured(1).toLower())) {
        error = QObject::tr("Themes cannot be cloned over '%1'.").arg(match.captured(1));
        return false;
    }
    return true;
}

// Mirrors omarchy-theme-install: basename without .git, omarchy- and -theme.
QString nameFromUrl(const QString &url) {
    QString path = url;
    while (path.endsWith('/')) path.chop(1);
    if (!path.contains("://") && path.contains(':') && !path.left(path.indexOf(':')).contains('/')) path = path.mid(path.indexOf(':') + 1);
    QString name = path.mid(path.lastIndexOf('/') + 1);
    if (name.endsWith(".git")) name.chop(4);
    name.remove(QRegularExpression("^omarchy-"));
    name.remove(QRegularExpression("-theme$"));
    name = name.toLower();
    static const QRegularExpression valid("^[a-z0-9_][a-z0-9._+-]*$");
    return valid.match(name).hasMatch() ? name : QString();
}

Result generateMissing(const Paths &paths) {
    Result result;
    const auto bundled = bundledSlugs();
    QStringList roots = paths.stockRoots;
    if (!roots.contains(paths.userThemeRoot)) roots << paths.userThemeRoot;
    QSet<QString> seen;
    for (const auto &root : roots) {
        for (const auto &name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!ThemeGenerator::validSlug(name) || bundled.contains(name) || seen.contains(name) || hasCinnamonTheme(paths, name)) continue;
            seen << name;
            QString error;
            const auto origin = readJson(root + "/" + name + "/" + marker);
            if (generateFor(paths, name, root + "/" + name, origin.isEmpty() ? QJsonObject{{"path", root + "/" + name}} : origin, error))
                result.generated << name;
            else
                result.skipped << name + ": " + error;
        }
    }
    return result;
}

Result syncOfficial(const Paths &paths, const QString &repo) {
    Result result;
    auto failed = [&](const QString &error) { result.ok = false; result.error = error; return result; };
    if (!QDir().mkpath(paths.userThemeRoot)) return failed(QObject::tr("Cannot create %1").arg(paths.userThemeRoot));
    QTemporaryDir temp(paths.userThemeRoot + "/.cinnarchy-fetch-XXXXXX");
    if (!temp.isValid()) return failed(QObject::tr("Cannot stage the theme files."));
    const QString clone = temp.path() + "/omarchy";
    QString error, listing, commit;
    // A blobless sparse clone downloads only the folders of themes we need.
    if (!git({"clone", "--quiet", "--depth", "1", "--filter=blob:none", "--sparse", "--", repo, clone}, error)
        || !git({"-C", clone, "ls-tree", "-d", "--name-only", "HEAD", "themes/"}, error, &listing)
        || !git({"-C", clone, "rev-parse", "HEAD"}, error, &commit))
        return failed(error);
    commit = commit.trimmed();
    const auto bundled = bundledSlugs();
    QStringList wanted;
    QHash<QString, QString> trees;
    for (const auto &line : listing.split('\n', Qt::SkipEmptyParts)) {
        const QString name = line.mid(QString("themes/").size());
        if (!ThemeGenerator::validSlug(name)) { result.skipped << name; continue; }
        if (bundled.contains(name)) continue;
        QString tree;
        if (!git({"-C", clone, "rev-parse", "HEAD:themes/" + name}, error, &tree)) return failed(error);
        trees[name] = tree.trimmed();
        const auto origin = readJson(paths.userThemeRoot + "/" + name + "/" + marker);
        if (!origin.isEmpty()) {
            if (origin.value("tree").toString() == trees[name] && hasCinnamonTheme(paths, name)) continue;
            wanted << name;
        } else if (!installedAnywhere(paths, name)) {
            wanted << name;
        }
    }
    if (!wanted.isEmpty()) {
        QStringList folders;
        for (const auto &name : wanted) folders << "themes/" + name;
        if (!git(QStringList{"-C", clone, "sparse-checkout", "set", "--"} + folders, error)) return failed(error);
    }
    for (const auto &name : wanted) {
        const QString source = clone + "/themes/" + name;
        const bool update = QFileInfo::exists(paths.userThemeRoot + "/" + name + "/" + marker);
        const QJsonObject origin{{"repository", repo}, {"commit", commit}, {"path", "themes/" + name},
                                 {"tree", trees[name]}, {"fetched", now()}};
        QString problem;
        if (ThemeGenerator::readThemeColors(source, problem).isEmpty()
            || !installFolder(paths, name, source, origin, problem)
            || !generateFor(paths, name, paths.userThemeRoot + "/" + name, origin, problem)) {
            result.skipped << name + ": " + problem;
            continue;
        }
        (update ? result.updated : result.added) << name;
    }
    const auto local = generateMissing(paths);
    result.generated += local.generated;
    result.skipped += local.skipped;
    return result;
}

Result installRepository(const Paths &paths, const QString &url) {
    Result result;
    auto failed = [&](const QString &error) { result.ok = false; result.error = error; return result; };
    QString error;
    if (!safeUrl(url, error)) return failed(error);
    const QString slug = ThemeGenerator::slugFor(nameFromUrl(url));
    if (!ThemeGenerator::validSlug(slug)) return failed(QObject::tr("'%1' does not give a usable theme name.").arg(url));
    if (bundledSlugs().contains(slug)) return failed(QObject::tr("%1 is already included with the picker.").arg(ThemeGenerator::titleFor(slug)));
    const QString target = paths.userThemeRoot + "/" + slug;
    if (QFileInfo(target).isSymLink()) return failed(QObject::tr("The theme directory is a symbolic link: %1").arg(target));
    for (const auto &root : paths.stockRoots) {
        // Installed by Omarchy or by hand: build its Cinnamon theme, leave it alone.
        if (root != paths.userThemeRoot && QFileInfo(root + "/" + slug).isDir()) {
            if (!generateFor(paths, slug, root + "/" + slug, {{"path", root + "/" + slug}}, error)) return failed(error);
            result.generated << slug;
            return result;
        }
    }
    if (QFileInfo(target).isDir() && !QFileInfo::exists(target + "/" + marker)) {
        if (!generateFor(paths, slug, target, {{"path", target}}, error)) return failed(error);
        result.generated << slug;
        return result;
    }
    if (!QDir().mkpath(paths.userThemeRoot)) return failed(QObject::tr("Cannot create %1").arg(paths.userThemeRoot));
    QTemporaryDir temp(paths.userThemeRoot + "/.cinnarchy-fetch-XXXXXX");
    if (!temp.isValid()) return failed(QObject::tr("Cannot stage the theme files."));
    const QString clone = temp.path() + "/repo";
    QString commit;
    if (!git({"clone", "--quiet", "--depth", "1", "--", url, clone}, error) || !git({"-C", clone, "rev-parse", "HEAD"}, error, &commit))
        return failed(error);
    if (ThemeGenerator::readThemeColors(clone, error).isEmpty()) return failed(error);
    const bool update = QFileInfo::exists(target);
    const QJsonObject origin{{"repository", url}, {"commit", commit.trimmed()}, {"fetched", now()}};
    if (!installFolder(paths, slug, clone, origin, error) || !generateFor(paths, slug, target, origin, error)) return failed(error);
    (update ? result.updated : result.added) << slug;
    return result;
}

QJsonArray parseCommunity(const QString &html, const QString &base) {
    static const QRegularExpression entry(R"re(<a href="([^"]+)"[^>]*>\s*<img src="([^"]*)"[^>]*>\s*<span[^>]*>([^<]+)</span>)re");
    auto text = [](QString s) {
        s.replace("&#x27;", "'").replace("&#39;", "'").replace("&quot;", "\"").replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
        return s.trimmed();
    };
    QJsonArray out;
    QSet<QString> seen;
    for (auto it = entry.globalMatch(html); it.hasNext();) {
        const auto m = it.next();
        const QString url = text(m.captured(1));
        QString error;
        const QString slug = ThemeGenerator::slugFor(nameFromUrl(url));
        if (!url.startsWith("https://") || !safeUrl(url, error) || !ThemeGenerator::validSlug(slug) || seen.contains(url)) continue;
        seen << url;
        out.append(QJsonObject{{"name", text(m.captured(3))}, {"url", url}, {"slug", slug},
                               {"image", QUrl(base).resolved(QUrl(text(m.captured(2)))).toString()}});
    }
    return out;
}

QJsonArray community(QString &error, const QString &catalog) {
    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(catalog)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "cinnarchy");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        error = QObject::tr("Cannot load the community theme list: %1").arg(reply->errorString());
        return {};
    }
    const auto list = parseCommunity(QString::fromUtf8(reply->read(16 * 1024 * 1024)), catalog);
    if (list.isEmpty()) error = QObject::tr("The community theme list could not be read.");
    return list;
}
}
