#include <QtTest>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickView>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QProcess>
#include <QUrlQuery>
#include "backend.h"
#include "fetcher.h"
#include "generator.h"
#include "theme.h"
#include "translator.h"

class FakeSettings final:public SettingsStore {
public:
    QMap<QString,QString> values;
    QSet<QString> locked;
    int failAt=-1,writes=0;
    FakeSettings() {
        values={{"org.cinnamon.theme/name","'cinnamon'"},{"org.cinnamon.desktop.interface/gtk-theme","'Adwaita'"},
            {"org.cinnamon.desktop.interface/icon-theme","'Adwaita'"},{"org.cinnamon.desktop.wm.preferences/theme","'Adwaita'"},
            {"org.cinnamon.desktop.background/picture-uri","'file:///previous%20background.png'"},
            {"org.cinnamon.desktop.background/picture-options","'scaled'"},{"org.cinnamon.desktop.background/picture-opacity","80"},
            {"org.cinnamon.desktop.background/primary-color","'#123456'"},{"org.cinnamon.desktop.background/secondary-color","'#987654'"},
            {"org.cinnamon.desktop.background/color-shading-type","'vertical'"},
            {"org.cinnamon.desktop.background.slideshow/slideshow-enabled","true"},{"org.cinnamon.desktop.background.slideshow/slideshow-paused","true"},
            {"org.x.apps.portal/color-scheme","'default'"},{"org.x.apps.portal/accent-rgb","''"}};
    }
    bool has(const QString&s,const QString&k) const override{return values.contains(s+"/"+k);}
    bool writable(const QString&s,const QString&k) const override{return has(s,k)&&!locked.contains(s+"/"+k);}
    QString read(const QString&s,const QString&k) const override{return values.value(s+"/"+k);}
    bool write(const QString&s,const QString&k,const QString&v,QString&e) override {
        if(++writes==failAt) {failAt=-1;e="Injected settings failure";return false;}
        if(!writable(s,k)){e="Locked";return false;}values[s+"/"+k]=v;return true;
    }
    void sync() override{}
};

struct Fixture {
    QTemporaryDir dir;
    FakeSettings settings;
    PickerPaths paths;
    Fixture() {
        paths={dir.filePath("themes"),dir.filePath("state"),{dir.filePath("stock")},{dir.filePath("extra")},{dir.filePath("icons")}};
        paths.wallpaperRuntime=false;
        paths.displayOverrides={QVariantMap{{"id","Test-1"},{"name","Test display"},{"x",0},{"y",0},{"width",320},{"height",180},{"primary",true}}};
        QFile manifest(":/themes/manifest.json");manifest.open(QIODevice::ReadOnly);
        QImage image(64,40,QImage::Format_RGB32);image.fill(QColor("#335577"));
        for(const auto &t:QJsonDocument::fromJson(manifest.readAll()).array()) {
            const auto slug=t.toObject().value("slug").toString();const auto path=paths.stockRoots[0]+"/"+slug;
            QDir().mkpath(path+"/backgrounds");
            image.save(path+"/backgrounds/1-first.png");image.save(path+"/backgrounds/2-second picture.png");
            QFile icons(path+"/icons.theme");icons.open(QIODevice::WriteOnly);icons.write("Yaru-blue\n");
        }
        for(const auto &name:{"Adwaita","Yaru-blue","Papirus-Dark","Papirus-Light"}) {
            const auto path=paths.iconRoots[0]+"/"+name;QDir().mkpath(path);QFile f(path+"/index.theme");f.open(QIODevice::WriteOnly);f.write("[Icon Theme]\nName=Example\n");
        }
    }
};

class FakePicker final:public FilePicker {
public:
    QStringList urls;int calls=0;
    void choose(const QString &) override {++calls;emit chosen(urls);}
};

namespace {
void writeFile(const QString &path,const QByteArray &bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(bytes);}
bool run(const QString &dir,const QStringList &args){QProcess p;p.setWorkingDirectory(dir);p.start("git",QStringList{"-c","user.name=Test","-c","user.email=test@example.invalid","-c","commit.gpgsign=false","-c","init.defaultBranch=main"}+args);return p.waitForFinished(30000)&&p.exitCode()==0;}
QByteArray colors(const QString &accent,const QString &background="#202020"){
    return QString("# comment\naccent = \"%1\"\nbackground = '%2' # inline\nforeground = \"#e0e0e0\"\ncolor1 = \"#cc3333\"\ncolor2 = \"#33cc33\"\ncolor3 = \"#cccc33\"\n").arg(accent,background).toUtf8();
}
void addTheme(const QString &dir,const QByteArray &palette){
    writeFile(dir+"/colors.toml",palette);writeFile(dir+"/icons.theme","Yaru-blue\n");
    QImage image(32,20,QImage::Format_RGB32);image.fill(Qt::darkCyan);QDir().mkpath(dir+"/backgrounds");image.save(dir+"/backgrounds/1-one.png");
}
}

class PickerTests:public QObject {
    Q_OBJECT
private slots:
    void generatorReproducesEveryBundledTheme() {
        QFile manifest(":/themes/manifest.json");QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto list=QJsonDocument::fromJson(manifest.readAll()).array();QCOMPARE(list.size(),23);
        for(const auto &value:list) {
            const auto theme=value.toObject();const auto name=theme.value("name").toString();
            ThemeGenerator::Palette raw;const auto p=theme.value("palette").toObject();
            for(auto it=p.begin();it!=p.end();++it)raw[it.key()]=it.value().toString();
            QString error;const auto roles=ThemeGenerator::roles(ThemeGenerator::resolve(raw,false),error);
            QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(roles,theme.value("roles").toObject());
            QDirIterator files(":/template",QDir::Files|QDir::Hidden,QDirIterator::Subdirectories);int count=0;
            while(files.hasNext()) {
                const auto path=files.next();const auto relative=path.mid(QString(":/template/").size());
                QFile templ(path),bundled(":/themes/"+name+"/"+relative);QVERIFY(templ.open(QIODevice::ReadOnly));QVERIFY2(bundled.open(QIODevice::ReadOnly),qPrintable(relative));
                const auto bytes=templ.readAll();const bool binary=relative.endsWith(".png");
                QVERIFY2((binary?bytes:ThemeGenerator::render(bytes,roles,name,theme.value("title").toString()))==bundled.readAll(),qPrintable(name+"/"+relative));++count;
            }
            QCOMPARE(count,28);
        }
    }
    void omarchyPaletteFallbacks() {
        auto c=ThemeGenerator::resolve(ThemeGenerator::parseColorsToml(colors("#4488FF")),false);
        QCOMPARE(c.value("accent"),QString("#4488ff"));QCOMPARE(c.value("background"),QString("#202020"));
        QCOMPARE(c.value("red"),QString("#cc3333"));QCOMPARE(c.value("lighter_background"),QString("#202020"));
        QCOMPARE(c.value("dark_background"),QString("#181818"));QCOMPARE(c.value("darker_background"),QString("#101010"));
        QCOMPARE(c.value("mode"),QString("dark"));
        QCOMPARE(ThemeGenerator::resolve(ThemeGenerator::parseColorsToml(colors("#4488ff","#f0f0f0")),false).value("mode"),QString("light"));
        QTemporaryDir dir;writeFile(dir.filePath("alacritty.toml"),
            "[colors.primary]\nbackground = \"#fafafa\"\nforeground = '0x101010'\n[colors.normal]\nblack='#000000'\nred='#aa0000'\ngreen='#00aa00'\n"
            "yellow='#aaaa00'\nblue='#0000aa'  # note\nmagenta='#aa00aa'\ncyan='#00aaaa'\nwhite='#aaaaaa'\n");
        writeFile(dir.filePath("light.mode"),"");
        QString error;c=ThemeGenerator::readThemeColors(dir.path(),error);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(c.value("accent"),QString("#0000aa"));QCOMPARE(c.value("foreground"),QString("#101010"));QCOMPARE(c.value("mode"),QString("light"));
        QVERIFY(!ThemeGenerator::roles(c,error).isEmpty());
        QTemporaryDir empty;QVERIFY(ThemeGenerator::readThemeColors(empty.path(),error).isEmpty());QVERIFY(!error.isEmpty());
    }
    void repositoryNamesAndUnsafeUrls() {
        QCOMPARE(ThemeFetch::nameFromUrl("https://github.com/a/omarchy-Osaka-Jade-theme.git"),QString("osaka-jade"));
        QCOMPARE(ThemeFetch::nameFromUrl("https://github.com/a/omarchy-synthwave84-theme/"),QString("synthwave84"));
        QCOMPARE(ThemeFetch::nameFromUrl("git@github.com:omarchy-blue-theme.git"),QString("blue"));
        QCOMPARE(ThemeFetch::nameFromUrl("https://github.com/a/.."),QString());
        QCOMPARE(ThemeGenerator::slugFor("vitesse_dark.soft"),QString("vitesse-dark-soft"));
        QString error;
        for(const auto &bad:{"","-uattack","ext::sh -c id","fd::3","gopher://example.com/x","https://example.com/a b"})QVERIFY2(!ThemeFetch::safeUrl(bad,error),bad);
        for(const auto &good:{"https://github.com/a/b","git@github.com:a/b.git","ssh://host/b","file:///tmp/b"})QVERIFY2(ThemeFetch::safeUrl(good,error),good);
        Fixture f;const auto result=ThemeFetch::installRepository(f.paths.fetchPaths(),"https://github.com/someone/omarchy-nord-theme");
        QVERIFY(!result.ok);QVERIFY(result.error.contains("Nord"));
    }
    void communityCatalogIsParsed() {
        const QString html=R"(<ul><li><a href="https://github.com/x/omarchy-synthwave84-theme/" class="g"><img src="/assets/themes/synthwave-84.webp" alt="s"/><span class="m">Synthwave &#x27;84</span></a></li>)"
            R"(<li><a href="ext::sh" class="g"><img src="/a.webp"/><span>Bad</span></a></li>)"
            R"(<li><a href="https://github.com/y/aetheria"><img src="https://cdn.example/a.webp"/><span>Aetheria</span></a></li></ul>)";
        const auto list=ThemeFetch::parseCommunity(html,"https://omarchy.org/themes/");QCOMPARE(list.size(),2);
        QCOMPARE(list[0].toObject().value("name").toString(),QString("Synthwave '84"));
        QCOMPARE(list[0].toObject().value("slug").toString(),QString("synthwave84"));
        QCOMPARE(list[0].toObject().value("image").toString(),QString("https://omarchy.org/assets/themes/synthwave-84.webp"));
        QCOMPARE(list[1].toObject().value("slug").toString(),QString("aetheria"));
    }
    void officialThemesAreFetchedOnceAndUpdated() {
        Fixture f;QTemporaryDir remote;const auto repo=remote.filePath("omarchy");
        addTheme(repo+"/themes/nord",colors("#000001"));addTheme(repo+"/themes/new-dawn",colors("#ff8800"));
        writeFile(repo+"/themes/Bad Name/colors.toml",colors("#123456"));writeFile(repo+"/bin/omarchy","#!/bin/sh\n");
        QVERIFY(run(repo,{"init","-q"}));QVERIFY(run(repo,{"add","-A"}));QVERIFY(run(repo,{"commit","-qm","themes"}));
        const auto url=QUrl::fromLocalFile(repo).toString();
        auto result=ThemeFetch::syncOfficial(f.paths.fetchPaths(),url);
        QVERIFY2(result.ok,qPrintable(result.error));QCOMPARE(result.added,QStringList{"new-dawn"});QCOMPARE(result.skipped,QStringList{"Bad Name"});
        const auto installed=f.paths.stockRoots[0]+"/new-dawn";
        QVERIFY(QFileInfo::exists(installed+"/backgrounds/1-one.png"));QVERIFY(QFileInfo::exists(installed+"/.cinnarchy-source"));
        QVERIFY(!QFileInfo::exists(f.paths.stockRoots[0]+"/nord/colors.toml"));
        QVERIFY(QFileInfo::exists(f.paths.themeRoot+"/Omarchy-new-dawn/cinnamon/cinnamon.css"));
        {QFile css(f.paths.themeRoot+"/Omarchy-new-dawn/gtk-3.0/gtk.css");QVERIFY(css.open(QIODevice::ReadOnly));const auto text=css.readAll();
         QVERIFY(text.contains("@define-color theme_selected_bg_color #ff8800;"));QVERIFY(!text.contains("{{"));}
        result=ThemeFetch::syncOfficial(f.paths.fetchPaths(),url);QVERIFY(result.ok);QVERIFY(result.added.isEmpty()&&result.updated.isEmpty());
        writeFile(repo+"/themes/new-dawn/colors.toml",colors("#00ff88"));QVERIFY(run(repo,{"commit","-qam","recolor"}));
        result=ThemeFetch::syncOfficial(f.paths.fetchPaths(),url);QCOMPARE(result.updated,QStringList{"new-dawn"});
        Backend b(nullptr,f.paths,&f.settings);QCOMPARE(b.themes().size(),24);b.selectTheme("new-dawn");
        QCOMPARE(b.selected().value("title").toString(),QString("New Dawn"));QCOMPARE(b.selected().value("roles").toMap().value("accent").toString(),QString("#00ff88"));
        QCOMPARE(b.selected().value("wallpapers").toList().size(),1);QVERIFY(b.applyNow());
        QCOMPARE(unquote(f.settings.read("org.cinnamon.theme","name")),QString("Omarchy-new-dawn"));
    }
    void communityThemeInstallsInTheBackground() {
        Fixture f;QTemporaryDir remote;const auto repo=remote.filePath("omarchy-Sun_Rise-theme");
        writeFile(repo+"/alacritty.toml","[colors.primary]\nbackground='#f4f0e8'\nforeground='#202020'\n[colors.normal]\nblack='#000000'\nred='#aa2222'\n"
            "green='#22aa22'\nyellow='#aaaa22'\nblue='#2244aa'\nmagenta='#aa22aa'\ncyan='#22aaaa'\nwhite='#dddddd'\n");
        QImage image(32,20,QImage::Format_RGB32);image.fill(Qt::yellow);QDir().mkpath(repo+"/backgrounds");image.save(repo+"/backgrounds/sun.png");
        QVERIFY(QFile::link(QDir::homePath(),repo+"/escape"));
        QVERIFY(run(repo,{"init","-q"}));QVERIFY(run(repo,{"add","-A"}));QVERIFY(run(repo,{"commit","-qm","theme"}));
        Backend b(nullptr,f.paths,&f.settings);QSignalSpy finished(&b,&Backend::fetchFinished);
        b.installTheme(QUrl::fromLocalFile(repo).toString());QVERIFY(b.fetching());
        QVERIFY(finished.wait(30000));QTRY_VERIFY(!b.fetching());
        QVERIFY2(!b.error(),qPrintable(b.message()));QCOMPARE(b.selectedSlug(),QString("sun-rise"));QCOMPARE(b.selected().value("mode").toString(),QString("light"));
        QVERIFY(QFileInfo::exists(f.paths.stockRoots[0]+"/sun-rise/backgrounds/sun.png"));
        QVERIFY(!QFileInfo(f.paths.stockRoots[0]+"/sun-rise/escape").isSymLink());QVERIFY(!QFileInfo::exists(f.paths.stockRoots[0]+"/sun-rise/.git"));
        b.installTheme(QUrl::fromLocalFile(repo).toString());QVERIFY(finished.wait(30000));QVERIFY(b.message().contains("Updated"));
        b.installTheme("ext::sh -c id");QVERIFY(finished.wait(30000));QVERIFY(b.error());
    }
    void installedOmarchyThemeGetsACinnamonTheme() {
        Fixture f;addTheme(f.paths.stockRoots[0]+"/hand-made",colors("#abcdef"));
        const auto result=ThemeFetch::generateMissing(f.paths.fetchPaths());QCOMPARE(result.generated,QStringList{"hand-made"});
        QVERIFY(ThemeFetch::generateMissing(f.paths.fetchPaths()).generated.isEmpty());
        Backend b(nullptr,f.paths,&f.settings);QCOMPARE(b.themes().size(),24);
        QDir().mkpath(f.paths.themeRoot+"/Omarchy-mine");writeFile(f.paths.themeRoot+"/Omarchy-mine/keep.txt","x");
        QString error;QVERIFY(!ThemeGenerator::generate(f.paths.themeRoot,"mine","Mine",ThemeGenerator::resolve(ThemeGenerator::parseColorsToml(colors("#abcdef")),false),{},error));
        QVERIFY(QFileInfo::exists(f.paths.themeRoot+"/Omarchy-mine/keep.txt"));
    }
    void mediaPreviewRendersPoster() {
        Fixture f;const auto path=f.paths.stockRoots.first()+"/catppuccin/backgrounds/1-first.png";
        const auto url=QUrl::fromLocalFile(path).toString();QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);view.resize(320,180);
        view.setInitialProperties({{"source",url},{"media",QVariantMap{{"mediaType","video"},{"thumbnail",url}}}});
        view.setSource(QUrl("qrc:/MediaBackdrop.qml"));QCOMPARE(view.status(),QQuickView::Ready);view.show();
        QTRY_COMPARE_WITH_TIMEOUT(view.grabWindow().pixelColor(10,10),QColor("#335577"),3000);
        view.close();
    }
    void separateWallpapersComposeAndRestore() {
        Fixture f;
        f.paths.displayOverrides={QVariantMap{{"id","DP-1"},{"name","Left"},{"x",-160},{"y",0},{"width",160},{"height",120}},QVariantMap{{"id","HDMI-1"},{"name","Right"},{"x",0},{"y",20},{"width",80},{"height",60}}};
        const auto folder=f.paths.stockRoots.first()+"/catppuccin/backgrounds/";
        QImage red(64,40,QImage::Format_RGB32);red.fill(Qt::red);QVERIFY(red.save(folder+"1-first.png"));
        QImage blue(64,40,QImage::Format_RGB32);blue.fill(Qt::blue);QVERIFY(blue.save(folder+"2-second picture.png"));
        const auto original=f.settings.values;Backend b(nullptr,f.paths,&f.settings);b.selectTheme("catppuccin");
        b.setTargetDisplay("DP-1");b.selectWallpaper(0);b.setTargetDisplay("HDMI-1");b.selectWallpaper(1);
        QCOMPARE(b.displayAssignments().size(),2);QVERIFY(b.applyNow());
        const auto uri=unquote(f.settings.read("org.cinnamon.desktop.background","picture-uri"));
        QImage combined(QUrl(uri).toLocalFile());QCOMPARE(combined.size(),QSize(240,120));
        QCOMPARE(combined.pixelColor(10,10),QColor(Qt::red));QCOMPARE(combined.pixelColor(170,30),QColor(Qt::blue));QCOMPARE(combined.pixelColor(170,105),QColor(Qt::black));
        QCOMPARE(unquote(f.settings.read("org.cinnamon.desktop.background","picture-options")),QString("spanned"));
        QVERIFY(b.undoNow());QCOMPARE(f.settings.values,original);QVERIFY(b.redoNow());
        Backend reopened(nullptr,f.paths,&f.settings);reopened.setTargetDisplay("DP-1");QCOMPARE(reopened.wallpaperIndex(),0);reopened.setTargetDisplay("HDMI-1");QCOMPARE(reopened.wallpaperIndex(),1);
    }
    void targetingOneScreenPreservesTheOther() {
        Fixture f;auto second=f.paths.displayOverrides.first().toMap();second["id"]="Test-2";second["x"]=320;f.paths.displayOverrides<<second;
        Backend b(nullptr,f.paths,&f.settings);const auto first=b.displayAssignments().first().toMap().value("url").toString();
        b.setTargetDisplay("Test-2");b.selectWallpaper(1);
        QCOMPARE(b.displayAssignments().first().toMap().value("url").toString(),first);QVERIFY(b.wallpaperUrl()!=first);
        b.setTargetDisplay("all");b.selectWallpaper(1);QCOMPARE(b.displayAssignments().first().toMap().value("url"),b.displayAssignments().last().toMap().value("url"));
    }
    void importedBackgroundTargetsOneScreenAndPersists() {
        Fixture f;auto second=f.paths.displayOverrides.first().toMap();second["id"]="Test-2";second["x"]=320;f.paths.displayOverrides<<second;
        const auto path=f.dir.filePath("custom picture.png");QImage image(80,60,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(path));
        FakePicker picker;picker.urls={QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)};Backend b(nullptr,f.paths,&f.settings,&picker);
        const auto first=b.displayAssignments().first().toMap().value("url");
        b.setTargetDisplay("Test-2");b.chooseMedia();QCOMPARE(picker.calls,1);QCOMPARE(b.wallpaperUrl(),picker.urls.first());
        QCOMPARE(b.displayAssignments().first().toMap().value("url"),first);QVERIFY(b.applyNow());
        Backend reopened(nullptr,f.paths,&f.settings);reopened.setTargetDisplay("Test-2");QCOMPARE(reopened.wallpaperUrl(),picker.urls.first());
        QVERIFY(!reopened.wallpaperMedia().isEmpty());
    }
    void aspectFitKeepsLetterboxing() {
        Fixture f;const auto path=f.dir.filePath("wide.png");QImage wide(200,50,QImage::Format_RGB32);wide.fill(Qt::green);QVERIFY(wide.save(path));
        QVariantList displays{QVariantMap{{"id","one"},{"x",0},{"y",0},{"width",100},{"height",100}}};
        QJsonObject choices{{"one",QJsonObject{{"url",QUrl::fromLocalFile(path).toString()},{"fit","scaled"}}}};QString error;
        auto image=Wallpapers::compose(displays,choices,f.paths.stateRoot,error);QVERIFY2(!image.isNull(),qPrintable(error));
        QCOMPARE(image.pixelColor(50,0),QColor(Qt::black));QCOMPARE(image.pixelColor(50,50),QColor(Qt::green));
        choices["one"]=QJsonObject{{"url",QUrl::fromLocalFile(path).toString()},{"fit","zoom"}};
        image=Wallpapers::compose(displays,choices,f.paths.stateRoot,error);QCOMPARE(image.pixelColor(50,0),QColor(Qt::green));
    }
    void animatedVideoAndGifAreDiscoveredAndPrepared() {
        Fixture f;const auto dir=f.paths.stockRoots.first()+"/catppuccin/backgrounds/intros";QDir().mkpath(dir);
        const auto video=dir+"/sample.mp4",gif=dir+"/sample.gif",webp=dir+"/sample.webp";
        QProcess ffmpeg;ffmpeg.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","testsrc2=size=96x64:rate=5:duration=1","-threads","1","-y",video});QVERIFY(ffmpeg.waitForFinished(10000));QCOMPARE(ffmpeg.exitCode(),0);
        ffmpeg.start("ffmpeg",{"-nostdin","-v","error","-i",video,"-threads","1","-y",gif});QVERIFY(ffmpeg.waitForFinished(10000));QCOMPARE(ffmpeg.exitCode(),0);
        ffmpeg.start("ffmpeg",{"-nostdin","-v","error","-i",video,"-threads","1","-y",webp});QVERIFY(ffmpeg.waitForFinished(10000));QCOMPARE(ffmpeg.exitCode(),0);
        QVERIFY(Wallpapers::isAnimated(video));QVERIFY(Wallpapers::isAnimated(gif));QVERIFY(Wallpapers::isAnimated(webp));
        Backend b(nullptr,f.paths,&f.settings);b.selectTheme("catppuccin");QCOMPARE(b.selected().value("wallpapers").toList().size(),5);
        b.addMedia({QUrl::fromLocalFile(video).toString()});QVERIFY(b.wallpaperMedia().value("animated").toBool());QVERIFY(b.applyNow());
        QVERIFY(b.hasAnimations());QVERIFY(QFileInfo::exists(QUrl(unquote(f.settings.read("org.cinnamon.desktop.background","picture-uri"))).toLocalFile()));
        b.pauseAnimations();QVERIFY(b.animationsPaused());b.pauseAnimations();QVERIFY(!b.animationsPaused());b.useSystemWallpaper();QVERIFY(!b.hasAnimations());QVERIFY(b.undoNow());QVERIFY(b.hasAnimations());
        auto screens=f.paths.displayOverrides;auto second=screens.first().toMap();second["id"]="Test-2";second["x"]=320;screens<<second;
        const QJsonObject shared{{"url",QUrl::fromLocalFile(gif).toString()},{"fit","spanned"}};QString problem;
        const auto spanned=Wallpapers::prepare(screens,{{"Test-1",shared},{"Test-2",shared}},f.paths.stateRoot,problem);
        QVERIFY2(spanned.value("spanning").toBool(),qPrintable(problem));
        QCOMPARE(QImage(QUrl(spanned.value("poster").toString()).toLocalFile()).size(),QSize(640,180));
    }
    void catalogAndSafeInstallation() {
        Fixture f;Backend b(nullptr,f.paths,&f.settings);QCOMPARE(b.themes().size(),23);
        b.selectTheme("tokyo-night");QCOMPARE(b.selected().value("wallpapers").toList().size(),2);
        QVERIFY(b.applyNow());
        QVERIFY(QFileInfo::exists(f.paths.themeRoot+"/Omarchy-tokyo-night/cinnamon/toggle-on.svg"));
        QVERIFY(QFileInfo::exists(f.paths.themeRoot+"/Omarchy-tokyo-night/.omarchy-cinnamon"));
        QCOMPARE(unquote(f.settings.read("org.cinnamon.theme","name")),QString("Omarchy-tokyo-night"));
    }
    void fullSwitchAndUndoRedoRestore() {
        Fixture f;const auto original=f.settings.values;Backend b(nullptr,f.paths,&f.settings);
        b.selectTheme("catppuccin");b.selectWallpaper(1);QVERIFY(b.applyNow());const auto applied=f.settings.values;
        QCOMPARE(unquote(f.settings.read("org.cinnamon.desktop.interface","icon-theme")),QString("Yaru-blue"));
        QCOMPARE(f.settings.read("org.cinnamon.desktop.background.slideshow","slideshow-enabled"),QString("false"));
        QVERIFY(unquote(f.settings.read("org.cinnamon.desktop.background","picture-uri")).contains("2-second%20picture.png"));
        QCOMPARE(unquote(f.settings.read("org.x.apps.portal","color-scheme")),QString("prefer-dark"));
        QCOMPARE(unquote(f.settings.read("org.x.apps.portal","accent-rgb")),QString("#89b4fa"));
        QVERIFY(b.undoNow());QCOMPARE(f.settings.values,original);
        QVERIFY(b.redoNow());QCOMPARE(f.settings.values,applied);
        b.selectTheme("flexoki-light");QVERIFY(b.applyNow());
        QCOMPARE(unquote(f.settings.read("org.x.apps.portal","color-scheme")),QString("prefer-light"));
        QVERIFY(b.restoreNow());QCOMPARE(f.settings.values,original);
    }
    void historySurvivesRestart() {
        Fixture f;const auto original=f.settings.values;
        {Backend b(nullptr,f.paths,&f.settings);b.selectTheme("nord");b.selectWallpaper(1);QVERIFY(b.applyNow());}
        Backend b(nullptr,f.paths,&f.settings);QVERIFY(b.canUndo());QCOMPARE(b.wallpaperIndex(),1);
        QVERIFY(b.restoreNow());QCOMPARE(f.settings.values,original);
    }
    void rollbackAfterPartialFailure() {
        Fixture f;const auto original=f.settings.values;Backend b(nullptr,f.paths,&f.settings);
        f.settings.failAt=5;QVERIFY(!b.applyNow());QCOMPARE(f.settings.values,original);
        QVERIFY(!b.canUndo());QVERIFY(!QFileInfo::exists(f.paths.stateRoot+"/pending.json"));
    }
    void lockedSettingChangesNothing() {
        Fixture f;const auto original=f.settings.values;Backend b(nullptr,f.paths,&f.settings);
        f.settings.locked.insert("org.cinnamon.desktop.interface/gtk-theme");
        QVERIFY(!b.applyNow());QCOMPARE(f.settings.writes,0);QCOMPARE(f.settings.values,original);
    }
    void optionalComponentsArePreserved() {
        Fixture f;const auto original=f.settings.values;Backend b(nullptr,f.paths,&f.settings);
        b.setUseWallpaper(false);b.setUseIcons(false);QVERIFY(b.applyNow());
        for(auto it=original.begin();it!=original.end();++it)
            if(it.key().contains("background")||it.key().contains("icon-theme")) QCOMPARE(f.settings.values.value(it.key()),it.value());
    }
    void unavailableBackgroundIsReported() {
        Fixture f;Backend b(nullptr,f.paths,&f.settings);const auto original=f.settings.values;
        QFile::remove(QUrl(b.wallpaperUrl()).toLocalFile());QVERIFY(!b.applyNow());
        QCOMPARE(f.settings.values,original);QVERIFY(b.error());
    }
    void customBackgroundsAndFallbackIcons() {
        Fixture f;const auto extra=f.paths.extraBackgroundRoots[0]+"/catppuccin";QDir().mkpath(extra);
        QImage image(30,30,QImage::Format_RGB32);image.fill(Qt::red);image.save(extra+"/custom.png");
        QFile icons(f.paths.stockRoots[0]+"/catppuccin/icons.theme");icons.open(QIODevice::WriteOnly);icons.write("Missing-pack");icons.close();
        Backend b(nullptr,f.paths,&f.settings);b.selectTheme("catppuccin");
        QCOMPARE(b.selected().value("wallpapers").toList().size(),3);
        QCOMPARE(b.selected().value("icons").toString(),QString("Papirus-Dark"));QVERIFY(!b.selected().value("iconNote").toString().isEmpty());
    }
    void unrelatedThemeFolderIsNotReplaced() {
        Fixture f;Backend b(nullptr,f.paths,&f.settings);const auto path=f.paths.themeRoot+"/Omarchy-catppuccin";QDir().mkpath(path);
        QFile file(path+"/personal.txt");file.open(QIODevice::WriteOnly);file.write("keep me");file.close();
        QVERIFY(!b.applyNow());QVERIFY(QFileInfo::exists(path+"/personal.txt"));
    }
    void oldGeneratedThemeIsUpgradedWithBackup() {
        Fixture f;Backend b(nullptr,f.paths,&f.settings);b.selectTheme("catppuccin");QVERIFY(b.applyNow());
        const auto path=f.paths.themeRoot+"/Omarchy-catppuccin";
        QFile marker(path+"/.omarchy-cinnamon");QVERIFY(marker.open(QIODevice::WriteOnly));marker.write("Generated by omarchy-cinnamon v1\n");marker.close();
        QFile css(path+"/gtk-3.0/gtk.css");QVERIFY(css.open(QIODevice::WriteOnly));css.write("/* old desktop-covering stylesheet */");css.close();
        const auto before=f.settings.values;QVERIFY(b.applyNow());QCOMPARE(f.settings.values,before);
        QVERIFY(css.open(QIODevice::ReadOnly));QVERIFY(css.readAll().contains("window.nemo-desktop-window"));css.close();
        const auto backups=QDir(f.paths.themeRoot).entryList({"Omarchy-catppuccin.backup-*"},QDir::Dirs);
        QCOMPARE(backups.size(),1);
        QFile old(f.paths.themeRoot+"/"+backups.first()+"/gtk-3.0/gtk.css");QVERIFY(old.open(QIODevice::ReadOnly));QVERIFY(old.readAll().contains("old desktop-covering"));
    }
    void interruptedSwitchOffersRecovery() {
        Fixture f;Backend b(nullptr,f.paths,&f.settings);const auto before=b.snapshot();
        QDir().mkpath(f.paths.stateRoot);QFile file(f.paths.stateRoot+"/pending.json");file.open(QIODevice::WriteOnly);
        file.write(QJsonDocument(QJsonObject{{"version",1},{"snapshot",before}}).toJson());file.close();
        f.settings.values["org.cinnamon.theme/name"]="'Omarchy-nord'";
        Backend recovered(nullptr,f.paths,&f.settings);QVERIFY(recovered.canUndo());QVERIFY(recovered.undoNow());
        QCOMPARE(f.settings.read("org.cinnamon.theme","name"),QString("'cinnamon'"));
    }
    void realQmlInteraction() {
        Fixture f;auto second=f.paths.displayOverrides.first().toMap();second["id"]="Test-2";second["x"]=320;f.paths.displayOverrides<<second;
        writeFile(f.dir.filePath("catalog.html"),"<a href=\"https://github.com/a/omarchy-dusk-theme\"><img src=\"dusk.png\"/><span>Dusk</span></a>");
        {QImage image(40,24,QImage::Format_RGB32);image.fill(Qt::magenta);QVERIFY(image.save(f.dir.filePath("dusk.png")));}
        f.paths.communityCatalog=QUrl::fromLocalFile(f.dir.filePath("catalog.html")).toString();
        FakePicker picker;Backend b(nullptr,f.paths,&f.settings,&picker);const auto original=f.settings.values;
        Theme theme(f.dir.filePath("current"));Translator translator(f.dir.filePath("language"));
        QQmlApplicationEngine engine;
        QStringList warnings;connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto&e:errors)warnings<<e.toString();});
        engine.rootContext()->setContextProperty("backend",&b);engine.rootContext()->setContextProperty("theme",&theme);engine.rootContext()->setContextProperty("translator",&translator);
        engine.load(QUrl("qrc:/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);QTest::qWait(200);
        auto item=[&](const char*name){return window->findChild<QQuickItem*>(name);};
        const auto previewPoint=item("desktopPreview")->mapToScene(QPointF(10,60)).toPoint();
        QTRY_COMPARE_WITH_TIMEOUT(window->grabWindow().pixelColor(previewPoint),QColor("#335577"),3000);
        auto click=[&](QQuickItem*control,QPointF pos=QPointF(-1,-1)) {
            QVERIFY(control);if(pos.x()<0)pos=QPointF(control->width()/2,control->height()/2);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,control->mapToScene(pos).toPoint());QTest::qWait(50);
        };
        QVERIFY(!item("themeSearch"));QVERIFY(item("reportButton"));
        const QUrl report(b.reportUrl());QCOMPARE(report.host(),QString("github.com"));QCOMPARE(report.path(),QString("/CyberJody/cinnarchy/issues/new"));
        QCOMPARE(QUrlQuery(report).queryItemValue("template"),QString("bug_report.yml"));QVERIFY(QUrlQuery(report).queryItemValue("system",QUrl::FullyDecoded).contains("Qt "));
        QVERIFY(!report.toString().contains(QDir::homePath()));QVERIFY(!report.toString().contains(qEnvironmentVariable("USER","\x01")));QTRY_COMPARE(item("themeList")->property("count").toInt(),23);
        QQuickItem *nord=nullptr;
        QTRY_VERIFY(([&]{QMetaObject::invokeMethod(item("themeList"),"positionViewAtIndex",Q_ARG(int,14),Q_ARG(int,4));
            for(auto *child:item("themeList")->property("contentItem").value<QQuickItem*>()->childItems())
                if(child->property("modelData").toMap().value("slug")=="nord"&&child->isVisible())nord=child;
            return nord!=nullptr;}()));
        click(nord);QCOMPARE(b.selectedSlug(),QString("nord"));
        click(item("displayAssignments"),QPointF(item("displayAssignments")->width()*.75,30));QCOMPARE(b.targetDisplay(),QString("Test-2"));
        const auto untouched=b.displayAssignments().first().toMap().value("url");
        click(item("wallpaperList"),QPointF(150,25));QCOMPARE(b.wallpaperIndex(),1);
        QCOMPARE(b.displayAssignments().first().toMap().value("url"),untouched);
        click(item("addMediaButton"));QCOMPARE(picker.calls,1);
        click(item("applyButton"));QTRY_VERIFY(b.canUndo());QTRY_VERIFY(!b.busy());
        QCOMPARE(unquote(f.settings.read("org.cinnamon.theme","name")),QString("Omarchy-nord"));
        click(item("undoButton"));QTRY_VERIFY(!b.busy());QCOMPARE(f.settings.values,original);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier);QTRY_VERIFY(!b.canRedo());
        QCOMPARE(unquote(f.settings.read("org.cinnamon.theme","name")),QString("Omarchy-nord"));
        // Typing in the Get more themes dialog must not reach the desktop shortcuts.
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QObject*>("getThemesDialog"),"open"));QTRY_VERIFY(!window->property("shortcutsActive").toBool());
        QTRY_COMPARE(item("communityGrid")->property("count").toInt(),1);QTRY_VERIFY(!b.fetching());
        QTest::keyClick(window,Qt::Key_Q);QTest::keyClick(window,Qt::Key_Space);QTest::qWait(100);
        QVERIFY(window->isVisible());QVERIFY(!b.busy());QCOMPARE(unquote(f.settings.read("org.cinnamon.theme","name")),QString("Omarchy-nord"));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close();
    }
};

int main(int argc,char**argv){QGuiApplication app(argc,argv);QQuickStyle::setStyle("Material");PickerTests tests;return QTest::qExec(&tests,argc,argv);}
#include "cinnarchy_tests.moc"
