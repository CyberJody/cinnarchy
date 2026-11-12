#pragma once
#include <QObject>
#include <QVariantMap>

class FilePicker:public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void choose(const QString &parentHandle)=0;
signals:
    void chosen(const QStringList &urls);
    void failed(const QString &message);
};

class PortalFilePicker final:public FilePicker {
    Q_OBJECT
public:
    using FilePicker::FilePicker;
    ~PortalFilePicker() override;
    void choose(const QString &parentHandle) override;
    void cancel();
private slots:
    void response(uint result,const QVariantMap &values);
private:
    QString requestPath;
};
