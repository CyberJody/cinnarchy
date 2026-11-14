#include "backend.h"
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QEventLoop>
#include <QImageReader>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QProcess>
#include <QSysInfo>

namespace {
const QString shell="org.cinnamon.theme", ui="org.cinnamon.desktop.interface";
const QString wm="org.cinnamon.desktop.wm.preferences", bg="org.cinnamon.desktop.background";
const QString slide="org.cinnamon.desktop.background.slideshow", portal="org.x.apps.portal";
const QList<QPair<QString,QString>> managed = {
    {shell,"name"},{ui,"gtk-theme"},{wm,"theme"},{ui,"icon-theme"},
    {bg,"picture-uri"},{bg,"picture-options"},{bg,"picture-opacity"},
    {bg,"primary-color"},{bg,"secondary-color"},{bg,"color-shading-type"},
    {slide,"slideshow-enabled"},{slide,"slideshow-paused"},{portal,"color-scheme"},{portal,"accent-rgb"},{"cinnarchy.wallpaper","configuration"}
};
QJsonObject readObject(const QString &path) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
QString stateBase() {
    return qEnvironmentVariable("XDG_STATE_HOME",QDir::homePath()+"/.local/state")+"/cinnarchy";
}
QStringList iconPaths() {
    QStringList result{QDir::homePath()+"/.icons",QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/icons"};
    for(const auto &path: QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) result<<path+"/icons";
    result.removeDuplicates(); return result;
}
}

PickerPaths PickerPaths::defaults() {
    return {QDir::homePath()+"/.themes",stateBase(),
        {"/usr/share/omarchy/themes",QDir::homePath()+"/.local/share/omarchy/themes",QDir::homePath()+"/.config/omarchy/themes"},
        {QDir::homePath()+"/.config/omarchy/backgrounds"},iconPaths(),{},true,QDir::homePath()+"/.config/omarchy/themes"};
}

Backend::Backend(QObject *parent, PickerPaths paths, SettingsStore *store,FilePicker *picker)
    : QObject(parent),m_paths(std::move(paths)),m_store(store) {
    if(!m_store) {m_ownedStore=makeSettingsStore();m_store=m_ownedStore.get();}
    // Downloaded themes must be found again, wherever they were told to go.
    if(!m_paths.userThemeRoot.isEmpty()&&!m_paths.stockRoots.contains(m_paths.userThemeRoot)) m_paths.stockRoots<<m_paths.userThemeRoot;
    m_picker=picker?picker:new PortalFilePicker(this);
    connect(m_picker,&FilePicker::chosen,this,&Backend::addMedia);
    connect(m_picker,&FilePicker::failed,this,&Backend::fail);
    m_displays=m_paths.displayOverrides.isEmpty()?Wallpapers::displays():m_paths.displayOverrides;
    loadState();
    m_assignments=currentConfiguration().value("assignments").toObject();
    reload();
    const auto current=unquote(m_store->read(bg,"picture-uri"));
    if(currentConfiguration().isEmpty()&&Wallpapers::supported(QUrl(current).toLocalFile())) {
        const auto fit=unquote(m_store->read(bg,"picture-options"));
        for(const auto &s:m_displays)m_assignments[s.toMap().value("id").toString()]=QJsonObject{{"url",current},{"fit",fit}};
        syncSelection();
    }
    const auto recovery=readObject(m_paths.stateRoot+"/pending.json").value("snapshot").toArray();
    if(!recovery.isEmpty()) {
        if(m_undo.isEmpty() || m_undo.last().toArray()!=recovery) m_undo.append(recovery);
        m_message=tr("An interrupted switch can be recovered with Undo.");
    }
    m_lastActive=activeName(); m_poll.setInterval(2000);
    connect(&m_poll,&QTimer::timeout,this,[this]{
        if(m_lastActive!=activeName()) {m_lastActive=activeName();emit stateChanged();}
        emit stateChanged();
        const auto service=Wallpapers::read(m_paths.stateRoot+"/wallpaper-status.json");
        if(hasAnimations()&&service.contains("ok")&&!service.value("ok").toBool()&&m_message!=service.value("error").toString())fail(service.value("error").toString());
    });
    m_poll.start();
}

Backend::~Backend() {
    // A download in progress is stopped rather than keeping the closed picker alive.
    if(m_worker) {ThemeFetch::requestCancel(true);m_worker->wait();ThemeFetch::requestCancel(false);delete m_worker;}
}
QString Backend::version() const {return QCoreApplication::applicationVersion();}
int Backend::themesWithoutBackgrounds() const {
    int count=0;
    for(const auto &t:m_themes) if(t.toMap().value("bundled").toBool()&&t.toMap().value("wallpapers").toList().isEmpty()) ++count;
    return count;
}
void Backend::downloadBackgrounds(bool everyTheme) {
    if(m_worker) return;
    const auto paths=fetchPaths();const QStringList slugs=everyTheme?QStringList():QStringList{m_slug};
    m_message=everyTheme?tr("Downloading Omarchy's backgrounds for every theme. This can take a few minutes…"):tr("Downloading Omarchy's backgrounds for %1…").arg(selected().value("title").toString());
    m_error=false;emit stateChanged();
    const QString nothing=tr("Omarchy has no backgrounds to download for this theme.");
    runFetch([this,paths,slugs,nothing,repo=m_paths.officialRepo]{
        auto result=ThemeFetch::downloadBackgrounds(paths,slugs,repo);
        // Say so plainly when Omarchy has nothing for this theme.
        if(result.ok&&result.backgrounds.isEmpty()&&result.skipped.isEmpty()){result.ok=false;result.error=nothing;}
        QMetaObject::invokeMethod(this,[this,result]{finishFetch(result);},Qt::QueuedConnection);
    });
}
ThemeFetch::Paths PickerPaths::fetchPaths() const {
    ThemeFetch::Paths paths{themeRoot,userThemeRoot.isEmpty()?stockRoots.value(stockRoots.size()-1):userThemeRoot,stockRoots};
    if(!paths.stockRoots.contains(paths.userThemeRoot)) paths.stockRoots<<paths.userThemeRoot;
    return paths;
}
QVariantList Backend::communityThemes() const {
    QSet<QString> installed;
    for(const auto &t:m_themes) installed<<t.toMap().value("slug").toString();
    QVariantList out;
    for(const auto &value:m_community){auto item=value.toObject().toVariantMap();item["installed"]=installed.contains(item.value("slug").toString());out<<item;}
    return out;
}
void Backend::runFetch(std::function<void()> work) {
    if(m_worker) return;
    m_worker=QThread::create(std::move(work));
    connect(m_worker,&QThread::finished,this,[this]{m_worker->deleteLater();m_worker=nullptr;emit fetchChanged();});
    m_worker->start();
    emit fetchChanged();
}
void Backend::fetchOfficial() {
    if(m_worker) return;
    const auto paths=fetchPaths();
    m_message=tr("Checking github.com/omacom/omarchy for new themes…");m_error=false;emit stateChanged();
    runFetch([this,paths,repo=m_paths.officialRepo]{const auto result=ThemeFetch::syncOfficial(paths,repo);QMetaObject::invokeMethod(this,[this,result]{finishFetch(result);},Qt::QueuedConnection);});
}
void Backend::installTheme(const QString &url) {
    if(m_worker) return;
    const auto paths=fetchPaths();const auto trimmed=url.trimmed();
    m_message=tr("Downloading %1…").arg(trimmed);m_error=false;emit stateChanged();
    runFetch([this,paths,trimmed]{const auto result=ThemeFetch::installRepository(paths,trimmed);QMetaObject::invokeMethod(this,[this,result]{finishFetch(result);},Qt::QueuedConnection);});
}
void Backend::loadCommunity() {
    if(m_worker) return;
    runFetch([this,catalog=m_paths.communityCatalog]{
        QString error;const auto list=ThemeFetch::community(error,catalog);
        QMetaObject::invokeMethod(this,[this,list,error]{
            m_community=list;if(list.isEmpty())fail(error);
            emit fetchChanged();emit fetchFinished();
        },Qt::QueuedConnection);
    });
}
// Untranslated: the issue is read by the maintainers.
QString Backend::reportUrl() const {
    QString cinnamon="Cinnamon unknown";
    QProcess process;process.start("cinnamon",{"--version"});
    if(process.waitForFinished(2000)&&process.exitCode()==0)cinnamon=QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    else process.kill();
    const QString session=qEnvironmentVariable("XDG_SESSION_TYPE","unknown");
    const QString system=QStringList{QSysInfo::prettyProductName(),cinnamon,"Qt "+QLibraryInfo::version().toString(),
        session+" session","theme "+(activeName().isEmpty()?QString("none"):activeName())}.join(" · ");
    QUrlQuery query;
    query.addQueryItem("template","bug_report.yml");
    query.addQueryItem("version",QCoreApplication::applicationVersion().isEmpty()?QString("unknown"):QCoreApplication::applicationVersion());
    query.addQueryItem("system",system);
    QUrl url(projectUrl+"/issues/new");url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}
void Backend::finishFetch(const ThemeFetch::Result &result) {
    reload();
    auto titles=[this](const QStringList &slugs){
        QStringList out;
        for(const auto &slug:slugs){QString title=slug;for(const auto &t:m_themes)if(t.toMap().value("slug").toString()==slug)title=t.toMap().value("title").toString();out<<title;}
        return out.join(", ");
    };
    if(!result.ok) fail(result.error);
    else {
        QStringList parts;
        if(!result.added.isEmpty()) parts<<tr("Added %1.").arg(titles(result.added));
        if(!result.updated.isEmpty()) parts<<tr("Updated %1.").arg(titles(result.updated));
        if(!result.generated.isEmpty()) parts<<tr("Prepared %1 for Cinnamon.").arg(titles(result.generated));
        if(!result.backgrounds.isEmpty()) parts<<tr("Downloaded backgrounds for %1.").arg(result.backgrounds.size()>3?tr("%n themes","",result.backgrounds.size()):titles(result.backgrounds));
        const bool nothing=parts.isEmpty();
        if(nothing) parts<<tr("No new themes found. You have every official Omarchy theme.");
        if(!result.skipped.isEmpty()) parts<<tr("Skipped: %1").arg(result.skipped.join("; "));
        m_message=parts.join(' ');m_error=nothing&&!result.skipped.isEmpty();
        const auto first=!result.added.isEmpty()?result.added.first():!result.updated.isEmpty()?result.updated.first():result.generated.value(0);
        if(!first.isEmpty()) selectTheme(first);
        // Show the selected theme's own background now that it has one.
        if(result.backgrounds.contains(m_slug)) selectWallpaper(0);
    }
    emit stateChanged();emit fetchChanged();emit fetchFinished();
}

QVariantMap Backend::selected() const {
    for(const auto &t:m_themes) if(t.toMap().value("slug").toString()==m_slug) return t.toMap();
    return {};
}
QString Backend::activeName() const {return unquote(m_store->read(shell,"name"));}
QString Backend::wallpaperUrl() const {return m_selectedUrl;}
QString Backend::wallpaperName() const {return wallpaperMedia().value("title").toString();}
QVariantMap Backend::wallpaperMedia() const {
    for(const auto &t:m_themes)for(const auto &m:t.toMap().value("wallpapers").toList())if(m.toMap().value("url").toString()==m_selectedUrl)return m.toMap();
    return m_selectedUrl.isEmpty()?QVariantMap():Wallpapers::media(QUrl(m_selectedUrl).toLocalFile(),m_paths.stateRoot);
}
void Backend::setPictureOption(const QString &v) {
    if(!QStringList{"zoom","scaled","spanned","centered"}.contains(v))return;
    if(v=="spanned"&&m_target!="all")return;
    m_pictureOption=v;
    for(const auto &s:m_displays){const auto id=s.toMap().value("id").toString();if(m_target!="all"&&m_target!=id)continue;auto choice=m_assignments.value(id).toObject();choice["fit"]=v;if(v=="spanned")choice["url"]=m_selectedUrl;m_assignments[id]=choice;}
    emit optionsChanged();emit selectionChanged();
}
QJsonObject Backend::withPlayback(QJsonObject choice) const {
    // Only non-default choices are written, so ordinary configurations stay as they were.
    if(m_playback.speed==1.0)choice.remove("speed");else choice["speed"]=m_playback.speed;
    if(m_playback.replay==0)choice.remove("replay");else choice["replay"]=m_playback.replay;
    return choice;
}
void Backend::setPlaybackSpeed(double speed) {
    if(!Playback::validSpeed(speed)||speed==m_playback.speed)return;
    m_playback.speed=speed;storePlayback();
}
void Backend::setReplayDelay(int seconds) {
    if(!Playback::validReplay(seconds)||seconds==m_playback.replay)return;
    m_playback.replay=seconds;storePlayback();
}
void Backend::storePlayback() {
    QStringList targets;
    for(const auto &s:m_displays){const auto id=s.toMap().value("id").toString();if(m_target=="all"||m_target==id)targets<<id;}
    for(const auto &id:targets){const auto choice=m_assignments.value(id).toObject();if(!choice.isEmpty())m_assignments[id]=withPlayback(choice);}
    emit optionsChanged();
    // A background that is playing on the desktop right now follows at once, like Pause.
    auto config=currentConfiguration();
    if(!config.value("active").toBool()||!config.value("animated").toBool())return;
    auto live=config.value("assignments").toObject();bool changed=false;
    for(const auto &id:targets) {
        const auto choice=live.value(id).toObject();
        if(choice.isEmpty()||choice.value("url")!=m_assignments.value(id).toObject().value("url"))continue;
        const auto tuned=withPlayback(choice);if(tuned!=choice){live[id]=tuned;changed=true;}
    }
    if(!changed)return;
    config["assignments"]=live;QString error;
    if(!Wallpapers::activate(config,m_paths.stateRoot,m_paths.wallpaperRuntime,error)){fail(error);return;}
    emit stateChanged();
}
void Backend::chooseUrl(const QString &url) {
    m_selectedUrl=url;
    for(const auto &s:m_displays){const auto id=s.toMap().value("id").toString();if(m_target=="all"||m_target==id)m_assignments[id]=withPlayback(QJsonObject{{"url",url},{"fit",m_pictureOption}});}
    syncSelection();
}
void Backend::syncSelection() {
    const auto id=m_target=="all"?m_displays.value(0).toMap().value("id").toString():m_target;
    const auto choice=m_assignments.value(id).toObject();
    if(!choice.value("url").toString().isEmpty()){m_selectedUrl=choice.value("url").toString();m_playback=Playback::from(choice);}
    m_pictureOption=choice.value("fit").toString(m_pictureOption);m_wallpaperIndex=-1;
    const auto list=selected().value("wallpapers").toList();for(int i=0;i<list.size();++i)if(list[i].toMap().value("url").toString()==m_selectedUrl)m_wallpaperIndex=i;
    emit selectionChanged();emit optionsChanged();
}
void Backend::setTargetDisplay(const QString &id) {
    if(m_busy)return;
    bool valid=id=="all";for(const auto &s:m_displays)valid|=s.toMap().value("id").toString()==id;
    if(valid){m_target=id;syncSelection();if(id!="all"&&m_pictureOption=="spanned")setPictureOption("zoom");}
}
QVariantList Backend::displayAssignments() const {
    QVariantList out;
    for(const auto &s:m_displays){auto item=s.toMap();const auto choice=m_assignments.value(item.value("id").toString()).toObject();const auto media=Wallpapers::media(QUrl(choice.value("url").toString()).toLocalFile(),m_paths.stateRoot);item["title"]=media.value("title");item["thumbnail"]=media.value("thumbnail");item["animated"]=media.value("animated");item["url"]=choice.value("url").toString();out<<item;}
    return out;
}
QJsonObject Backend::currentConfiguration() const {
    const auto config=Wallpapers::read(Wallpapers::configurationPath(m_paths.stateRoot));
    return config.value("poster").toString()==unquote(m_store->read(bg,"picture-uri"))?config:QJsonObject();
}
bool Backend::hasAnimations() const{return currentConfiguration().value("active").toBool()&&currentConfiguration().value("animated").toBool();}
bool Backend::animationsPaused() const{return currentConfiguration().value("paused").toBool();}
void Backend::chooseMedia(){m_picker->choose(m_window?"x11:"+QString::number(m_window->winId(),16):QString());}
void Backend::addMedia(const QStringList &urls) {
    auto added=m_added.value(m_slug).toArray();QString selected;
    for(const auto &value:urls){const QUrl url(value);const QString path=url.isLocalFile()?url.toLocalFile():QFileInfo(value).absoluteFilePath();
        if(!Wallpapers::supported(path)){fail(tr("Unsupported or missing background: %1").arg(value));continue;}
        if(Wallpapers::isVideo(path)){QString problem;if(Wallpapers::poster(path,m_paths.stateRoot,problem).isEmpty()){fail(problem);continue;}}
        const auto uri=QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);if(!added.contains(uri))added.append(uri);if(selected.isEmpty())selected=uri;
    }
    m_added[m_slug]=added;reload();if(!selected.isEmpty()){chooseUrl(selected);saveState();}
}
void Backend::pauseAnimations(){
    auto config=currentConfiguration();if(!config.value("animated").toBool())return;
    config["paused"]=!config.value("paused").toBool();QString error;
    if(!Wallpapers::activate(config,m_paths.stateRoot,m_paths.wallpaperRuntime,error)){fail(error);return;}
    m_message=config.value("paused").toBool()?tr("Animations paused."):tr("Animations resumed.");m_error=false;emit stateChanged();
}
void Backend::useSystemWallpaper(){
    const auto before=snapshot();const auto oldUndo=m_undo,oldRedo=m_redo;m_undo.append(before);m_redo={};
    auto stopped=currentConfiguration();stopped["active"]=false;
    QJsonArray changes;changes.append(QJsonObject{{"schema","cinnarchy.wallpaper"},{"key","configuration"},{"value",QString::fromUtf8(QJsonDocument(stopped).toJson(QJsonDocument::Compact))}});
    if(!commit(changes)){m_undo=oldUndo;m_redo=oldRedo;return;}
    m_message=tr("Live backgrounds stopped. Cinnamon's static wallpaper remains.");m_error=false;emit stateChanged();
}
void Backend::fail(const QString &text) {m_message=text;m_error=true;emit stateChanged();}

QVariantList Backend::wallpapers(const QString &slug) const {
    QVariantList result;QStringList paths,seen;
    for(const auto &root:m_paths.stockRoots){QDirIterator it(root+"/"+slug+"/backgrounds",QDir::Files,QDirIterator::Subdirectories);while(it.hasNext())paths<<it.next();}
    for(const auto &root:m_paths.extraBackgroundRoots){QDirIterator it(root+"/"+slug,QDir::Files,QDirIterator::Subdirectories);while(it.hasNext())paths<<it.next();}
    for(const auto &url:m_added.value(slug).toArray())paths<<QUrl(url.toString()).toLocalFile();
    std::sort(paths.begin(),paths.end(),[](const QString&a,const QString&b){const bool av=Wallpapers::isVideo(a),bv=Wallpapers::isVideo(b);return av!=bv?!av:a<b;});
    for(const auto &path:paths){QFileInfo file(path);if(seen.contains(file.canonicalFilePath())||!Wallpapers::supported(path))continue;seen<<file.canonicalFilePath();result<<Wallpapers::media(path,m_paths.stateRoot);}
    return result;
}

QString Backend::iconFor(const QString &slug,const QString &mode,const QString &bundledChoice,QString &note) const {
    // Omarchy 4.5.0's choice for a built-in theme, unless an installed theme names its own.
    QString requested=bundledChoice;
    for(const auto &root:m_paths.stockRoots) {
        QFile f(root+"/"+slug+"/icons.theme");
        if(f.open(QIODevice::ReadOnly)) requested=QString::fromUtf8(f.readAll()).trimmed();
    }
    auto exists=[&](const QString &name){
        if(name.isEmpty() || name.contains('/') || name.contains('\n')) return false;
        for(const auto &root:m_paths.iconRoots) if(QFileInfo::exists(root+"/"+name+"/index.theme")) return true;
        return false;
    };
    if(exists(requested)) return requested;
    // Older Yaru releases (e.g. Debian 13's 24.04) lack some accents Omarchy names;
    // prefer the nearest Yaru accent before leaving the Yaru family.
    static const QHash<QString,QString> nearestYaru{{"Yaru-wartybrown","Yaru-bark"},{"Yaru-yellow","Yaru"}};
    if(const auto nearest=nearestYaru.value(requested);exists(nearest)) {
        note=tr("%1 is unavailable; using %2.").arg(requested,nearest);
        return nearest;
    }
    for(const auto &fallback:QStringList{mode=="dark"?"Papirus-Dark":"Papirus-Light","Adwaita"}) {
        if(exists(fallback)) {
            if(!requested.isEmpty()) note=tr("%1 is unavailable; using %2.").arg(requested,fallback);
            return fallback;
        }
    }
    note=tr("No supported icon pack is installed."); return {};
}

void Backend::reload() {
    const QString oldWallpaper=wallpaperUrl();
    const auto assignments=m_assignments;
    m_displays=m_paths.displayOverrides.isEmpty()?Wallpapers::displays():m_paths.displayOverrides;emit displaysChanged();
    bool targetPresent=m_target=="all";for(const auto &s:m_displays)targetPresent|=s.toMap().value("id").toString()==m_target;
    if(!targetPresent)m_target="all";
    QMap<QString,QVariantMap> found;
    QFile manifest(":/themes/manifest.json");
    if(!manifest.open(QIODevice::ReadOnly)) {fail(tr("The bundled theme catalog is missing."));return;}
    for(const auto &value:QJsonDocument::fromJson(manifest.readAll()).array()) {
        auto map=value.toObject().toVariantMap();map["bundled"]=true;found.insert(map.value("slug").toString(),map);
    }
    for(const auto &name:QDir(m_paths.themeRoot).entryList({"Omarchy-*"},QDir::Dirs|QDir::NoDotAndDotDot)) {
        const auto value=readObject(m_paths.themeRoot+"/"+name+"/palette.json");
        const auto slug=value.value("slug").toString();
        if(!QRegularExpression("^[a-z0-9]+(-[a-z0-9]+)*$").match(slug).hasMatch() || name!="Omarchy-"+slug) continue;
        if(!found.contains(slug)) {
            auto map=value.toVariantMap();map["name"]=name;
            map["mode"]=value.value("roles").toObject().value("mode").toString("dark");
            if(QColor(map.value("roles").toMap().value("accent").toString()).isValid()) found.insert(slug,map);
        }
    }
    m_themes.clear();
    for(auto it=found.begin();it!=found.end();++it) {
        auto map=it.value();const auto slug=it.key();
        QString note;map["icons"]=iconFor(slug,map.value("mode").toString(),map.value("icon_theme").toString(),note);map["iconNote"]=note;
        map["wallpapers"]=wallpapers(slug);
        map["thumbnail"]=map.value("wallpapers").toList().value(0).toMap().value("url");
        m_themes<<map;
    }
    emit catalogChanged();emit fetchChanged();
    if(!found.contains(m_slug)) {
        m_slug=activeName().startsWith("Omarchy-")?activeName().mid(8):QString();
        if(!found.contains(m_slug)) m_slug=found.isEmpty()?QString():found.firstKey();
    }
    selectTheme(m_slug);
    if(!oldWallpaper.isEmpty()) {
        const auto list=selected().value("wallpapers").toList();
        for(int i=0;i<list.size();++i) if(list[i].toMap().value("url").toString()==oldWallpaper) selectWallpaper(i);
    }
    if(!assignments.isEmpty()){m_assignments=assignments;syncSelection();}
    emit stateChanged();
}

void Backend::selectTheme(const QString &slug) {
    if(m_busy) return;
    for(const auto &item:m_themes) if(item.toMap().value("slug").toString()==slug) {
        m_slug=slug;m_wallpaperIndex=0;
        const auto list=selected().value("wallpapers").toList();
        const QString chosen=m_choices.value(slug).toString();
        const QString activeUri=unquote(m_store->read(bg,"picture-uri"));
        for(int i=0;i<list.size();++i) {
            const auto url=list[i].toMap().value("url").toString();
            if(url==chosen || (chosen.isEmpty() && activeName()==selected().value("name").toString() && url==activeUri)) m_wallpaperIndex=i;
        }
        // A theme without backgrounds of its own keeps the ones already chosen.
        if(list.isEmpty()) syncSelection();
        else chooseUrl(list.value(m_wallpaperIndex).toMap().value("url").toString());
        return;
    }
}
void Backend::selectWallpaper(int i) {
    if(m_busy) return;
    if(i>=0 && i<selected().value("wallpapers").toList().size()) {m_wallpaperIndex=i;chooseUrl(selected().value("wallpapers").toList()[i].toMap().value("url").toString());}
}

QJsonArray Backend::snapshot() const {
    QJsonArray out;
    for(const auto &key:managed) if(m_store->has(key.first,key.second)) out.append(QJsonObject{
        {"schema",key.first},{"key",key.second},{"value",m_store->read(key.first,key.second)}});
    out.append(QJsonObject{{"schema","cinnarchy.wallpaper"},{"key","configuration"},{"value",QString::fromUtf8(QJsonDocument(currentConfiguration()).toJson(QJsonDocument::Compact))}});
    return out;
}

bool Backend::ensureInstalled(const QVariantMap &theme) {
    const QString name=theme.value("name").toString();
    if(!QRegularExpression("^Omarchy-[a-z0-9]+(-[a-z0-9]+)*$").match(name).hasMatch()) {fail(tr("Invalid theme name."));return false;}
    const QString target=m_paths.themeRoot+"/"+name;
    if(QFileInfo(target).isSymLink()) {fail(tr("The theme directory is a symbolic link: %1").arg(target));return false;}
    const QString bundled=":/themes/"+name;
    QFile currentMarker(target+"/.omarchy-cinnamon"), bundledMarker(bundled+"/.omarchy-cinnamon");
    const bool owned=currentMarker.open(QIODevice::ReadOnly);
    const bool hasBundled=bundledMarker.open(QIODevice::ReadOnly);
    const bool upgrade=owned && hasBundled && currentMarker.readAll()!=bundledMarker.readAll();
    if(QFileInfo::exists(target+"/cinnamon/cinnamon.css") && QFileInfo::exists(target+"/gtk-3.0/gtk.css") && !upgrade) return true;
    if(!QDir(":/themes/"+name).exists()) {fail(tr("The theme files are missing. Rebuild and install this theme first."));return false;}
    if(QDir(target).exists() && !QFileInfo::exists(target+"/.omarchy-cinnamon")) {fail(tr("An unrelated folder already uses this theme name: %1").arg(target));return false;}
    if(!QDir().mkpath(m_paths.themeRoot)) {fail(tr("Cannot create the theme directory: %1").arg(m_paths.themeRoot));return false;}
    QTemporaryDir temp(m_paths.themeRoot+"/.cinnarchy-XXXXXX");
    if(!temp.isValid()) {fail(tr("Cannot stage the theme files."));return false;}
    QDirIterator files(":/themes/"+name,QDir::Files|QDir::Hidden,QDirIterator::Subdirectories);
    while(files.hasNext()) {
        const QString source=files.next(),relative=source.mid(QString(":/themes/"+name+"/").size());
        const QString path=temp.path()+"/"+relative;QDir().mkpath(QFileInfo(path).absolutePath());
        QFile input(source);QSaveFile output(path);
        if(!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {fail(tr("Cannot copy theme files."));return false;}
        const auto bytes=input.readAll();
        if(output.write(bytes)!=bytes.size() || !output.commit()) {fail(tr("Cannot save theme files."));return false;}
    }
    QString backup;
    if(QDir(target).exists()) {
        backup=target+".backup-"+QDateTime::currentDateTimeUtc().toString("yyyyMMddhhmmsszzz");
        if(!QDir().rename(target,backup)) {fail(tr("Cannot back up the existing theme."));return false;}
    }
    if(!QDir().rename(temp.path(),target)) {
        if(!backup.isEmpty()) QDir().rename(backup,target);
        fail(tr("Cannot install the theme."));return false;
    }
    temp.setAutoRemove(false);m_themeFilesChanged=true;return true;
}

QJsonArray Backend::desiredSettings() {
    QJsonArray changes;const auto t=selected();const auto r=t.value("roles").toMap();
    auto add=[&](const QString &schema,const QString &key,const QString &value){
        if(m_store->has(schema,key)) changes.append(QJsonObject{{"schema",schema},{"key",key},{"value",value}});
    };
    for(const auto &key:QList<QPair<QString,QString>>{{shell,"name"},{ui,"gtk-theme"},{wm,"theme"}}) add(key.first,key.second,variantString(t.value("name").toString()));
    add(portal,"color-scheme",variantString(t.value("mode").toString()=="light"?"prefer-light":"prefer-dark"));
    add(portal,"accent-rgb",variantString(r.value("accent").toString()));
    if(m_useIcons) add(ui,"icon-theme",variantString(t.value("icons").toString()));
    if(m_useWallpaper) {
        add(slide,"slideshow-enabled","false");add(slide,"slideshow-paused","false");
        add(bg,"picture-uri",variantString(m_prepared.value("poster").toString()));add(bg,"picture-options",variantString(m_prepared.value("option").toString()));
        changes.append(QJsonObject{{"schema","cinnarchy.wallpaper"},{"key","configuration"},{"value",QString::fromUtf8(QJsonDocument(m_prepared).toJson(QJsonDocument::Compact))}});
        add(bg,"picture-opacity","100");add(bg,"primary-color",variantString(r.value("bg").toString()));
        add(bg,"secondary-color",variantString(r.value("bg").toString()));add(bg,"color-shading-type",variantString("solid"));
    }
    return changes;
}

bool Backend::writeSnapshot(const QJsonArray &values,QString &error) {
    if(values.isEmpty()) {error=tr("The settings snapshot is empty.");return false;}
    for(const auto &item:values) {
        const auto obj=item.toObject();const auto s=obj.value("schema").toString(),k=obj.value("key").toString();
        if(!managed.contains({s,k}) || !obj.value("value").isString()) {error=tr("Invalid settings backup.");return false;}
        if(s=="cinnarchy.wallpaper")continue;
        if(!m_store->writable(s,k)) {error=tr("This setting is unavailable or locked: %1 / %2").arg(s,k);return false;}
    }
    for(const auto &item:values) {
        const auto obj=item.toObject();
        if(obj.value("schema").toString()=="cinnarchy.wallpaper")continue;
        if(!m_store->write(obj.value("schema").toString(),obj.value("key").toString(),obj.value("value").toString(),error)) return false;
    }
    m_store->sync();
    for(const auto &item:values) {
        const auto obj=item.toObject();
        if(obj.value("schema").toString()=="cinnarchy.wallpaper")continue;
        if(m_store->read(obj.value("schema").toString(),obj.value("key").toString())!=obj.value("value").toString()) {
            error=tr("A desktop setting did not persist: %1").arg(obj.value("key").toString());return false;
        }
    }
    bool wallpaperChanged=false;QJsonObject configuration;
    for(const auto &item:values){const auto obj=item.toObject();
        if(obj.value("schema").toString()==bg&&obj.value("key").toString()=="picture-uri")wallpaperChanged=true;
        if(obj.value("schema").toString()=="cinnarchy.wallpaper"){wallpaperChanged=true;QJsonParseError parse;const auto doc=QJsonDocument::fromJson(obj.value("value").toString().toUtf8(),&parse);if(parse.error!=QJsonParseError::NoError||!doc.isObject()){error=tr("Invalid wallpaper configuration backup.");return false;}configuration=doc.object();}
    }
    if(wallpaperChanged&&!Wallpapers::activate(configuration,m_paths.stateRoot,m_paths.wallpaperRuntime,error))return false;
    return true;
}

bool Backend::commit(const QJsonArray &changes) {
    for(const auto &item:changes) {
        const auto obj=item.toObject();const auto s=obj.value("schema").toString(),k=obj.value("key").toString();
        if(!managed.contains({s,k}) || !obj.value("value").isString() || (s!="cinnarchy.wallpaper"&&!m_store->writable(s,k))) {
            fail(tr("This setting is unavailable or locked: %1 / %2").arg(s,k));return false;
        }
    }
    const bool refreshTheme=m_themeFilesChanged;
    m_themeFilesChanged=false;
    const auto before=snapshot();
    if(!saveJson(m_paths.stateRoot+"/pending.json",{{"version",1},{"snapshot",before}})) return false;
    QString error;
    bool refreshed=true;
    // GTK caches a provider while the selected name stays unchanged. A theme
    // asset upgrade must also invalidate existing Nemo and settings windows.
    if(refreshTheme && unquote(m_store->read(ui,"gtk-theme"))==selected().value("name").toString()) {
        refreshed=m_store->write(ui,"gtk-theme",variantString("Adwaita"),error);
        m_store->sync();
        QEventLoop refresh;
        QTimer::singleShot(180,&refresh,&QEventLoop::quit);
        refresh.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if(!refreshed || !writeSnapshot(changes,error) || !saveState()) {
        if(error.isEmpty()) error=m_message;
        QString rollback;
        if(writeSnapshot(before,rollback)) QFile::remove(m_paths.stateRoot+"/pending.json");
        else error+=tr(" Recovery is available through Undo. %1").arg(rollback);
        fail(error);return false;
    }
    QFile::remove(m_paths.stateRoot+"/pending.json");emit stateChanged();return true;
}

bool Backend::applyNow() {
    m_themeFilesChanged=false;
    if(selected().isEmpty()) {fail(tr("Choose a theme first."));return false;}
    if(!m_store->has(shell,"name") || !m_store->has(ui,"gtk-theme")) {fail(tr("Cinnamon desktop settings are unavailable. Run this in your Cinnamon session."));return false;}
    if(m_useWallpaper){
        bool chosen=false;for(const auto &choice:m_assignments)chosen|=!choice.toObject().value("url").toString().isEmpty();
        if(!chosen){fail(tr("No background is chosen yet. Download this theme's backgrounds, add your own image, or turn off Apply wallpaper."));return false;}
        QString error;m_prepared=Wallpapers::prepare(m_displays,m_assignments,m_paths.stateRoot,error);if(m_prepared.isEmpty()){fail(error);return false;}
    }
    if(m_useIcons && selected().value("icons").toString().isEmpty()) {fail(tr("No matching icon pack is installed. Turn off Apply matching icons to continue."));return false;}
    if(!ensureInstalled(selected())) return false;
    const auto oldOriginal=m_original,oldUndo=m_undo,oldRedo=m_redo;const auto oldChoices=m_choices;
    const auto before=snapshot();if(m_original.isEmpty()) m_original=before;
    m_undo.append(before);while(m_undo.size()>20) m_undo.removeFirst();m_redo={};
    m_choices[m_slug]=wallpaperUrl();
    if(!commit(desiredSettings())) {m_original=oldOriginal;m_undo=oldUndo;m_redo=oldRedo;m_choices=oldChoices;emit stateChanged();return false;}
    m_error=false;m_message=tr("Applied %1. Your desktop is ready.").arg(selected().value("title").toString());emit stateChanged();return true;
}
bool Backend::undoNow() {
    if(m_undo.isEmpty()) return false;
    const auto oldUndo=m_undo,oldRedo=m_redo;const auto target=m_undo.takeAt(m_undo.size()-1).toArray();m_redo.append(snapshot());
    if(!commit(target)) {m_undo=oldUndo;m_redo=oldRedo;emit stateChanged();return false;}
    m_error=false;m_message=tr("Restored the previous desktop setup.");emit stateChanged();return true;
}
bool Backend::redoNow() {
    if(m_redo.isEmpty()) return false;
    const auto oldUndo=m_undo,oldRedo=m_redo;const auto target=m_redo.takeAt(m_redo.size()-1).toArray();m_undo.append(snapshot());
    if(!commit(target)) {m_undo=oldUndo;m_redo=oldRedo;emit stateChanged();return false;}
    m_error=false;m_message=tr("Reapplied the desktop setup.");emit stateChanged();return true;
}
bool Backend::restoreNow() {
    if(m_original.isEmpty()) return false;
    const auto oldOriginal=m_original,oldUndo=m_undo,oldRedo=m_redo;
    const auto target=m_original;m_original={};m_undo.append(snapshot());m_redo={};
    if(!commit(target)) {m_original=oldOriginal;m_undo=oldUndo;m_redo=oldRedo;emit stateChanged();return false;}
    m_error=false;m_message=tr("Restored the setup from before your first switch.");emit stateChanged();return true;
}
bool Backend::resetNow() {
    const auto inUse=[this]{
        for(const auto &key:managed.mid(0,3)) // the desktop, application and window-border themes
            if(unquote(m_store->read(key.first,key.second)).startsWith("Omarchy-"))return true;
        return currentConfiguration().value("active").toBool()
            ||QUrl(unquote(m_store->read(bg,"picture-uri"))).toLocalFile().startsWith(m_paths.stateRoot+"/");
    };
    bool done=true;
    if(hasOriginal())done=restoreNow();
    if(done&&inUse()) {
        // Nothing saved to go back to, or it was an Omarchy look itself.
        QJsonArray defaults;
        for(const auto &key:managed)if(m_store->writable(key.first,key.second))
            defaults.append(QJsonObject{{"schema",key.first},{"key",key.second},{"value",m_store->defaultValue(key.first,key.second)}});
        defaults.append(QJsonObject{{"schema","cinnarchy.wallpaper"},{"key","configuration"},{"value","{}"}});
        const auto oldUndo=m_undo,oldRedo=m_redo;m_undo.append(snapshot());m_redo={};
        done=commit(defaults);
        if(!done){m_undo=oldUndo;m_redo=oldRedo;}
    }
    if(!done){emit stateChanged();return false;}
    Wallpapers::listDesktopHandler(*m_store,false);
    m_error=false;m_message=tr("The desktop is back to how it was without Cinnarchy.");emit stateChanged();return true;
}
void Backend::schedule(const std::function<bool()> &action) {
    if(m_busy) return;
    m_busy=true;emit stateChanged();
    QTimer::singleShot(50,this,[this,action]{action();m_busy=false;emit stateChanged();});
}
void Backend::apply(){schedule([this]{return applyNow();});}
void Backend::undo(){schedule([this]{return undoNow();});}
void Backend::redo(){schedule([this]{return redoNow();});}
void Backend::restoreOriginal(){schedule([this]{return restoreNow();});}

bool Backend::saveJson(const QString &path,const QJsonObject &value) {
    if(!QDir().mkpath(QFileInfo(path).absolutePath())) {fail(tr("Cannot create the settings backup directory: %1").arg(QFileInfo(path).absolutePath()));return false;}
    QSaveFile f(path);const auto bytes=QJsonDocument(value).toJson();
    if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()) {fail(tr("Cannot save the settings backup: %1").arg(path));return false;}
    return true;
}
bool Backend::saveState() {
    return saveJson(m_paths.stateRoot+"/state.json",{{"version",1},{"original",m_original},{"undo",m_undo},{"redo",m_redo},
        {"wallpapers",m_choices},{"addedMedia",m_added},{"selectedSlug",m_slug},{"pictureOption",m_pictureOption},{"useWallpaper",m_useWallpaper},{"useIcons",m_useIcons}});
}
void Backend::loadState() {
    const auto value=readObject(m_paths.stateRoot+"/state.json");
    if(value.value("version").toInt()!=1) return;
    m_original=value.value("original").toArray();m_undo=value.value("undo").toArray();m_redo=value.value("redo").toArray();
    m_added=value.value("addedMedia").toObject();
    m_choices=value.value("wallpapers").toObject();m_slug=value.value("selectedSlug").toString();
    setPictureOption(value.value("pictureOption").toString("zoom"));
    m_useWallpaper=value.value("useWallpaper").toBool(true);m_useIcons=value.value("useIcons").toBool(true);
}
QJsonObject Backend::status() const {
    return {{"selected",m_slug},{"active",activeName()},{"wallpaper",wallpaperUrl()},{"message",m_message},
        {"error",m_error},{"canUndo",canUndo()},{"canRedo",canRedo()},{"hasOriginal",hasOriginal()},{"themeCount",m_themes.size()},{"displays",QJsonArray::fromVariantList(m_displays)},{"assignments",m_assignments},{"liveWallpaper",currentConfiguration()}};
}
