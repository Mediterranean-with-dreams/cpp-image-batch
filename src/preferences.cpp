#include "preferences.h"
#include <QSettings>
#include <QDir>
#include <QFileInfo>
namespace imagebatch {
Preferences readPreferences(const QString &file) {
    Preferences p;
    QSettings settings(file, QSettings::IniFormat);
    auto bounded = [&](const char *key, int fallback, int minimum, int maximum) {
        bool ok = false;
        int value = settings.value(key, fallback).toInt(&ok);
        return ok && value >= minimum && value <= maximum ? value : fallback;
    };
    p.options.maxEdge = bounded("image/maxEdge", 1920, 0, 20000);
    p.options.jpegQuality = bounded("image/quality", 90, 1, 100);
    p.options.format = settings.value("image/format", "jpg").toString();
    if (!QStringList{"jpg", "png", "bmp", "original"}.contains(p.options.format)) p.options.format = "jpg";
    p.options.watermark = settings.value("image/watermark").toString().left(100);
    p.options.watermarkPosition = static_cast<WatermarkPosition>(bounded("image/position", 0, 0, 3));
    p.watermarkEnabled = settings.value("image/watermarkEnabled", false).toBool();
    p.batch.workers = bounded("batch/workers", 2, 1, 8);
    p.batch.queueCapacity = p.batch.workers * 2;
    p.batch.memoryBudgetBytes = quint64(bounded("batch/budgetMiB", 512, 64, 4096)) * 1024 * 1024;
    p.lastFolder = settings.value("folders/lastInput").toString();
    return p;
}
bool writePreferences(const QString &file, const Preferences &p, QString *error) {
    if (error) error->clear();
    if (!QDir().mkpath(QFileInfo(file).absolutePath())) {
        if (error) *error = QStringLiteral("无法创建设置目录");
        return false;
    }
    QSettings settings(file, QSettings::IniFormat);
    settings.setValue("image/maxEdge", p.options.maxEdge);
    settings.setValue("image/quality", p.options.jpegQuality);
    settings.setValue("image/format", p.options.format);
    settings.setValue("image/watermark", p.options.watermark);
    settings.setValue("image/position", static_cast<int>(p.options.watermarkPosition));
    settings.setValue("image/watermarkEnabled", p.watermarkEnabled);
    settings.setValue("batch/workers", p.batch.workers);
    settings.setValue("batch/budgetMiB", static_cast<int>(p.batch.memoryBudgetBytes / 1024 / 1024));
    settings.setValue("folders/lastInput", p.lastFolder);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("无法保存设置，请检查目录权限");
        return false;
    }
    return true;
}
}
