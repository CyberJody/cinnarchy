#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QTimer>
#include <functional>
#include "settings.h"
#include "wallpaper.h"
#include "filepicker.h"
#include <QPointer>
#include <QWindow>
#include <QThread>
#include "fetcher.h"

inline const QString projectUrl = "https://github.com/CyberJody/cinnarchy";

struct PickerPaths {
    QString themeRoot, stateRoot;
    QStringList stockRoots, extraBackgroundRoots, iconRoots;
    QVariantList displayOverrides;
    bool wallpaperRuntime=true;
    QString userThemeRoot; // where fetched Omarchy themes go; empty: last stock root
    QString communityCatalog=ThemeFetch::communityCatalog;
    QString officialRepo=ThemeFetch::officialRepo;
    static PickerPaths defaults();
    ThemeFetch::Paths fetchPaths() const;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList themes READ themes NOTIFY catalogChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSlug READ selectedSlug NOTIFY selectionChanged)
    Q_PROPERTY(int wallpaperIndex READ wallpaperIndex NOTIFY selectionChanged)
    Q_PROPERTY(QString wallpaperUrl READ wallpaperUrl NOTIFY selectionChanged)
    Q_PROPERTY(QString wallpaperName READ wallpaperName NOTIFY selectionChanged)
    Q_PROPERTY(QString activeName READ activeName NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(bool error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
    Q_PROPERTY(bool hasOriginal READ hasOriginal NOTIFY stateChanged)
    Q_PROPERTY(bool useWallpaper READ useWallpaper WRITE setUseWallpaper NOTIFY optionsChanged)
    Q_PROPERTY(bool useIcons READ useIcons WRITE setUseIcons NOTIFY optionsChanged)
    Q_PROPERTY(QString pictureOption READ pictureOption WRITE setPictureOption NOTIFY optionsChanged)
    Q_PROPERTY(QVariantList displays READ displays NOTIFY displaysChanged)
    Q_PROPERTY(QString targetDisplay READ targetDisplay WRITE setTargetDisplay NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap wallpaperMedia READ wallpaperMedia NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList displayAssignments READ displayAssignments NOTIFY selectionChanged)
    Q_PROPERTY(bool animationsPaused READ animationsPaused NOTIFY stateChanged)
    Q_PROPERTY(bool hasAnimations READ hasAnimations NOTIFY stateChanged)
    Q_PROPERTY(bool fetching READ fetching NOTIFY fetchChanged)
    Q_PROPERTY(QVariantList communityThemes READ communityThemes NOTIFY fetchChanged)
    Q_PROPERTY(int themesWithoutBackgrounds READ themesWithoutBackgrounds NOTIFY catalogChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
public:
    explicit Backend(QObject *parent=nullptr, PickerPaths paths=PickerPaths::defaults(), SettingsStore *store=nullptr,FilePicker *picker=nullptr);
    ~Backend() override;
    ThemeFetch::Paths fetchPaths() const { return m_paths.fetchPaths(); }
    bool fetching() const { return m_worker!=nullptr; }
    QVariantList communityThemes() const;
    // Downloads run on a worker thread; the catalog reloads when they finish.
    Q_INVOKABLE void fetchOfficial();
    Q_INVOKABLE void installTheme(const QString &url);
    Q_INVOKABLE void loadCommunity();
    // Omarchy's own backgrounds for the selected built-in theme, or for every one lacking them.
    Q_INVOKABLE void downloadBackgrounds(bool everyTheme);
    int themesWithoutBackgrounds() const;
    QString version() const;
    // A new GitHub issue with version and system details filled in; nothing personal.
    Q_INVOKABLE QString reportUrl() const;
    void finishFetch(const ThemeFetch::Result &result);
    QVariantList themes() const { return m_themes; }
    QVariantMap selected() const;
    QString selectedSlug() const { return m_slug; }
    int wallpaperIndex() const { return m_wallpaperIndex; }
    QString wallpaperUrl() const;
    QString wallpaperName() const;
    QString activeName() const;
    QString message() const { return m_message; }
    bool error() const { return m_error; }
    bool busy() const { return m_busy; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool hasOriginal() const { return !m_original.isEmpty(); }
    bool useWallpaper() const { return m_useWallpaper; }
    bool useIcons() const { return m_useIcons; }
    QString pictureOption() const { return m_pictureOption; }
    QVariantList displays() const {return m_displays;}
    QString targetDisplay() const {return m_target;}
    void setTargetDisplay(const QString &id);
    QVariantMap wallpaperMedia() const;
    QVariantList displayAssignments() const;
    bool animationsPaused() const;
    bool hasAnimations() const;
    void setWindow(QWindow *window){m_window=window;}
    Q_INVOKABLE void chooseMedia();
    Q_INVOKABLE void addMedia(const QStringList &urls);
    Q_INVOKABLE void pauseAnimations();
    Q_INVOKABLE void useSystemWallpaper();
    void setUseWallpaper(bool v) { m_useWallpaper=v; emit optionsChanged(); }
    void setUseIcons(bool v) { m_useIcons=v; emit optionsChanged(); }
    void setPictureOption(const QString &v);
    Q_INVOKABLE void selectTheme(const QString &slug);
    Q_INVOKABLE void selectWallpaper(int index);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void restoreOriginal();
    Q_INVOKABLE bool applyNow();
    Q_INVOKABLE bool undoNow();
    Q_INVOKABLE bool redoNow();
    Q_INVOKABLE bool restoreNow();
    QJsonArray snapshot() const;
    QJsonObject status() const;
signals:
    void catalogChanged();
    void selectionChanged();
    void optionsChanged();
    void stateChanged();
    void displaysChanged();
    void fetchChanged();
    void fetchFinished();
private:
    void runFetch(std::function<void()> work);
    QThread *m_worker=nullptr;
    QJsonArray m_community;
    QVariantList wallpapers(const QString &slug) const;
    QString iconFor(const QString &slug, const QString &mode, const QString &bundledChoice, QString &note) const;
    bool ensureInstalled(const QVariantMap &theme);
    QJsonArray desiredSettings();
    bool commit(const QJsonArray &changes);
    bool writeSnapshot(const QJsonArray &snapshot, QString &error);
    bool saveState();
    bool saveJson(const QString &path, const QJsonObject &value);
    void loadState();
    void fail(const QString &message);
    void schedule(const std::function<bool()> &action);
    PickerPaths m_paths;
    std::unique_ptr<SettingsStore> m_ownedStore;
    SettingsStore *m_store;
    QVariantList m_themes;
    QString m_slug, m_message, m_pictureOption="zoom";
    int m_wallpaperIndex=0;
    bool m_error=false, m_busy=false, m_useWallpaper=true, m_useIcons=true;
    bool m_themeFilesChanged=false;
    QJsonArray m_original, m_undo, m_redo;
    QJsonObject m_choices;
    QTimer m_poll;
    QString m_lastActive;
    QVariantList m_displays;
    QString m_target="all",m_selectedUrl;
    QJsonObject m_assignments,m_prepared,m_added;
    FilePicker *m_picker=nullptr;
    QPointer<QWindow> m_window;
    void chooseUrl(const QString &url);
    void syncSelection();
    QJsonObject currentConfiguration() const;
};
