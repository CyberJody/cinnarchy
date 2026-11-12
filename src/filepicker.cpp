#include "filepicker.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QUuid>

struct Pattern{uint type;QString value;};
using Patterns=QList<Pattern>;
struct Filter{QString label;Patterns patterns;};
using Filters=QList<Filter>;
Q_DECLARE_METATYPE(Pattern)
Q_DECLARE_METATYPE(Patterns)
Q_DECLARE_METATYPE(Filter)
Q_DECLARE_METATYPE(Filters)
QDBusArgument &operator<<(QDBusArgument &a,const Pattern &p){a.beginStructure();a<<p.type<<p.value;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,Pattern &p){a.beginStructure();a>>p.type>>p.value;a.endStructure();return a;}
QDBusArgument &operator<<(QDBusArgument &a,const Filter &f){a.beginStructure();a<<f.label<<f.patterns;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,Filter &f){a.beginStructure();a>>f.label>>f.patterns;a.endStructure();return a;}

void PortalFilePicker::choose(const QString &parentHandle) {
    if(!requestPath.isEmpty())return;
    qDBusRegisterMetaType<Pattern>();qDBusRegisterMetaType<Patterns>();qDBusRegisterMetaType<Filter>();qDBusRegisterMetaType<Filters>();
    auto bus=QDBusConnection::sessionBus();QString sender=bus.baseService();sender.remove(':');sender.replace('.','_');
    const QString token="cinnarchy_"+QUuid::createUuid().toString(QUuid::Id128);
    const QString request="/org/freedesktop/portal/desktop/request/"+sender+"/"+token;
    if(!bus.connect("org.freedesktop.portal.Desktop",request,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)))) {
        emit failed(tr("Cannot connect to the desktop file chooser."));return;
    }
    requestPath=request;
    Filters filters{{tr("Images and videos"),{{1,"image/*"},{1,"video/*"}}},{tr("All files"),{{0,"*"}}}};
    QVariantMap options{{"handle_token",token},{"multiple",true},{"modal",true},{"filters",QVariant::fromValue(filters)},{"accept_label",tr("Add backgrounds")}};
    QDBusInterface portal("org.freedesktop.portal.Desktop","/org/freedesktop/portal/desktop","org.freedesktop.portal.FileChooser",bus);
    auto *watcher=new QDBusPendingCallWatcher(portal.asyncCall("OpenFile",parentHandle,tr("Choose background images or videos"),options),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher]{QDBusPendingReply<QDBusObjectPath> result=*watcher;if(result.isError()){cancel();emit failed(tr("Desktop file chooser: %1").arg(result.error().message()));}watcher->deleteLater();});
}
PortalFilePicker::~PortalFilePicker(){cancel();}
void PortalFilePicker::cancel() {
    if(requestPath.isEmpty())return;
    const auto path=requestPath;requestPath.clear();
    auto bus=QDBusConnection::sessionBus();
    bus.disconnect("org.freedesktop.portal.Desktop",path,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)));
    QDBusInterface request("org.freedesktop.portal.Desktop",path,"org.freedesktop.portal.Request",bus);request.asyncCall("Close");
}
void PortalFilePicker::response(uint result,const QVariantMap &values) {
    auto bus=QDBusConnection::sessionBus();bus.disconnect("org.freedesktop.portal.Desktop",requestPath,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)));requestPath.clear();
    if(result!=0)return;
    const auto urls=qdbus_cast<QStringList>(values.value("uris"));if(!urls.isEmpty())emit chosen(urls);
}
