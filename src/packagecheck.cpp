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
    if (!fixture.isValid()) return 2;
    // Preview has its own Qt Test coverage. Keep its asynchronous reader away
    // from the temporary corpus when this diagnostic event loop exits.
    window.setProperty("skipPreview", true);
    const QString input = fixture.filePath(QStringLiteral("中文 input"));
    QDir().mkpath(input);
    const QString source = QDir(input).filePath(QStringLiteral("透明 图片.png"));
    QImage image(160, 100, QImage::Format_ARGB32); image.fill(Qt::transparent);
    { QPainter p(&image); p.fillRect(40, 20, 80, 60, QColor("#288f86")); }
    if (!image.save(source)) return 2;
    const QString second = QDir(input).filePath(QStringLiteral("第二张.png"));
    const QString third = QDir(input).filePath(QStringLiteral("竖图.bmp"));
    if (!image.save(second) || !image.transformed(QTransform().rotate(90)).save(third)) return 2;
    QFile corrupt(QDir(input).filePath(QStringLiteral("损坏.jpg")));
    if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("not an image") < 0) return 2;
    corrupt.close();
    const auto hashFile = [](const QString &path) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    };
    const auto originalHash = hashFile(source);
    const auto secondHash = hashFile(second), thirdHash = hashFile(third), corruptHash = hashFile(corrupt.fileName());
    bool outputsValid = false, metricsValid = false, retryValid = false, subsetValid = false, grayValid = false, compressedValid = false;
    int phase = 0;
    bool completed = false;
    auto finish = [&](bool passed, const QString &reason) {
        if (completed) return;
        completed = true;
        QJsonObject report{{"passed", passed}, {"reason", reason}, {"qt_version", qVersion()},
                           {"original_unchanged", hashFile(source) == originalHash},
                           {"all_originals_unchanged", hashFile(source) == originalHash && hashFile(second) == secondHash && hashFile(third) == thirdHash && hashFile(corrupt.fileName()) == corruptHash},
                           {"parallel_batch_outputs_valid", outputsValid}, {"batch_metrics_valid", metricsValid},
                           {"retry_without_duplicates_valid", retryValid}, {"subset_original_format_valid", subsetValid},
                           {"grayscale_png_valid", grayValid}, {"jpeg_target_size_valid", compressedValid},
                           {"version", IMAGEBATCH_VERSION},
                           {"platform", QGuiApplication::platformName()}};
        QFile file(reportPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) { app.exit(3); return; }
        file.close(); app.exit(passed ? 0 : 1);
    };
    QObject::connect(&window, &MainWindow::batchComplete, &window, [&] {
        const QString output = QDir(input).filePath("processed");
        if (phase == 4) {
            const auto path = QDir(output).filePath(QStringLiteral("透明 图片_processed_1.jpg"));
            QImage compressed(path);
            compressedValid = compressed.size() == QSize(80, 50) && QFileInfo(path).size() > 0 && QFileInfo(path).size() <= 1024;
            const bool unchanged = hashFile(source) == originalHash && hashFile(second) == secondHash &&
                hashFile(third) == thirdHash && hashFile(corrupt.fileName()) == corruptHash;
            finish(outputsValid && metricsValid && retryValid && subsetValid && grayValid && compressedValid && unchanged,
                "Native GUI batch, retry, selected original format, 8-bit single-channel grayscale PNG, JPEG size limit and unchanged originals verified.");
            return;
        }
        if (phase == 3) {
            const auto path = QDir(output).filePath(QStringLiteral("透明 图片_processed_1.png"));
            QFile file(path); QByteArray data; if (file.open(QIODevice::ReadOnly)) data = file.readAll();
            QImage gray(path);
            grayValid = data.size() > 25 && quint8(data[24]) == 8 && quint8(data[25]) == 0 &&
                gray.size() == QSize(160, 100) && gray.pixelColor(0, 0) == QColor(Qt::white);
            if (!grayValid) { finish(false, "Grayscale PNG channel or background validation failed."); return; }
            window.findChild<QPushButton *>("photoPresetButton")->click();
            window.findChild<QSpinBox *>("maxEdge")->setValue(80);
            window.findChild<QSpinBox *>("jpegTargetSize")->setValue(1);
            phase = 4; window.findChild<QPushButton *>("startButton")->click(); return;
        }
        if (phase == 1) {
            QFile metrics(window.lastReport().left(window.lastReport().size() - 4) + ".json");
            if (metrics.open(QIODevice::ReadOnly)) {
                const auto json = QJsonDocument::fromJson(metrics.readAll()).object();
                retryValid = json.value("success").toInt() == 0 && json.value("failed").toInt() == 1 &&
                    QDir(output).entryList({"*.jpg"}, QDir::Files).size() == 3;
            }
            if (!retryValid) { finish(false, "Retry duplicated successes or did not isolate the corrupt image."); return; }
            auto *table = window.findChild<QTableWidget *>("fileTable");
            for (int row = 0; row < table->rowCount(); ++row)
                table->item(row, 0)->setCheckState(table->item(row, 0)->data(Qt::UserRole).toString() == source ? Qt::Checked : Qt::Unchecked);
            window.findChild<QComboBox *>("outputFormat")->setCurrentIndex(3);
            phase = 2; window.findChild<QPushButton *>("startButton")->click(); return;
        }
        if (phase == 2) {
            QImage png(QDir(output).filePath(QStringLiteral("透明 图片_processed.png")));
            subsetValid = png.size() == QSize(80, 50) && png.pixelColor(0, 0).alpha() == 0 &&
                QDir(output).entryList({"*.png"}, QDir::Files).size() == 1 &&
                QDir(output).entryList({"*.jpg"}, QDir::Files).size() == 3;
            if (!subsetValid) { finish(false, "Selected original-format output validation failed."); return; }
            window.findChild<QPushButton *>("trainingPresetButton")->click();
            window.findChild<QComboBox *>("pngCompression")->setCurrentIndex(2);
            phase = 3; window.findChild<QPushButton *>("startButton")->click(); return;
        }
        QImage result(QDir(input).filePath("processed/透明 图片_processed.jpg"));
        QImage result2(QDir(input).filePath("processed/第二张_processed.jpg"));
        QImage result3(QDir(input).filePath("processed/竖图_processed.jpg"));
        outputsValid = result.size() == QSize(80, 50) && result2.size() == QSize(80, 50) && result3.size() == QSize(50, 80);
        QFile metrics(window.lastReport().left(window.lastReport().size() - 4) + ".json");
        if (metrics.open(QIODevice::ReadOnly)) {
            const auto json = QJsonDocument::fromJson(metrics.readAll()).object();
            metricsValid = json.value("workers").toInt() == 2 && json.value("success").toInt() == 3 &&
                json.value("failed").toInt() == 1 && json.value("peak_queued").toInt() <= 4 &&
                json.value("peak_reserved_bytes").toDouble() <= 64.0 * 1024 * 1024;
        }
        const bool ok = outputsValid && metricsValid && !window.lastReport().isEmpty() &&
                        QFileInfo::exists(window.lastReport()) && originalHash == hashFile(source) &&
                        hashFile(second) == secondHash && hashFile(third) == thirdHash &&
                        result.pixelColor(0, 0).red() > 245 && result.pixelColor(0, 0).green() > 245;
        if (!ok) { finish(false, "Packaged GUI result validation failed."); return; }
        phase = 1; window.findChild<QPushButton *>("retryButton")->click();
    });
    QTimer::singleShot(0, &window, [&] {
        window.findChild<QLineEdit *>("inputPath")->setText(input);
        window.findChild<QPushButton *>("importButton")->click();
        window.findChild<QSpinBox *>("maxEdge")->setValue(80);
        window.findChild<QSpinBox *>("workerCount")->setValue(2);
        window.findChild<QSpinBox *>("memoryBudget")->setValue(64);
        window.findChild<QCheckBox *>("watermarkEnabled")->setChecked(true);
        window.findChild<QLineEdit *>("watermarkText")->setText(QStringLiteral("测试水印"));
        window.findChild<QPushButton *>("startButton")->click();
    });
    QTimer::singleShot(20000, &window, [&] { finish(false, "Packaged GUI check timed out."); });
    return app.exec();
}
}
