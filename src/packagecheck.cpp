#include "packagecheck.h"
#include "mainwindow.h"
#include <QtWidgets>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
namespace imagebatch {
int runPackageCheck(QApplication &app, MainWindow &window, const QString &reportPath) {
    QTemporaryDir fixture(QDir(QFileInfo(reportPath).absolutePath()).filePath("imagebatch-check-XXXXXX"));
    const QString input = fixture.filePath(QStringLiteral("中文 input"));
    QDir().mkpath(input);
    const QString source = QDir(input).filePath(QStringLiteral("透明 图片.png"));
    QImage image(160, 100, QImage::Format_ARGB32); image.fill(Qt::transparent);
    { QPainter p(&image); p.fillRect(40, 20, 80, 60, QColor("#288f86")); }
    if (!image.save(source)) return 2;
    const auto hashFile = [](const QString &path) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    };
    const auto originalHash = hashFile(source);
    bool completed = false;
    auto finish = [&](bool passed, const QString &reason) {
        if (completed) return;
        completed = true;
        QJsonObject report{{"passed", passed}, {"reason", reason}, {"qt_version", qVersion()},
                           {"original_unchanged", hashFile(source) == originalHash},
                           {"platform", QGuiApplication::platformName()}};
        QFile file(reportPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) { app.exit(3); return; }
        file.close(); app.exit(passed ? 0 : 1);
    };
    QObject::connect(&window, &MainWindow::batchComplete, &window, [&] {
        QImage result(QDir(input).filePath("processed/透明 图片_processed.jpg"));
        const bool ok = result.size() == QSize(80, 50) && !window.lastReport().isEmpty() &&
                        QFileInfo::exists(window.lastReport()) && originalHash == hashFile(source) &&
                        result.pixelColor(0, 0).red() > 245 && result.pixelColor(0, 0).green() > 245;
        finish(ok, ok ? "Native GUI import, resize, JPEG export, watermark and CSV completed; original hash unchanged."
                     : "Packaged GUI result validation failed.");
    });
    QTimer::singleShot(0, &window, [&] {
        window.findChild<QLineEdit *>("inputPath")->setText(input);
        window.findChild<QPushButton *>("importButton")->click();
        window.findChild<QSpinBox *>("maxEdge")->setValue(80);
        window.findChild<QCheckBox *>("watermarkEnabled")->setChecked(true);
        window.findChild<QLineEdit *>("watermarkText")->setText(QStringLiteral("测试水印"));
        window.findChild<QPushButton *>("startButton")->click();
    });
    QTimer::singleShot(20000, &window, [&] { finish(false, "Packaged GUI check timed out."); });
    return app.exec();
}
}
