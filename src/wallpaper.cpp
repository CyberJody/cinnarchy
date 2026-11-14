#include "wallpaper.h"
#include "settings.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QImageReader>
#include <QJsonDocument>
#include <QSaveFile>
#include <QPainter>
#include <QCryptographicHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QSocketNotifier>
#include <QUrl>
#include <QRegularExpression>
#include <memory>
#include <vector>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>
#undef None
#undef Status
#undef Bool

namespace {
QString mediaPath(const QJsonObject &choice) {return QUrl(choice.value("url").toString()).toLocalFile();}
QRect rect(const QVariantMap &screen) {return {screen.value("x").toInt(),screen.value("y").toInt(),screen.value("width").toInt(),screen.value("height").toInt()};}
QString fingerprint(const QString &path) {
    QFileInfo f(path);return QString::fromLatin1(QCryptographicHash::hash((f.absoluteFilePath()+QString::number(f.size())+QString::number(f.lastModified().toMSecsSinceEpoch())).toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
}
QString quotedExec(QString text) {text.replace('\\',"\\\\");text.replace('"',"\\\"");return '"'+text+'"';}
bool autostart(bool active,const QString &stateRoot,QString &error) {
    // Isolated development state roots must never install login entries.
    const QString normal=qEnvironmentVariable("XDG_STATE_HOME",QDir::homePath()+"/.local/state")+"/cinnarchy";
    if(QDir(stateRoot).absolutePath()!=QDir(normal).absolutePath()) return true;
    const QString path=qEnvironmentVariable("XDG_CONFIG_HOME",QDir::homePath()+"/.config")+"/autostart/cinnarchy-wallpaper.desktop";
    if(!active) {QFile::remove(path);return true;}
    QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);
    const QString contents="[Desktop Entry]\nType=Application\nName=Omarchy Wallpapers\nExec="+quotedExec(QCoreApplication::applicationFilePath())+" wallpaper-service\nTerminal=false\nNoDisplay=true\nX-GNOME-Autostart-enabled=true\nOnlyShowIn=X-Cinnamon;Cinnamon;\n";
    if(!f.open(QIODevice::WriteOnly)||f.write(contents.toUtf8())<0||!f.commit()) {error=QObject::tr("Cannot save wallpaper startup settings.");return false;}return true;
}
}

QVariantList Wallpapers::displays() {
    QVariantList result;
    Display *display=XOpenDisplay(nullptr);
    if(display) {
        int count=0;auto *monitors=XRRGetMonitors(display,DefaultRootWindow(display),True,&count);
        for(int i=0;i<count;++i) {
            auto &m=monitors[i];char *name=XGetAtomName(display,m.name);
            result<<QVariantMap{{"id",QString::fromUtf8(name?name:"Display")},{"name",QString::fromUtf8(name?name:"Display")},
                {"x",m.x},{"y",m.y},{"width",m.width},{"height",m.height},{"primary",bool(m.primary)}};
            if(name)XFree(name);
        }
        if(monitors)XRRFreeMonitors(monitors);
        XCloseDisplay(display);
    }
    if(result.isEmpty()) for(auto *screen:QGuiApplication::screens()) {
        const auto g=screen->geometry();result<<QVariantMap{{"id",screen->name().isEmpty()?"Display-1":screen->name()},
            {"name",screen->name().isEmpty()?QObject::tr("Display 1"):screen->name()},
            {"x",g.x()},{"y",g.y()},{"width",g.width()},{"height",g.height()},{"primary",screen==QGuiApplication::primaryScreen()}};
    }
    return result;
}
bool Wallpapers::isVideo(const QString &path) {return QStringList{"mp4","webm","mkv","mov","m4v","avi","apng"}.contains(QFileInfo(path).suffix().toLower());}
bool Wallpapers::isAnimated(const QString &path) {
    if(isVideo(path))return true;
    QImageReader r(path);return r.supportsAnimation()&&r.imageCount()>1;
}
bool Wallpapers::supported(const QString &path) {return QFileInfo(path).isFile()&&(isVideo(path)||QImageReader(path).canRead());}
QVariantMap Wallpapers::media(const QString &path,const QString &stateRoot) {
    const QFileInfo file(path);const bool animated=isAnimated(path);QString thumbnail=path;
    if(isVideo(path)) {
        thumbnail=stateRoot+"/posters/"+fingerprint(path)+".png";
        if(!QFileInfo::exists(thumbnail)) {
            thumbnail.clear();QDir dir=file.dir();if(dir.dirName()=="intros")dir.cdUp();
            for(const auto &extension:{"webp","jpg","png","jpeg"})if(QFileInfo::exists(dir.filePath(file.completeBaseName()+"."+extension))){thumbnail=dir.filePath(file.completeBaseName()+"."+extension);break;}
        }
    }
    QString title=file.completeBaseName();title.remove(QRegularExpression("^[0-9]+[-_ ]*"));title.replace('-',' ');title.replace('_',' ');if(!title.isEmpty())title[0]=title.at(0).toUpper();
    return {{"path",file.absoluteFilePath()},{"url",QUrl::fromLocalFile(file.absoluteFilePath()).toString(QUrl::FullyEncoded)},
        {"title",title},{"animated",animated},{"mediaType",isVideo(path)?"video":animated?"animated-image":"image"},
        {"thumbnail",thumbnail.isEmpty()?QString():QUrl::fromLocalFile(thumbnail).toString(QUrl::FullyEncoded)}};
}
QJsonObject Wallpapers::read(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();}
bool Wallpapers::save(const QString &path,const QJsonObject &data,QString &error) {
    if(!QDir().mkpath(QFileInfo(path).absolutePath())){error=QObject::tr("Cannot create wallpaper storage.");return false;}
    QSaveFile f(path);const auto bytes=QJsonDocument(data).toJson();
    if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit()){error=QObject::tr("Cannot save wallpaper configuration: %1").arg(path);return false;}return true;
}
QString Wallpapers::poster(const QString &path,const QString &stateRoot,QString &error) {
    if(!supported(path)){error=QObject::tr("Wallpaper is missing or unsupported: %1").arg(path);return {};}
    if(!isAnimated(path))return path;
    const QString dir=stateRoot+"/posters";QDir().mkpath(dir);const QString output=dir+"/"+fingerprint(path)+".png";
    if(QImageReader(output).canRead())return output;
    if(!isVideo(path)){QImageReader r(path);const auto image=r.read();if(!image.isNull()&&image.save(output))return output;}
    const QString ffmpeg=QStandardPaths::findExecutable("ffmpeg");
    if(ffmpeg.isEmpty()){error=QObject::tr("ffmpeg is required for animated backgrounds.");return {};}
    QProcess p;p.start(ffmpeg,{"-nostdin","-v","error","-threads","2","-i",path,"-frames:v","1","-vf","scale=1920:1920:force_original_aspect_ratio=decrease","-threads","2","-y",output});
    if(!p.waitForFinished(15000)){p.kill();p.waitForFinished();error=QObject::tr("Timed out reading animated background: %1").arg(path);return {};}
    if(p.exitCode()!=0||!QImageReader(output).canRead()){error=QObject::tr("Cannot decode animated background: %1").arg(path);return {};}
    return output;
}
QImage Wallpapers::compose(const QVariantList &screens,const QJsonObject &assignments,const QString &stateRoot,QString &error) {
    QRect bounds;for(const auto &v:screens)bounds=bounds.united(rect(v.toMap()));
    if(bounds.isEmpty()||bounds.width()>32768||bounds.height()>32768){error=QObject::tr("Invalid display layout.");return {};}
    QImage image(bounds.size(),QImage::Format_RGB32);if(image.isNull()){error=QObject::tr("Not enough memory for this display layout.");return {};}
    image.fill(Qt::black);QPainter painter(&image);painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for(const auto &v:screens) {
        const auto screen=v.toMap();const auto choice=assignments.value(screen.value("id").toString()).toObject();
        const QString source=poster(mediaPath(choice),stateRoot,error);if(source.isEmpty())return {};
        QImageReader reader(source);reader.setAutoTransform(true);const auto pixels=reader.read();
        if(pixels.isNull()){error=QObject::tr("Cannot read wallpaper: %1").arg(source);return {};}
        const QRect area=rect(screen).translated(-bounds.topLeft());const QString fit=choice.value("fit").toString("zoom");
        const auto size=fit=="centered"?pixels.size():pixels.size().scaled(area.size(),fit=="scaled"?Qt::KeepAspectRatio:Qt::KeepAspectRatioByExpanding);
        const QRect destination(QPoint(area.x()+(area.width()-size.width())/2,area.y()+(area.height()-size.height())/2),size);
        painter.save();painter.setClipRect(area);painter.drawImage(destination,pixels);painter.restore();
    }
    return image;
}
QJsonObject Wallpapers::prepare(const QVariantList &screens,const QJsonObject &assignments,const QString &stateRoot,QString &error) {
    if(screens.isEmpty()){error=QObject::tr("No displays are available.");return {};}
    QString common,fit;bool same=true,animated=false;
    for(const auto &s:screens) {
        const auto id=s.toMap().value("id").toString();const auto choice=assignments.value(id).toObject();const QString path=mediaPath(choice);
        if(!supported(path)){error=QObject::tr("Choose an available background for %1.").arg(id);return {};}
        animated|=isAnimated(path);
        if(common.isEmpty()){common=choice.value("url").toString();fit=choice.value("fit").toString("zoom");}
        else same&=common==choice.value("url").toString()&&fit==choice.value("fit").toString("zoom");
    }
    if(same&&!animated) return {{"version",1},{"active",false},{"poster",common},{"option",fit},{"assignments",assignments},{"screens",QJsonArray::fromVariantList(screens)}};
    if(animated&&QStandardPaths::findExecutable("mpv").isEmpty()){error=QObject::tr("mpv is required for animated desktop backgrounds.");return {};}
    auto posterScreens=screens;auto posterAssignments=assignments;
    const bool spanning=same&&fit=="spanned";
    if(spanning) {
        QRect bounds;for(const auto &screen:screens)bounds=bounds.united(rect(screen.toMap()));
        const auto id=screens.first().toMap().value("id").toString();
        posterScreens={QVariantMap{{"id",id},{"x",bounds.x()},{"y",bounds.y()},{"width",bounds.width()},{"height",bounds.height()}}};
    }
    const auto image=compose(posterScreens,posterAssignments,stateRoot,error);if(image.isNull())return {};
    // The still poster depends on what is shown where, not on how a video plays.
    QJsonObject shown;for(auto it=assignments.begin();it!=assignments.end();++it)shown[it.key()]=QJsonObject{{"url",it.value().toObject().value("url")},{"fit",it.value().toObject().value("fit")}};
    const auto key=QCryptographicHash::hash(QJsonDocument(QJsonObject{{"screens",QJsonArray::fromVariantList(screens)},{"assignments",shown}}).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex().left(24);
    const QString path=stateRoot+"/wallpapers/"+QString::fromLatin1(key)+".png";QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||!image.save(&file,"PNG")||!file.commit()){error=QObject::tr("Cannot save the combined desktop background.");return {};}
    return {{"version",1},{"active",true},{"animated",animated},{"paused",false},
        {"poster",QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)},{"option","spanned"},{"spanning",spanning},{"assignments",assignments},{"screens",QJsonArray::fromVariantList(screens)}};
}
QString Wallpapers::configurationPath(const QString &root){return root+"/wallpaper.json";}
void Wallpapers::listDesktopHandler(SettingsStore &settings,bool listed) {
    const QString schema="org.nemo.desktop",key="ignored-desktop-handlers",ours="CinnarchyWallpaper";
    const auto current=settings.read(schema,key);
    if(current.isEmpty()||current.contains(ours,Qt::CaseInsensitive)==listed)return;
    QStringList items;auto it=QRegularExpression("'((?:[^'\\\\]|\\\\.)*)'").globalMatch(current);
    while(it.hasNext()){const auto item=it.next().captured(1);if(item.compare(ours,Qt::CaseInsensitive)!=0)items<<"'"+item+"'";}
    if(listed)items<<"'"+ours+"'";
    QString ignored;
    if(settings.write(schema,key,"["+items.join(", ")+"]",ignored))settings.sync();
}
QString Wallpapers::socketPath(const QString &root){
    const auto key=QCryptographicHash::hash(QDir(root).absolutePath().toUtf8(),QCryptographicHash::Sha256).toHex().left(20);
    return QDir::tempPath()+"/cinnarchy-"+QString::number(getuid())+"/"+QString::fromLatin1(key)+".sock";
}
QJsonObject Wallpapers::request(const QString &root,const QJsonObject &message,int timeout) {
    QLocalSocket socket;socket.connectToServer(socketPath(root));if(!socket.waitForConnected(qMin(timeout,500)))return {};
    socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact)+'\n');socket.flush();QElapsedTimer timer;timer.start();QByteArray data;
    while(timer.elapsed()<timeout){if(socket.bytesAvailable()||socket.waitForReadyRead(qMax(1,timeout-int(timer.elapsed())))){data+=socket.readAll();if(data.contains('\n'))return QJsonDocument::fromJson(data.left(data.indexOf('\n'))).object();}else break;}
    return {};
}
bool Wallpapers::activate(const QJsonObject &config,const QString &root,bool runtime,QString &error) {
    if(!save(configurationPath(root),config,error))return false;
    if(!runtime)return true;
    const bool active=config.value("active").toBool();
    auto response=request(root,{{"action","reload"}},10000);
    if(active&&!response.isEmpty()&&response.value("version").toString()!=QCoreApplication::applicationVersion()) {
        // A player left running by another version of the picker (an upgrade while a live
        // wallpaper was on) would ignore newer settings. It quits on an inactive
        // configuration, so let it wind down and start this version's in its place.
        auto idle=config;idle["active"]=false;
        if(!save(configurationPath(root),idle,error))return false;
        request(root,{{"action","reload"}},3000);
        QElapsedTimer leaving;leaving.start();
        while(leaving.elapsed()<4000&&!request(root,{{"action","status"}},300).isEmpty())QThread::msleep(100);
        if(!save(configurationPath(root),config,error))return false;
        response={};
    }
    if(response.isEmpty()&&active) {
        QProcess process;auto env=QProcessEnvironment::systemEnvironment();env.remove("QT_QPA_PLATFORM");env.remove("QT_QUICK_BACKEND");
        process.setProcessEnvironment(env);process.setProgram(QCoreApplication::applicationFilePath());process.setArguments({"wallpaper-service","--state-dir",root});
        process.setStandardOutputFile(root+"/wallpaper-service.log",QIODevice::Append);process.setStandardErrorFile(root+"/wallpaper-service.log",QIODevice::Append);
        if(!process.startDetached()){error=QObject::tr("Cannot start the desktop wallpaper player.");return false;}
        QElapsedTimer wait;wait.start();
        while(wait.elapsed()<12000){response=request(root,{{"action","status"}},1000);if(!response.isEmpty())break;QThread::msleep(80);}
    }
    if(active&&(response.isEmpty()||!response.value("ok").toBool()||!response.value("active").toBool())) {error=response.value("error").toString(QObject::tr("The wallpaper player did not become ready."));return false;}
    return autostart(active,root,error);
}

namespace {
QJsonObject mpvCommand(const QString &socket,const QJsonArray &command,int timeout=500);
struct LiveWindow {
    Window window=0;QProcess process;QString socket,path,fit,screen;QRect geometry;QByteArray stderrTail;PlaybackLoop loop;
    ~LiveWindow(){process.kill();process.waitForFinished(1500);}
};
// What decides which players exist; speed, repeat and pause are changed on running ones.
QJsonObject withoutPlayback(QJsonObject config) {
    config.remove("paused");auto assignments=config.value("assignments").toObject();
    for(auto it=assignments.begin();it!=assignments.end();++it){auto choice=it.value().toObject();choice.remove("speed");choice.remove("replay");it.value()=choice;}
    config["assignments"]=assignments;return config;
}
QJsonObject mpvCommand(const QString &socket,const QJsonArray &command,int timeout) {
    QLocalSocket connection;connection.connectToServer(socket);if(!connection.waitForConnected(timeout))return {};
    connection.write(QJsonDocument(QJsonObject{{"command",command},{"request_id",7}}).toJson(QJsonDocument::Compact)+'\n');connection.flush();
    QElapsedTimer timer;timer.start();QByteArray buffer;
    while(timer.elapsed()<timeout) {
        if(!connection.bytesAvailable()&&!connection.waitForReadyRead(qMax(1,timeout-int(timer.elapsed()))))break;
        buffer+=connection.readAll();
        while(buffer.contains('\n')) {const int i=buffer.indexOf('\n');const auto value=QJsonDocument::fromJson(buffer.left(i)).object();buffer.remove(0,i+1);if(value.value("request_id").toInt()==7)return value;}
    }
    return {};
}
class DesktopService:public QObject {
    QString root;Display *display=nullptr;QLocalServer server;QTimer poll;QJsonObject config;QByteArray signature;QString error;
    QElapsedTimer externalChange;
    // At login the desktop may not be ready for video yet; failed starts are retried.
    int retries=0;QElapsedTimer retryWait;QByteArray retriedSignature;
    std::vector<std::unique_ptr<LiveWindow>> windows;std::unique_ptr<SettingsStore> settings=makeSettingsStore();
    void clear(){for(auto &w:windows){w->process.kill();w->process.waitForFinished(1000);if(display&&w->window)XDestroyWindow(display,w->window);}windows.clear();if(display)XFlush(display);}
    // Cinnamon redirects a window into its compositor when it starts managing
    // it. A player that renders before then can stay invisible, so wait for
    // the window manager's WM_STATE (at most 1.5 s).
    void waitUntilManaged(Window window) {
        const Atom state=XInternAtom(display,"WM_STATE",False);QElapsedTimer wait;wait.start();
        while(wait.elapsed()<1500) {
            XSync(display,False);Atom type=0;int format=0;unsigned long count=0,after=0;unsigned char *data=nullptr;
            const bool managed=XGetWindowProperty(display,window,state,0,2,False,state,&type,&format,&count,&after,&data)==Success&&type==state&&count>0;
            if(data)XFree(data);
            if(managed){QThread::msleep(100);return;}
            QThread::msleep(20);
        }
    }
    void lower(){for(auto &w:windows)if(w->window)XLowerWindow(display,w->window);if(display)XFlush(display);}
    bool load() {
        const auto next=Wallpapers::read(Wallpapers::configurationPath(root));const auto sig=QJsonDocument(next).toJson(QJsonDocument::Compact);
        if(sig==signature)return error.isEmpty();
        if(withoutPlayback(next)==withoutPlayback(config)&&!windows.empty()) {
            config=next;signature=sig;
            for(auto &w:windows) {
                w->loop.configure(w->socket,Playback::from(config.value("assignments").toObject().value(w->screen).toObject()),true);
                mpvCommand(w->socket,{"set_property","pause",config.value("paused").toBool()});
            }
            return error.isEmpty();
        }
        clear();config=next;signature=sig;error.clear();externalChange.invalidate();
        if(!config.value("active").toBool())return true;
        if(!display){error=tr("Animated desktop backgrounds require an X11 Cinnamon session.");return false;}
        const auto screens=Wallpapers::displays();QRect desktopBounds;for(const auto &s:screens)desktopBounds=desktopBounds.united(rect(s.toMap()));
        for(const auto &s:screens) {
            const auto screen=s.toMap();const auto choice=config.value("assignments").toObject().value(screen.value("id").toString()).toObject();
            const auto path=mediaPath(choice);if(!Wallpapers::isAnimated(path))continue;
            auto live=std::make_unique<LiveWindow>();live->path=path;live->fit=choice.value("fit").toString("zoom");live->geometry=rect(screen);
            live->screen=screen.value("id").toString();const auto playback=Playback::from(choice);
            if(config.value("spanning").toBool())live->geometry=desktopBounds;
            auto g=live->geometry;live->window=XCreateSimpleWindow(display,DefaultRootWindow(display),g.x(),g.y(),g.width(),g.height(),0,0,0);
            Atom type=XInternAtom(display,"_NET_WM_WINDOW_TYPE_DESKTOP",False),property=XInternAtom(display,"_NET_WM_WINDOW_TYPE",False);
            XChangeProperty(display,live->window,property,XA_ATOM,32,PropModeReplace,reinterpret_cast<unsigned char*>(&type),1);
            Atom states[]={XInternAtom(display,"_NET_WM_STATE_BELOW",False),XInternAtom(display,"_NET_WM_STATE_STICKY",False),XInternAtom(display,"_NET_WM_STATE_SKIP_TASKBAR",False),XInternAtom(display,"_NET_WM_STATE_SKIP_PAGER",False)};
            XChangeProperty(display,live->window,XInternAtom(display,"_NET_WM_STATE",False),XA_ATOM,32,PropModeReplace,reinterpret_cast<unsigned char*>(states),4);
            unsigned long desktop=0xffffffff;XChangeProperty(display,live->window,XInternAtom(display,"_NET_WM_DESKTOP",False),XA_CARDINAL,32,PropModeReplace,reinterpret_cast<unsigned char*>(&desktop),1);
            XWMHints hints{};hints.flags=InputHint;hints.input=False;XSetWMHints(display,live->window,&hints);
            XClassHint klass{const_cast<char*>("cinnarchy-wallpaper"),const_cast<char*>("CinnarchyWallpaper")};XSetClassHint(display,live->window,&klass);
            XStoreName(display,live->window,"Omarchy animated wallpaper");
            XShapeCombineRectangles(display,live->window,ShapeInput,0,0,nullptr,0,ShapeSet,Unsorted);
            XMapWindow(display,live->window);XLowerWindow(display,live->window);XFlush(display);
            waitUntilManaged(live->window);
            live->socket=Wallpapers::socketPath(root)+"-mpv-"+QString::number(windows.size());QFile::remove(live->socket);
            auto *player=live.get();
            connect(&live->process,&QProcess::readyReadStandardError,this,[player]{player->stderrTail=(player->stderrTail+player->process.readAllStandardError()).right(4096);});
            connect(&live->process,&QProcess::readyReadStandardOutput,this,[player]{player->process.readAllStandardOutput();});
            auto environment=QProcessEnvironment::systemEnvironment();
            // Explicit Unix transport also works across private IPC namespaces.
            // mpv's software fallback must not use inaccessible SysV shared memory.
            const auto xDisplay=environment.value("DISPLAY");
            if(xDisplay.startsWith(':'))environment.insert("DISPLAY","unix/"+xDisplay);
            live->process.setProcessEnvironment(environment);
            QStringList args{"--no-config","--load-scripts=no","--no-audio","--no-osc","--no-osd-bar","--osd-level=0","--msg-level=all=warn","--input-terminal=no","--input-default-bindings=no","--input-vo-keyboard=no","--cursor-autohide=always","--keep-open=yes","--vo=gpu-next,gpu,x11","--x11-present=no","--hwdec=auto-safe","--wid="+QString::number(live->window),"--input-ipc-server="+live->socket};
            args<<"--panscan="+QString(live->fit=="zoom"||live->fit=="spanned"?"1":"0");
            if(live->fit=="centered")args<<"--video-unscaled=yes";
            args<<playback.arguments();live->loop.configure(live->socket,playback,false);
            if(config.value("paused").toBool())args<<"--pause=yes";
            args<<"--"<<path;live->process.start(QStandardPaths::findExecutable("mpv"),args);
            if(!live->process.waitForStarted(1500)){error=tr("Cannot start mpv for %1.").arg(screen.value("id").toString());windows.push_back(std::move(live));break;}
            QElapsedTimer wait;wait.start();bool ready=false;double firstPosition=-1;
            while(wait.elapsed()<6500&&live->process.state()!=QProcess::NotRunning) {
                const auto reply=mpvCommand(live->socket,{"get_property","time-pos"},350);
                if(reply.value("error").toString()=="success"&&reply.value("data").isDouble()) {
                    const auto position=reply.value("data").toDouble();
                    if(firstPosition<0)firstPosition=position;
                    if(config.value("paused").toBool()||qAbs(position-firstPosition)>.025){ready=true;break;}
                    // A short clip may already rest on its last frame before a repeat.
                    if(mpvCommand(live->socket,{"get_property","eof-reached"},350).value("data").toBool()){ready=true;break;}
                }
                live->process.waitForReadyRead(70);
            }
            if(!ready)error=tr("Could not play %1. %2").arg(QFileInfo(path).fileName(),QString::fromUtf8(live->stderrTail).right(500));
            windows.push_back(std::move(live));if(!ready||config.value("spanning").toBool())break;
        }
        lower();QTimer::singleShot(400,this,[this]{lower();});
        if(!error.isEmpty())clear();
        QString ignored;Wallpapers::save(root+"/wallpaper-status.json",{{"ok",error.isEmpty()},{"error",error},{"players",int(windows.size())}},ignored);
        return error.isEmpty();
    }
public:
    explicit DesktopService(QString stateRoot):root(std::move(stateRoot)) {
        display=XOpenDisplay(nullptr);if(display)XSelectInput(display,DefaultRootWindow(display),SubstructureNotifyMask);
        const QString directory=QFileInfo(Wallpapers::socketPath(root)).absolutePath();
        const QFileInfo existing(directory);
        if(existing.isSymLink()||(existing.exists()&&existing.ownerId()!=getuid())){error=tr("The wallpaper socket directory is not owned by this user.");QTimer::singleShot(0,QCoreApplication::instance(),[]{QCoreApplication::exit(1);});return;}
        QDir().mkpath(directory);QFile::setPermissions(directory,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
        QLocalServer::removeServer(Wallpapers::socketPath(root));server.setSocketOptions(QLocalServer::UserAccessOption);
        if(!server.listen(Wallpapers::socketPath(root))){error=tr("Cannot open the wallpaper control socket.");QTimer::singleShot(0,QCoreApplication::instance(),[]{QCoreApplication::exit(1);});return;}
        connect(&server,&QLocalServer::newConnection,this,[this]{while(server.hasPendingConnections()){
            auto *client=server.nextPendingConnection();connect(client,&QLocalSocket::readyRead,this,[this,client]{
                if(!client->canReadLine())return;
                const auto message=QJsonDocument::fromJson(client->readLine()).object();const auto action=message.value("action").toString();
                if(action=="reload")load();
                if(action=="pause")for(auto &w:windows)mpvCommand(w->socket,{"set_property","pause",message.value("paused").toBool()});
                client->write(QJsonDocument(QJsonObject{{"ok",error.isEmpty()},{"error",error},{"players",int(windows.size())},{"active",config.value("active")},{"version",QCoreApplication::applicationVersion()}}).toJson(QJsonDocument::Compact)+'\n');client->flush();client->disconnectFromServer();
            });connect(client,&QLocalSocket::disconnected,client,&QObject::deleteLater);
        }});
        Wallpapers::listDesktopHandler(*settings,true);
        load();poll.setInterval(400);connect(&poll,&QTimer::timeout,this,[this]{
            load();
            if(!config.value("active").toBool()){QCoreApplication::quit();return;}
            const auto uri=unquote(settings->read("org.cinnamon.desktop.background","picture-uri"));
            const auto option=unquote(settings->read("org.cinnamon.desktop.background","picture-options"));
            const bool slideshow=settings->read("org.cinnamon.desktop.background.slideshow","slideshow-enabled")=="true";
            if(uri!=config.value("poster").toString()||option!=config.value("option").toString()||slideshow) {
                if(!externalChange.isValid()){externalChange.start();return;}
                if(externalChange.elapsed()<1200)return;
                // A choice in Cinnamon's regular Backgrounds tool takes over.
                auto stopped=config;stopped["active"]=false;QString ignored;
                Wallpapers::save(Wallpapers::configurationPath(root),stopped,ignored);autostart(false,root,ignored);clear();QCoreApplication::quit();return;
            }
            externalChange.invalidate();
            const auto screens=Wallpapers::displays();
            if(QJsonArray::fromVariantList(screens)!=config.value("screens").toArray()) {
                auto assignments=config.value("assignments").toObject();
                QJsonObject fallback;for(const auto &value:assignments)if(value.isObject()){fallback=value.toObject();break;}
                for(const auto &s:screens){const auto id=s.toMap().value("id").toString();if(!assignments.contains(id))assignments[id]=fallback;}
                QString problem;auto refreshed=Wallpapers::prepare(screens,assignments,root,problem);
                if(!refreshed.isEmpty()) {
                    refreshed["paused"]=config.value("paused");
                    if(assignments.size()>1)refreshed["active"]=true;
                    if(settings->write("org.cinnamon.desktop.background","picture-uri",variantString(refreshed.value("poster").toString()),problem)&&settings->write("org.cinnamon.desktop.background","picture-options",variantString(refreshed.value("option").toString()),problem)) {
                        settings->sync();Wallpapers::save(Wallpapers::configurationPath(root),refreshed,problem);load();
                    }
                }
                if(!problem.isEmpty()){error=problem;QString ignored;Wallpapers::save(root+"/wallpaper-status.json",{{"ok",false},{"error",error}},ignored);}
            }
            if(display){bool mapped=false;while(XPending(display)){XEvent event;XNextEvent(display,&event);mapped|=event.type==MapNotify;}if(mapped)lower();}
            for(auto &w:windows)if(w->process.state()!=QProcess::NotRunning)w->loop.advance(config.value("paused").toBool());
            for(auto &w:windows)if(w->process.state()==QProcess::NotRunning){error=tr("An animated wallpaper player stopped.");QString ignored;Wallpapers::save(root+"/wallpaper-status.json",{{"ok",false},{"error",error}},ignored);}
            retry();
        });poll.start();
    }
    void retry() {
        if(error.isEmpty()||!config.value("active").toBool()){if(error.isEmpty())retries=0;retryWait.invalidate();return;}
        if(signature!=retriedSignature){retriedSignature=signature;retries=0;}
        if(retries>=8)return;
        if(!retryWait.isValid()){retryWait.start();return;}
        if(retryWait.elapsed()<3000*(retries+1))return;
        ++retries;retryWait.invalidate();
        const auto wanted=signature;clear();signature.clear();load();retriedSignature=wanted;
    }
    ~DesktopService() override{clear();server.close();if(display)XCloseDisplay(display);}
};
}
Playback Playback::from(const QJsonObject &choice) {
    Playback playback;
    const double speed=choice.value("speed").toDouble(1.0);const int replay=choice.value("replay").toInt(0);
    if(validSpeed(speed))playback.speed=speed;
    if(validReplay(replay))playback.replay=replay;
    return playback;
}
QStringList Playback::arguments() const {
    return {"--speed="+QString::number(speed,'f',2),QString("--loop-file=")+(replay==0?"inf":"no")};
}
QJsonObject Wallpapers::player(const QString &socket,const QJsonArray &command,int timeout) {return mpvCommand(socket,command,timeout);}

void PlaybackLoop::configure(const QString &socket,const Playback &playback,bool running) {
    m_socket=socket;m_playback=playback;
    if(!running){m_resting=false;return;}
    mpvCommand(socket,{"set_property","speed",playback.speed});
    mpvCommand(socket,{"set_property","loop-file",playback.replay==0?"inf":"no"});
    // Back to continuous looping while resting on the last frame: carry on now.
    if(playback.replay==0&&m_resting)restart();
}
void PlaybackLoop::restart() {
    mpvCommand(m_socket,{"seek",0,"absolute"});mpvCommand(m_socket,{"set_property","pause",false});m_resting=false;
}
void PlaybackLoop::advance(bool paused) {
    if(m_socket.isEmpty()||m_playback.replay==0)return;
    if(!mpvCommand(m_socket,{"get_property","eof-reached"},250).value("data").toBool()){m_resting=false;return;}
    if(!m_resting){m_resting=true;m_ended.start();}
    if(paused||m_playback.replay<0||m_ended.elapsed()<qint64(m_playback.replay)*1000)return;
    restart();
}

int runWallpaperService(const QString &root) {
    QDir().mkpath(root);QLockFile lock(root+"/wallpaper-service.lock");if(!lock.tryLock(100))return 0;
    DesktopService service(root);return QCoreApplication::exec();
}
