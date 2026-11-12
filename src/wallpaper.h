#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include <QImage>
#include <QRect>

class SettingsStore;

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
    static QString configurationPath(const QString &stateRoot);
    static QString socketPath(const QString &stateRoot);
};

int runWallpaperService(const QString &stateRoot);
