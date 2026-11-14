// Include GLib before Qt, whose signals macro otherwise collides with GIO.
#include <gio/gio.h>
#include "settings.h"
#include <QMap>

class GioSettings final : public SettingsStore {
    mutable QMap<QString, GSettings *> objects;
    GSettings *object(const QString &id) const {
        if (objects.contains(id)) return objects.value(id);
        auto *source = g_settings_schema_source_get_default();
        auto *schema = source ? g_settings_schema_source_lookup(source, id.toUtf8().constData(), TRUE) : nullptr;
        if (!schema) return nullptr;
        auto *settings = g_settings_new_full(schema, nullptr, nullptr);
        g_settings_schema_unref(schema);
        objects.insert(id, settings);
        return settings;
    }
public:
    ~GioSettings() override { for (auto *settings : objects) g_object_unref(settings); }
    bool has(const QString &schema, const QString &key) const override {
        auto *source = g_settings_schema_source_get_default();
        auto *s = source ? g_settings_schema_source_lookup(source, schema.toUtf8().constData(), TRUE) : nullptr;
        if (!s) return false;
        const bool found = g_settings_schema_has_key(s, key.toUtf8().constData());
        g_settings_schema_unref(s);
        return found;
    }
    bool writable(const QString &s, const QString &k) const override {
        return has(s,k) && g_settings_is_writable(object(s), k.toUtf8().constData());
    }
    QString read(const QString &s, const QString &k) const override {
        if (!has(s,k)) return {};
        auto *value = g_settings_get_value(object(s), k.toUtf8().constData());
        char *printed = g_variant_print(value, TRUE);
        const QString result = QString::fromUtf8(printed);
        g_free(printed); g_variant_unref(value);
        return result;
    }
    QString defaultValue(const QString &s, const QString &k) const override {
        if (!has(s,k)) return {};
        auto *value = g_settings_get_default_value(object(s), k.toUtf8().constData());
        if (!value) return {};
        char *printed = g_variant_print(value, TRUE);
        const QString result = QString::fromUtf8(printed);
        g_free(printed); g_variant_unref(value);
        return result;
    }
    bool write(const QString &s, const QString &k, const QString &text, QString &error) override {
        if (!writable(s,k)) { error = QString("Setting is unavailable or locked: %1 / %2").arg(s,k); return false; }
        GError *parseError = nullptr;
        auto *current = g_settings_get_value(object(s), k.toUtf8().constData());
        auto *value = g_variant_parse(g_variant_get_type(current), text.toUtf8().constData(), nullptr, nullptr, &parseError);
        g_variant_unref(current);
        if (!value) {
            error = QString::fromUtf8(parseError->message); g_error_free(parseError); return false;
        }
        g_variant_take_ref(value);
        auto *schema = g_settings_schema_source_lookup(g_settings_schema_source_get_default(), s.toUtf8().constData(), TRUE);
        auto *schemaKey = g_settings_schema_get_key(schema,k.toUtf8().constData());
        const bool valid = g_settings_schema_key_range_check(schemaKey,value);
        g_settings_schema_key_unref(schemaKey);g_settings_schema_unref(schema);
        const bool success = valid && g_settings_set_value(object(s), k.toUtf8().constData(), value);
        g_variant_unref(value);
        if (!success) error = QString("Could not change %1 / %2").arg(s,k);
        return success;
    }
    void sync() override { g_settings_sync(); }
};

std::unique_ptr<SettingsStore> makeSettingsStore() { return std::make_unique<GioSettings>(); }
QString variantString(const QString &text) {
    auto *v = g_variant_new_string(text.toUtf8().constData());
    g_variant_ref_sink(v);
    char *printed = g_variant_print(v, TRUE);
    const QString result = QString::fromUtf8(printed);
    g_free(printed); g_variant_unref(v);
    return result;
}
QString unquote(const QString &text) {
    GError *error = nullptr;
    auto *v = g_variant_parse(G_VARIANT_TYPE_STRING,text.toUtf8().constData(),nullptr,nullptr,&error);
    if (!v) { if(error) g_error_free(error); return text; }
    const QString result = QString::fromUtf8(g_variant_get_string(v,nullptr));
    g_variant_unref(v); return result;
}
