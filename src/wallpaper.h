#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include <QImage>
#include <QRect>
#include <QElapsedTimer>

class SettingsStore;

// How one animated background plays: saved beside its url and fit.
struct Playback {
    double speed=1.0; // 0.25 to 4; 1 is the video's own speed
    int replay=0;     // seconds resting on the last frame before the next loop; 0 loops at once, -1 plays once
    static Playback from(const QJsonObject &choice);
    static bool validSpeed(double speed) {return speed>=0.25&&speed<=4.0;}
    static bool validReplay(int seconds) {return seconds>=-1&&seconds<=86400;}
    QStringList arguments() const; // mpv options for a player about to start
    bool operator==(const Playback &other) const {return speed==other.speed&&replay==other.replay;}
};

// Drives one mpv player's speed and looping through its IPC socket. mpv has no
// pause-between-loops option: the player holds its last frame at the end and
// advance(), called regularly, starts it again once the rest is over.
class PlaybackLoop {
public:
    void configure(const QString &socket,const Playback &playback,bool running);
    void advance(bool paused);
    bool resting() const {return m_resting;}
private:
    void restart();
    QString m_socket;Playback m_playback;bool m_resting=false;QElapsedTimer m_ended;
};

class Wallpapers {
public:
    static QVariantList displays();
    static bool isVideo(const QString &path);
    static bool isAnimated(const QString &path);
    static bool supported(const QString &path);
    static QVariantMap media(const QString &path,const QString &stateRoot);
    static QJsonObject read(const QString &path);
    static bool save(const QString &path,const QJsonObject &data,QString &error);
    static QString poster(const QString &path,const QString &stateRoot,QString &error);
    static QImage compose(const QVariantList &screens,const QJsonObject &assignments,
                          const QString &stateRoot,QString &error);
    static QJsonObject prepare(const QVariantList &screens,const QJsonObject &assignments,
                               const QString &stateRoot,QString &error);
    static bool activate(const QJsonObject &config,const QString &stateRoot,bool runtime,QString &error);
    static QJsonObject request(const QString &stateRoot,const QJsonObject &message,int timeout=6000);
    // One command to an mpv player's IPC socket; empty when it does not answer.
    static QJsonObject player(const QString &socket,const QJsonArray &command,int timeout=500);
    static QString configurationPath(const QString &stateRoot);
    static QString socketPath(const QString &stateRoot);
    // Nemo will not draw desktop icons while another program has a desktop window,
    // unless its WM_CLASS is in ignored-desktop-handlers. Lists the player's, or takes it out.
    static void listDesktopHandler(SettingsStore &settings,bool listed);
};

int runWallpaperService(const QString &stateRoot);
