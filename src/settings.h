#pragma once
#include <QString>
#include <memory>

// Values retain their GVariant representation so snapshots preserve types.
class SettingsStore {
public:
    virtual ~SettingsStore() = default;
    virtual bool has(const QString &schema, const QString &key) const = 0;
    virtual bool writable(const QString &schema, const QString &key) const = 0;
    virtual QString read(const QString &schema, const QString &key) const = 0;
    virtual bool write(const QString &schema, const QString &key, const QString &value, QString &error) = 0;
    virtual void sync() = 0;
};
std::unique_ptr<SettingsStore> makeSettingsStore();
QString variantString(const QString &text);
QString unquote(const QString &text);
