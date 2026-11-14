#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QScreen>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include "backend.h"
#include "fetcher.h"
#include "theme.h"
#include "translator.h"

int main(int argc,char **argv) {
    QStringList raw;for(int i=1;i<argc;++i) raw<<QString::fromLocal8Bit(argv[i]);
    const bool gui=raw.isEmpty() || raw.first()=="open" || raw.first()=="wallpaper-service";
    if(!gui) {qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QUICK_BACKEND","software");}
    QGuiApplication app(argc,argv);
    app.setApplicationName("cinnarchy");app.setApplicationVersion("1.3.2");app.setOrganizationName("Cinnarchy");
    app.setDesktopFileName("cinnarchy");app.setWindowIcon(QIcon(":/cinnarchy.svg"));
    QQuickStyle::setStyle("Material");
    QCommandLineParser parser;
    parser.setApplicationDescription("Omarchy Theme Picker for Cinnamon");parser.addHelpOption();parser.addVersionOption();
    parser.addPositionalArgument("command","open, list, status, check, apply SLUG, fetch, undo, redo, restore, reset, pause, stop-animation, screenshot, skill install","[command] [slug]");
    parser.addOption({"json","Write machine-readable output"});
    parser.addOption({"select","Select a theme in the picker","slug"});
    parser.addOption({"background","Choose a background by its 1-based index","number","1"});
    parser.addOption({"no-wallpaper","Keep the existing background"});
    parser.addOption({"no-icons","Keep the existing icon theme"});
    parser.addOption({"fit","Wallpaper fit: zoom, scaled, spanned, centered","fit"});
    parser.addOption({"state-dir","Use another backup/history directory","path"});
    parser.addOption({"theme-root","Use another installation directory","path"});
    parser.addOption({"output","Screenshot destination","path"});
    parser.addOption({"language","Preview a language without changing desktop settings","code"});
    parser.addOption({"display","Wallpaper target: all or a display connector ID","id","all"});
    parser.addOption({"file","Use a local image or animated background","path"});
    parser.addOption({"screen-background","Set DISPLAY=/path/to/image-or-video; repeat for multiple displays","assignment"});
    parser.addOption({"speed","apply: animated background speed from 0.25 to 4; 1 is normal","factor"});
    parser.addOption({"repeat-after","apply: seconds an animated background rests before repeating; 0 loops at once, 'once' plays a single time","seconds"});
    parser.addOption({"layout","Read a display layout JSON array for isolated testing","path"});
    parser.addOption({"url","fetch: install a community theme from its git repository","git-url"});
    parser.addOption({"community","fetch: list the community themes on omarchy.org"});
    parser.addOption({"backgrounds","fetch: download Omarchy's backgrounds for built-in themes that have none"});
    parser.addOption({"repo","fetch: official Omarchy repository to read themes from","git-url",ThemeFetch::officialRepo});
    parser.addOption({"omarchy-themes","Use another folder for downloaded Omarchy themes","path"});
    parser.process(app);
    const auto args=parser.positionalArguments();const QString command=args.value(0,"open");
    if(command=="wallpaper-service")return runWallpaperService(parser.isSet("state-dir")?QDir(parser.value("state-dir")).absolutePath():PickerPaths::defaults().stateRoot);
    auto print=[&](const QJsonValue &value){
        const auto document=value.isArray()?QJsonDocument(value.toArray()):QJsonDocument(value.toObject());
        const auto bytes=document.toJson(parser.isSet("json")?QJsonDocument::Compact:QJsonDocument::Indented);
        std::fwrite(bytes.constData(),1,bytes.size(),stdout);std::fputc('\n',stdout);
    };
    if(command=="skill") {
        if(args.value(1)!="install") {std::fprintf(stderr,"Use: cinnarchy skill install\n");return 1;}
        const auto root=qEnvironmentVariable("CODEX_HOME",QDir::homePath()+"/.codex")+"/skills/cinnarchy";
        QFile input(":/SKILL.md");QSaveFile output(root+"/SKILL.md");
        if(!QDir().mkpath(root) || !input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {std::fprintf(stderr,"Cannot install skill\n");return 1;}
        output.write(input.readAll());if(!output.commit()) return 1;
        print(QJsonObject{{"installed",root+"/SKILL.md"}});return 0;
    }
    QTemporaryDir languageDir;
    QString languageFile;
    if(parser.isSet("language")) {
        languageFile=languageDir.filePath("language");QFile file(languageFile);
        if(!file.open(QIODevice::WriteOnly)) return 1;
        file.write(parser.value("language").toUtf8());file.close();
    }
    Translator translator(languageFile);app.installTranslator(&translator);
    Theme theme;
    auto paths=PickerPaths::defaults();
    if(parser.isSet("state-dir")) paths.stateRoot=QDir(parser.value("state-dir")).absolutePath();
    if(parser.isSet("theme-root")) paths.themeRoot=QDir(parser.value("theme-root")).absolutePath();
    if(parser.isSet("omarchy-themes")){paths.userThemeRoot=QDir(parser.value("omarchy-themes")).absolutePath();paths.stockRoots<<paths.userThemeRoot;}
    if(command=="fetch") {
        if(parser.isSet("community")) {
            QString error;const auto list=ThemeFetch::community(error);print(list);
            if(list.isEmpty()) std::fprintf(stderr,"%s\n",error.toUtf8().constData());
            return list.isEmpty()?1:0;
        }
        const auto result=parser.isSet("url")?ThemeFetch::installRepository(paths.fetchPaths(),parser.value("url"))
                         :parser.isSet("backgrounds")?ThemeFetch::downloadBackgrounds(paths.fetchPaths(),{},parser.value("repo"))
                         :ThemeFetch::syncOfficial(paths.fetchPaths(),parser.value("repo"));
        print(result.toJson());
        if(!result.ok) std::fprintf(stderr,"%s\n",result.error.toUtf8().constData());
        return result.ok?0:1;
    }
    if(parser.isSet("layout")){QFile f(parser.value("layout"));if(!f.open(QIODevice::ReadOnly))return 1;paths.displayOverrides=QJsonDocument::fromJson(f.readAll()).array().toVariantList();}
    Backend backend(nullptr,paths);
    backend.setTargetDisplay(parser.value("display"));
    if(backend.targetDisplay()!=parser.value("display")){std::fprintf(stderr,"Unknown display connector\n");return 1;}
    if(parser.isSet("fit")&&!QStringList{"zoom","scaled","spanned","centered"}.contains(parser.value("fit"))){std::fprintf(stderr,"Unknown wallpaper fit\n");return 1;}
    if(parser.value("fit")=="spanned"&&backend.targetDisplay()!="all"){std::fprintf(stderr,"Span screens requires --display all\n");return 1;}
    if(parser.isSet("select")) backend.selectTheme(parser.value("select"));
    if(parser.isSet("fit")) backend.setPictureOption(parser.value("fit"));
    if(parser.isSet("no-wallpaper")) backend.setUseWallpaper(false);
    if(parser.isSet("no-icons")) backend.setUseIcons(false);
    if(command=="list") {print(QJsonArray::fromVariantList(backend.themes()));return 0;}
    if(command=="status") {print(backend.status());return 0;}
    if(command=="check") {
        QJsonArray missing;
        for(const auto &value:backend.themes()) if(value.toMap().value("wallpapers").toList().isEmpty()) missing.append(value.toMap().value("slug").toString());
        const bool ready=!backend.themes().isEmpty() && missing.isEmpty() && !backend.snapshot().isEmpty();
        print(QJsonObject{{"ok",ready},{"themes",backend.themes().size()},{"missingBackgrounds",missing}});return ready?0:1;
    }
    if(command=="apply" || command=="undo" || command=="redo" || command=="restore" || command=="reset" || command=="pause" || command=="stop-animation") {
        bool success=false;
        if(command=="apply") {
            const auto slug=args.value(1);
            bool speedValid=true,repeatValid=true;
            const double speed=parser.isSet("speed")?parser.value("speed").toDouble(&speedValid):1.0;
            const int repeat=!parser.isSet("repeat-after")?0:parser.value("repeat-after")=="once"?-1:parser.value("repeat-after").toInt(&repeatValid);
            if(!speedValid||!Playback::validSpeed(speed)||!repeatValid||!Playback::validReplay(repeat)||(parser.isSet("repeat-after")&&repeat<0&&parser.value("repeat-after")!="once")) {
                std::fprintf(stderr,"Use --speed 0.25 to 4 and --repeat-after SECONDS or 'once'\n");return 1;
            }
            auto tune=[&]{if(parser.isSet("speed"))backend.setPlaybackSpeed(speed);if(parser.isSet("repeat-after"))backend.setReplayDelay(repeat);};
            if(parser.isSet("screen-background")){const auto first=parser.values("screen-background").first();backend.setTargetDisplay(first.left(first.indexOf('=')));}
            backend.selectTheme(slug);
            bool valid=false;const int index=parser.value("background").toInt(&valid)-1;
            if(slug!=backend.selectedSlug() || !valid || index<0 || (!parser.isSet("file")&&!parser.isSet("screen-background")&&backend.useWallpaper() && index>=backend.selected().value("wallpapers").toList().size())) {std::fprintf(stderr,"Unknown theme or invalid background index\n");return 1;}
            if(parser.isSet("file"))backend.addMedia({QUrl::fromLocalFile(QFileInfo(parser.value("file")).absoluteFilePath()).toString()});
            else if(!parser.isSet("screen-background"))backend.selectWallpaper(index);
            for(const auto &assignment:parser.values("screen-background")){
                const int split=assignment.indexOf('=');const auto id=assignment.left(split);backend.setTargetDisplay(id);
                if(split<1||backend.targetDisplay()!=id){std::fprintf(stderr,"Invalid display assignment\n");return 1;}
                backend.addMedia({QUrl::fromLocalFile(QFileInfo(assignment.mid(split+1)).absoluteFilePath()).toString()});tune();
            }
            if(!parser.isSet("screen-background"))tune();
            if(backend.error())success=false;else success=backend.applyNow();
        } else if(command=="undo") success=backend.undoNow();
        else if(command=="redo") success=backend.redoNow();
        else if(command=="pause"){backend.pauseAnimations();success=!backend.error();}
        else if(command=="stop-animation"){backend.useSystemWallpaper();success=!backend.error();}
        else if(command=="reset") success=backend.resetNow();
        else success=backend.restoreNow();
        print(backend.status());if(!success) std::fprintf(stderr,"%s\n",backend.message().isEmpty()?"No saved setup is available":backend.message().toUtf8().constData());return success?0:1;
    }
    if(command!="open" && command!="screenshot") {std::fprintf(stderr,"Unknown command\n");return 1;}
    if(parser.isSet("file"))backend.addMedia({QUrl::fromLocalFile(QFileInfo(parser.value("file")).absoluteFilePath()).toString()});
    QQmlApplicationEngine engine;
    QObject::connect(&translator,&Translator::changed,&engine,&QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("backend",&backend);
    engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("translator",&translator);
    if(command=="open") if(const auto *screen=QGuiApplication::primaryScreen()) {
        // Open inside the screen on small laptops; the options area scrolls when short.
        const QSize room=screen->availableSize()-QSize(24,64);
        if(room.width()>0&&room.height()>0) engine.setInitialProperties({{"width",qMin(1180,room.width())},{"height",qMin(960,room.height())}});
    }
    engine.load(QUrl("qrc:/Main.qml"));if(engine.rootObjects().isEmpty()) return 1;
    backend.setWindow(qobject_cast<QQuickWindow*>(engine.rootObjects().first()));
    if(command=="screenshot") {
        if(parser.value("output").isEmpty()) {std::fprintf(stderr,"--output is required\n");return 1;}
        auto *captureTimer=new QTimer(&app);captureTimer->setInterval(1800);
        QObject::connect(captureTimer,&QTimer::timeout,&app,[&,captureTimer,attempt=0]()mutable{
            auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            auto *poster=window?window->findChild<QObject*>("mediaPoster"):nullptr;
            // Allow the selected full-size image to finish after gallery thumbnails.
            if(poster&&poster->property("status").toInt()==2&&attempt++<80){captureTimer->setInterval(100);return;}
            captureTimer->stop();
            const bool saved=window && window->grabWindow().save(parser.value("output"));
            print(QJsonObject{{"saved",saved},{"path",parser.value("output")}});app.exit(saved?0:1);
        });
        captureTimer->start();
    }
    return app.exec();
}
