#include "mainwindow.h"
#include "preferences.h"
#include <QtTest>
#include <QtWidgets>
#include <QCryptographicHash>
#include <QImageReader>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QThreadPool>

using namespace imagebatch;
static QByteArray digest(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}
static QImage scene(int width = 2400, int height = 1600) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor("#d7edf6"));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(width / 1200.0, height / 800.0);
    QLinearGradient sky(0, 0, 0, 800);
    sky.setColorAt(0, QColor("#a3d5ea")); sky.setColorAt(1, QColor("#f0f5de"));
    p.fillRect(QRect(0, 0, 1200, 800), sky);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#fff1ac")); p.drawEllipse(QPoint(920, 170), 78, 78);
    p.setBrush(QColor("#7da8b5"));
    p.drawPolygon(QPolygon{{0, 620}, {300, 180}, {610, 620}});
    p.drawPolygon(QPolygon{{280, 650}, {700, 245}, {1150, 650}});
    p.setBrush(QColor("#e9f1ed"));
    p.drawPolygon(QPolygon{{237, 273}, {300, 180}, {365, 274}, {319, 252}, {287, 282}});
    p.setBrush(QColor("#286c6e"));
    p.drawPolygon(QPolygon{{0, 570}, {350, 440}, {800, 760}, {0, 800}});
    p.setBrush(QColor("#3a8980"));
    p.drawPolygon(QPolygon{{1200, 460}, {700, 590}, {470, 800}, {1200, 800}});
    p.setPen(QColor("#ffffff")); QFont f; f.setPixelSize(30); f.setBold(true); p.setFont(f);
    p.drawText(QRect(50, 665, 800, 70), Qt::AlignLeft | Qt::AlignVCenter, "A SMALL JOURNEY");
    f.setPixelSize(17); f.setBold(false); p.setFont(f);
    p.drawText(QRect(50, 730, 800, 30), "ImageBatch / original procedural sample");
    return image;
}
class ImageBatchTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qputenv("IMAGEBATCH_NO_SETTINGS", "1"); // Never read or write a user's app settings during tests.
        QImageReader::setAllocationLimit(256); QThreadPool::globalInstance()->setMaxThreadCount(1);
    }
    void defaultsAndValidation() {
        Options o;
        QCOMPARE(o.maxEdge, 1920); QCOMPARE(o.jpegQuality, 90); QVERIFY(validateOptions(o).isEmpty());
        o.maxEdge = -1; QVERIFY(!validateOptions(o).isEmpty());
        o.maxEdge = 20001; QVERIFY(!validateOptions(o).isEmpty());
        o.maxEdge = 0; o.jpegQuality = 0; QVERIFY(!validateOptions(o).isEmpty());
        o.jpegQuality = 101; QVERIFY(!validateOptions(o).isEmpty());
        o.jpegQuality = 90; o.format = "gif"; QVERIFY(!validateOptions(o).isEmpty());
        o.format = "png"; o.watermark = QString(101, 'x'); QVERIFY(!validateOptions(o).isEmpty());
    }
    void scanAndEmptyDirectory() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QString error;
        QVERIFY(scanImages(dir.path(), &error).isEmpty()); QVERIFY(error.isEmpty());
        QVERIFY(scanImages(dir.filePath("missing"), &error).isEmpty()); QVERIFY(!error.isEmpty());
        QImage image(30, 20, QImage::Format_RGB32); image.fill(Qt::red);
        QVERIFY(image.save(dir.filePath("UPPER.JPG")));
        QVERIFY(image.save(dir.filePath(QStringLiteral("中文 图片.png"))));
        QVERIFY(QDir().mkpath(dir.filePath("sub")));
        QVERIFY(image.save(dir.filePath("sub/nested.png")));
        QFile extra(dir.filePath("note.txt")); QVERIFY(extra.open(QIODevice::WriteOnly)); extra.write("ignored"); extra.close();
        QCOMPARE(scanImages(dir.path()).size(), 2);
    }
    void proportionalResize() {
        Options o; o.maxEdge = 100; o.format = "png";
        QImage wide(400, 200, QImage::Format_RGB32); wide.fill(Qt::blue);
        QCOMPARE(transformImage(wide, o).size(), QSize(100, 50));
        QCOMPARE(transformImage(wide.transformed(QTransform().rotate(90)), o).size(), QSize(50, 100));
        QImage small(40, 20, QImage::Format_RGB32); small.fill(Qt::red);
        QCOMPARE(transformImage(small, o).size(), small.size());
        o.maxEdge = 0; QCOMPARE(transformImage(wide, o).size(), wide.size());
    }
    void formatRoundTrip_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("jpg") << QString("jpg");
        QTest::newRow("png") << QString("png");
        QTest::newRow("bmp") << QString("bmp");
    }
    void formatRoundTrip() {
        QFETCH(QString, format);
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("透明 中文,图片.png"));
        QImage image(200, 100, QImage::Format_ARGB32); image.fill(Qt::transparent);
        image.setPixelColor(100, 50, Qt::red); QVERIFY(image.save(source));
        const auto original = digest(source);
        Options o; o.maxEdge = 100; o.format = format;
        const auto result = processFile(source, dir.filePath("output space"), o);
        QCOMPARE(result.status, QString("success")); QVERIFY(result.error.isEmpty());
        QImageReader reader(result.outputPath);
        QCOMPARE(reader.format(), format == "jpg" ? QByteArray("jpeg") : format.toLatin1());
        const auto reopened = reader.read();
        QVERIFY(!reopened.isNull()); QCOMPARE(reopened.size(), QSize(100, 50));
        if (format == "png") QCOMPARE(reopened.pixelColor(0, 0).alpha(), 0);
        else QVERIFY(reopened.pixelColor(0, 0).red() >= 250 && reopened.pixelColor(0, 0).green() >= 250);
        QCOMPARE(digest(source), original);
    }
    void jpegQualityAffectsEncoding() {
        QTemporaryDir dir;
        const QString input = dir.filePath("quality.png"); QVERIFY(scene(1200, 800).save(input));
        Options o; o.jpegQuality = 10;
        const auto low = processFile(input, dir.filePath("out"), o);
        o.jpegQuality = 95;
        const auto high = processFile(input, dir.filePath("out"), o);
        QCOMPARE(low.status, QString("success")); QCOMPARE(high.status, QString("success"));
        QCOMPARE(QImage(low.outputPath).size(), QImage(high.outputPath).size());
        QVERIFY(QFileInfo(high.outputPath).size() > QFileInfo(low.outputPath).size());
    }
    void watermarkAndTinyImages() {
        QImage image(800, 400, QImage::Format_RGB32); image.fill(QColor("#24545d"));
        Options o; o.format = "png";
        const auto noWatermark = transformImage(image, o);
        o.watermark = QStringLiteral("© 我的作品 ImageBatch");
        const auto watermarked = transformImage(image, o);
        QVERIFY(watermarked != noWatermark);
        QCOMPARE(watermarked.size(), noWatermark.size());
        QCOMPARE(image.pixelColor(0, 0), QColor("#24545d"));
        QImage tiny(1, 1, QImage::Format_ARGB32); tiny.fill(Qt::transparent);
        QVERIFY(!transformImage(tiny, o).isNull());
    }
    void watermarkCorners_data() {
        QTest::addColumn<int>("position");
        for (int i = 0; i < 4; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void watermarkCorners() {
        QFETCH(int, position);
        QImage source(800, 400, QImage::Format_RGB32); source.fill(Qt::black);
        Options o; o.format = "png"; o.watermark = "CORNER";
        o.watermarkPosition = static_cast<WatermarkPosition>(position);
        const auto result = transformImage(source, o);
        QRect changed;
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                if (result.pixel(x, y) != source.pixel(x, y)) changed |= QRect(x, y, 1, 1);
        QVERIFY(!changed.isEmpty());
        const bool left = position == 1 || position == 3;
        const bool top = position == 2 || position == 3;
        QCOMPARE(changed.center().x() < 400, left);
        QCOMPARE(changed.center().y() < 200, top);
    }
    void exifOrientation() {
        QTemporaryDir dir;
        QImage source(80, 40, QImage::Format_RGB32); source.fill(Qt::red);
        const auto path = dir.filePath("rotated.jpg"); QVERIFY(source.save(path));
        QFile input(path); QVERIFY(input.open(QIODevice::ReadOnly)); QByteArray bytes = input.readAll(); input.close();
        // APP1: Exif + little-endian TIFF, orientation tag 0x0112 = 6 (90 degrees CW).
        const QByteArray exif = QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000");
        bytes.insert(2, exif);
        QVERIFY(input.open(QIODevice::WriteOnly)); QCOMPARE(input.write(bytes), qint64(bytes.size())); input.close();
        QString error; const auto loaded = loadImage(path, &error);
        QVERIFY2(!loaded.isNull(), qPrintable(error)); QCOMPARE(loaded.size(), QSize(40, 80));
        Options o; o.format = "png";
        auto result = processFile(path, dir.filePath("out"), o);
        QCOMPARE(result.status, QString("success")); QCOMPARE(QImage(result.outputPath).size(), QSize(40, 80));
    }
    void failuresAndCollisions() {
        QTemporaryDir dir;
        const QString path = dir.filePath("sample.png"); QVERIFY(scene(120, 80).save(path));
        const auto hash = digest(path); Options o; o.format = "png";
        QCOMPARE(processFile(path, dir.path(), o).status, QString("failed"));
        QCOMPARE(processFile(path, "", o).status, QString("failed"));
        const QString out = dir.filePath("out");
        auto first = processFile(path, out, o); QCOMPARE(first.status, QString("success"));
        const auto firstHash = digest(first.outputPath);
        auto second = processFile(path, out, o); QCOMPARE(second.status, QString("success"));
        QVERIFY(first.outputPath != second.outputPath); QCOMPARE(digest(first.outputPath), firstHash);
        QCOMPARE(digest(path), hash);
        QFile bad(dir.filePath("broken.jpg")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("not an image"); bad.close();
        auto result = processFile(bad.fileName(), out, o);
        QCOMPARE(result.status, QString("failed")); QVERIFY(!result.error.isEmpty());
        QFile blocker(dir.filePath("not_a_directory")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QCOMPARE(processFile(path, blocker.fileName(), o).status, QString("failed"));
        o.maxEdge = -1; QCOMPARE(processFile(path, out, o).status, QString("failed"));
        QCOMPARE(QDir(out).entryList({".imagebatch-*"}, QDir::Files | QDir::Hidden).size(), 0);
    }
    void reportCsvAndFailure() {
        QTemporaryDir dir;
        FileResult result; result.inputPath = QStringLiteral("中文,\"照片\".png"); result.status = "failed"; result.error = "line1\nline2";
        QString error; const auto report = writeReport({result}, dir.path(), &error);
        QVERIFY2(!report.isEmpty(), qPrintable(error)); QFile file(report); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll(); QVERIFY(bytes.startsWith(QByteArray::fromHex("efbbbf")));
        QVERIFY(bytes.contains(QStringLiteral("\"中文,\"\"照片\"\".png\"").toUtf8()));
        QVERIFY(bytes.contains("\"line1\nline2\""));
        QVERIFY(writeReport({result}, dir.path()) != report);
        QFile blocker(dir.filePath("blocked")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QVERIFY(writeReport({result}, blocker.fileName(), &error).isEmpty()); QVERIFY(!error.isEmpty());
    }
    void batchContinuesAfterFailure() {
        QTemporaryDir dir;
        const QString good = dir.filePath("good.png"); QVERIFY(scene(100, 70).save(good));
        const auto token = std::make_shared<std::atomic_bool>(false);
        BatchWorker worker({dir.filePath("missing.jpg"), good}, dir.filePath("out"), Options{}, token);
        QSignalSpy progress(&worker, &BatchWorker::progress), done(&worker, &BatchWorker::finished);
        worker.run(); QCOMPARE(progress.size(), 2); QCOMPARE(done.size(), 1);
        QCOMPARE(done.first()[0].toInt(), 1); QCOMPARE(done.first()[1].toInt(), 1);
        QVERIFY(QFileInfo::exists(done.first()[3].toString()));
    }
    void cancellationStopsRemainingFiles() {
        QTemporaryDir dir; QStringList files;
        for (int i = 0; i < 4; ++i) { auto path = dir.filePath(QString::number(i) + ".png"); QVERIFY(scene(100, 70).save(path)); files << path; }
        auto token = std::make_shared<std::atomic_bool>(false);
        BatchWorker worker(files, dir.filePath("out"), Options{}, token);
        connect(&worker, &BatchWorker::progress, &worker, [token] { token->store(true); }, Qt::DirectConnection);
        QSignalSpy done(&worker, &BatchWorker::finished); worker.run();
        QCOMPARE(done.size(), 1); QCOMPARE(done.first()[0].toInt(), 1); QCOMPARE(done.first()[1].toInt(), 0); QCOMPARE(done.first()[2].toInt(), 3);
        QCOMPARE(QDir(dir.filePath("out")).entryList({"*.jpg"}, QDir::Files).size(), 1);
    }
    void batchSettingsAndEstimatedMemory() {
        BatchSettings settings;
        QVERIFY(validateBatchSettings(settings).isEmpty());
        settings.workers = 0; QVERIFY(!validateBatchSettings(settings).isEmpty());
        settings.workers = 9; QVERIFY(!validateBatchSettings(settings).isEmpty());
        settings.workers = 4; settings.queueCapacity = 0; QVERIFY(!validateBatchSettings(settings).isEmpty());
        settings.queueCapacity = 2; settings.memoryBudgetBytes = 1; QVERIFY(!validateBatchSettings(settings).isEmpty());
        QTemporaryDir dir;
        const auto input = dir.filePath("large.png"); QVERIFY(scene(4000, 3000).save(input));
        Options o;
        QVERIFY(estimateWorkingBytes(input, o) > 64ULL * 1024 * 1024);
        settings.memoryBudgetBytes = 64ULL * 1024 * 1024;
        std::atomic_bool cancelled{false};
        const auto result = runBatch({input}, dir.filePath("out"), o, settings, cancelled);
        QCOMPARE(result.failed, 1); QCOMPARE(result.peakReservedBytes, quint64(0));
        QVERIFY(result.results.first().error.contains(QStringLiteral("预算")));
        settings.workers = 0;
        const auto invalid = runBatch({input}, dir.filePath("invalid"), o, settings, cancelled);
        QCOMPARE(invalid.failed, 1); QVERIFY(!invalid.error.isEmpty());
    }
    void parallelOutputCollisionAndMetrics() {
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("同名 图片.png")); QVERIFY(scene(700, 500).save(source));
        const auto originalHash = digest(source);
        QStringList files; for (int i = 0; i < 16; ++i) files << source;
        Options o; o.format = "png";
        BatchSettings settings; settings.workers = 4; settings.queueCapacity = 2;
        std::atomic_bool cancelled{false}; int notifications = 0;
        BatchCallbacks callbacks;
        callbacks.completed = [&](int done, int total, const FileResult &) { QCOMPARE(done, ++notifications); QCOMPARE(total, 16); };
        const auto summary = runBatch(files, dir.filePath("out"), o, settings, cancelled, callbacks);
        QCOMPARE(summary.succeeded, 16); QCOMPARE(summary.failed, 0); QCOMPARE(notifications, 16);
        QVERIFY(summary.error.isEmpty()); QVERIFY(summary.peakActive <= 4); QVERIFY(summary.peakQueued <= 2);
        QVERIFY(summary.peakReservedBytes <= settings.memoryBudgetBytes);
        QSet<QString> paths;
        for (int i = 0; i < summary.results.size(); ++i) {
            const auto &result = summary.results[i]; QCOMPARE(result.index, i);
            paths.insert(result.outputPath); QVERIFY(!QImage(result.outputPath).isNull());
        }
        QCOMPARE(paths.size(), 16); QCOMPARE(digest(source), originalHash);
        QFile metrics(summary.metricsReport); QVERIFY(metrics.open(QIODevice::ReadOnly));
        const auto json = QJsonDocument::fromJson(metrics.readAll()).object();
        QCOMPARE(json.value("success").toInt(), 16); QCOMPARE(json.value("workers").toInt(), 4);
        QCOMPARE(json.value("peak_queued").toInt(), summary.peakQueued);
        QCOMPARE(QDir(dir.filePath("out")).entryList({".imagebatch-*"}, QDir::Files | QDir::Hidden).size(), 0);
    }
    void concurrentDistinctImagesPreserveContent() {
        QTemporaryDir dir;
        const auto output = dir.filePath("out"); QVERIFY(QDir().mkpath(output));
        const auto sentinel = QDir(output).filePath("same_processed.png");
        QFile existing(sentinel); QVERIFY(existing.open(QIODevice::WriteOnly));
        QCOMPARE(existing.write("keep this existing file"), qint64(23)); existing.close();
        const auto sentinelHash = digest(sentinel);
        QStringList files; QList<QByteArray> originals;
        QList<QColor> colors;
        for (int i = 0; i < 16; ++i) {
            const auto folder = dir.filePath(QString::number(i)); QVERIFY(QDir().mkpath(folder));
            const auto path = QDir(folder).filePath("same.png");
            const QColor color((i * 47) % 256, (i * 83) % 256, (i * 131) % 256);
            QImage image(80, 60, QImage::Format_RGB32); image.fill(color); QVERIFY(image.save(path));
            files << path; colors << color; originals << digest(path);
        }
        BatchSettings settings; settings.workers = 8; settings.queueCapacity = 2;
        Options options; options.format = "png"; std::atomic_bool cancelled{false};
        QSet<QString> allPaths; QMap<QString, QByteArray> savedHashes;
        for (int round = 0; round < 4; ++round) {
            const auto summary = runBatch(files, output, options, settings, cancelled);
            QCOMPARE(summary.succeeded, 16); QCOMPARE(summary.failed, 0);
            for (int i = 0; i < 16; ++i) {
                const auto path = summary.results[i].outputPath;
                QVERIFY(!allPaths.contains(path)); allPaths.insert(path);
                const QImage image(path); QCOMPARE(image.size(), QSize(80, 60));
                QCOMPARE(image.pixelColor(40, 30), colors[i]);
                savedHashes.insert(path, digest(path)); QCOMPARE(digest(files[i]), originals[i]);
            }
            QCOMPARE(digest(sentinel), sentinelHash);
            for (auto it = savedHashes.cbegin(); it != savedHashes.cend(); ++it) QCOMPARE(digest(it.key()), it.value());
        }
        QCOMPARE(allPaths.size(), 64);
        QCOMPARE(QDir(output).entryList({".imagebatch-*"}, QDir::Files | QDir::Hidden).size(), 0);
    }
    void serialAndParallelProduceIdenticalBytes() {
        QTemporaryDir dir; QStringList files;
        for (int i = 0; i < 6; ++i) {
            const auto input = dir.filePath(QString::number(i) + ".png"); QVERIFY(scene(900 + i * 10, 600).save(input)); files << input;
        }
        Options o; o.maxEdge = 700; o.watermark = "SAME OUTPUT";
        std::atomic_bool cancelled{false}; BatchSettings settings;
        const auto serial = runBatch(files, dir.filePath("serial"), o, settings, cancelled);
        settings.workers = 4;
        const auto parallel = runBatch(files, dir.filePath("parallel"), o, settings, cancelled);
        QCOMPARE(serial.succeeded, 6); QCOMPARE(parallel.succeeded, 6);
        for (int i = 0; i < files.size(); ++i) QCOMPARE(digest(serial.results[i].outputPath), digest(parallel.results[i].outputPath));
    }
    void budgetLimitsAdmissionsAndParallelCancellation() {
        QTemporaryDir dir;
        const auto input = dir.filePath("input.png"); QVERIFY(scene(1600, 1000).save(input));
        QStringList files; for (int i = 0; i < 12; ++i) files << input;
        BatchSettings settings; settings.workers = 4; settings.queueCapacity = 2;
        settings.memoryBudgetBytes = 64ULL * 1024 * 1024;
        std::atomic_bool cancelled{false}; Options o;
        const auto limited = runBatch(files, dir.filePath("limited"), o, settings, cancelled);
        QCOMPARE(limited.succeeded, 12);
        QCOMPARE(limited.peakReservedBytes, estimateWorkingBytes(input, o));
        cancelled.store(true);
        const auto preCancelled = runBatch(files, dir.filePath("before"), o, settings, cancelled);
        QCOMPARE(preCancelled.cancelled, 12); QCOMPARE(preCancelled.succeeded, 0);
        QCOMPARE(preCancelled.peakReservedBytes, quint64(0));
        cancelled.store(false); settings.memoryBudgetBytes = 512ULL * 1024 * 1024;
        BatchCallbacks callbacks;
        callbacks.completed = [&](int, int, const FileResult &) { cancelled.store(true); };
        const auto during = runBatch(files, dir.filePath("during"), o, settings, cancelled, callbacks);
        QVERIFY(during.succeeded >= 1 && during.succeeded <= 4);
        QCOMPARE(during.failed, 0); QCOMPARE(during.succeeded + during.cancelled, 12);
    }
    void callbackFailureIsContained() {
        QTemporaryDir dir;
        const auto input = dir.filePath("input.png"); QVERIFY(scene(200, 100).save(input));
        BatchCallbacks callbacks;
        callbacks.completed = [](int, int, const FileResult &) { throw std::runtime_error("observer failure"); };
        std::atomic_bool cancelled{false};
        const auto summary = runBatch({input}, dir.filePath("out"), Options{}, BatchSettings{}, cancelled, callbacks);
        QCOMPARE(summary.succeeded, 1); QVERIFY(summary.error.contains(QStringLiteral("通知异常")));
        QVERIFY(QFileInfo::exists(summary.metricsReport));
    }
    void preserveOriginalFormatAndTransparency() {
        QTemporaryDir dir; Options options; options.format = "original"; options.maxEdge = 100;
        QImage image(160, 100, QImage::Format_ARGB32); image.fill(Qt::transparent);
        { QPainter painter(&image); painter.fillRect(30, 20, 60, 50, Qt::green); }
        for (const auto &format : QStringList{"jpg", "png", "bmp"}) {
            const auto input = dir.filePath("sample." + format); QVERIFY(image.save(input));
            const auto result = processFile(input, dir.filePath("out"), options);
            QCOMPARE(result.status, QString("success"));
            QCOMPARE(QFileInfo(result.outputPath).suffix(), format);
            QCOMPARE(QImage(result.outputPath).size(), QSize(100, 62)); // Integer dimensions round down here (62.5 pixels).
            if (format == "png") QCOMPARE(QImage(result.outputPath).pixelColor(0, 0).alpha(), 0);
            else QVERIFY(QImage(result.outputPath).pixelColor(0, 0).alpha() == 255);
        }
        const auto misleading = dir.filePath("actually_png.jpg"); QVERIFY(image.save(misleading, "PNG"));
        const auto result = processFile(misleading, dir.filePath("out"), options);
        QCOMPARE(result.status, QString("success")); QVERIFY(result.outputPath.endsWith(".png"));
        QFile broken(dir.filePath("broken.png")); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("broken"); broken.close();
        QCOMPARE(processFile(broken.fileName(), dir.filePath("out"), options).status, QString("failed"));
    }
    void preferencesRoundTripAndInvalidData() {
        QTemporaryDir dir; const auto path = dir.filePath("settings.ini");
        Preferences p; p.options.maxEdge = 1280; p.options.format = "original"; p.options.jpegQuality = 81;
        p.options.watermark = QStringLiteral("我的图片"); p.options.watermarkPosition = WatermarkPosition::TopLeft;
        p.watermarkEnabled = true; p.batch.workers = 3; p.batch.memoryBudgetBytes = 256ULL * 1024 * 1024;
        p.lastFolder = dir.filePath(QStringLiteral("中文 space"));
        QString error; QVERIFY(writePreferences(path, p, &error)); QVERIFY(error.isEmpty());
        const auto restored = readPreferences(path);
        QCOMPARE(restored.options.maxEdge, 1280); QCOMPARE(restored.options.format, QString("original"));
        QCOMPARE(restored.options.watermark, p.options.watermark); QVERIFY(restored.watermarkEnabled);
        QCOMPARE(restored.options.watermarkPosition, WatermarkPosition::TopLeft);
        QCOMPARE(restored.batch.workers, 3); QCOMPARE(restored.batch.memoryBudgetBytes, p.batch.memoryBudgetBytes);
        QCOMPARE(restored.lastFolder, p.lastFolder);
        { QSettings settings(path, QSettings::IniFormat);
          settings.setValue("image/maxEdge", "bad"); settings.setValue("image/quality", 0);
          settings.setValue("image/format", "tiff"); settings.setValue("batch/workers", 999);
          settings.setValue("batch/budgetMiB", -1); settings.setValue("image/position", 999); settings.sync(); }
        const auto invalid = readPreferences(path);
        QCOMPARE(invalid.options.maxEdge, 1920); QCOMPARE(invalid.options.jpegQuality, 90);
        QCOMPARE(invalid.options.format, QString("jpg")); QCOMPARE(invalid.batch.workers, 2);
        QCOMPARE(invalid.batch.memoryBudgetBytes, quint64(512) * 1024 * 1024);
        QCOMPARE(invalid.options.watermarkPosition, WatermarkPosition::BottomRight);
        QFile blocker(dir.filePath("blocker")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QVERIFY(!writePreferences(dir.filePath("blocker/settings.ini"), p, &error)); QVERIFY(!error.isEmpty());
    }
    void guiDropDeduplicateAndRemove() {
        QTemporaryDir dir;
        const auto first = dir.filePath("a.png"), second = dir.filePath("b.png");
        QVERIFY(scene(160, 100).save(first)); QVERIFY(scene(160, 100).save(second));
        QVERIFY(QDir().mkpath(dir.filePath("sub"))); QVERIFY(scene(160, 100).save(dir.filePath("sub/nested.png")));
        MainWindow window; window.show();
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(first), QUrl::fromLocalFile(dir.path()), QUrl("https://example.invalid/image.png")});
        QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter); QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drop); QVERIFY(drop.isAccepted());
        auto *table = window.findChild<QTableWidget *>("fileTable"); QCOMPARE(table->rowCount(), 2);
        window.addPaths({first, second, dir.filePath("missing.png")}); QCOMPARE(table->rowCount(), 2);
        window.findChild<QPushButton *>("selectNoneButton")->click();
        QVERIFY(!window.findChild<QPushButton *>("startButton")->isEnabled());
        table->item(1, 0)->setCheckState(Qt::Checked);
        window.findChild<QPushButton *>("removeButton")->click();
        QCOMPARE(table->rowCount(), 1); QVERIFY(QFileInfo::exists(second));
        window.findChild<QPushButton *>("clearButton")->click();
        QCOMPARE(table->rowCount(), 0); QVERIFY(QFileInfo::exists(first));
        QVERIFY(window.findChild<QLabel *>("afterPreview")->pixmap().isNull());
        window.addPaths({first, dir.filePath("sub/nested.png")});
        window.findChild<QLineEdit *>("outputPath")->setText(dir.filePath("sub"));
        window.findChild<QPushButton *>("startButton")->click(); QVERIFY(!window.busy());
        QVERIFY(window.findChild<QLabel *>("statusText")->text().contains(QStringLiteral("必须与原图")));
    }
    void guiSelectedSubsetAndRetry() {
        QTemporaryDir dir;
        const auto skipped = dir.filePath("a_skip.png"), good = dir.filePath("b_good.png"), bad = dir.filePath("c_bad.png");
        QVERIFY(scene(300, 200).save(skipped)); QVERIFY(scene(300, 200).save(good));
        QFile broken(bad); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("broken"); broken.close();
        MainWindow window; window.show(); window.addPaths({skipped, good, bad});
        auto *table = window.findChild<QTableWidget *>("fileTable");
        table->item(0, 0)->setCheckState(Qt::Unchecked);
        QSignalSpy done(&window, &MainWindow::batchComplete);
        window.findChild<QPushButton *>("startButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("待处理"));
        QCOMPARE(table->item(1, 1)->text(), QStringLiteral("成功")); QCOMPARE(table->item(2, 1)->text(), QStringLiteral("失败"));
        table->setCurrentCell(2, 0);
        QVERIFY(window.findChild<QPlainTextEdit *>("fileDetails")->toPlainText().contains(QStringLiteral("原因")));
        const auto out = dir.filePath("processed");
        const auto goodOut = QDir(out).filePath("b_good_processed.jpg"); const auto hash = digest(goodOut);
        QVERIFY(scene(300, 200).save(bad));
        auto *retry = window.findChild<QPushButton *>("retryButton"); QVERIFY(retry->isEnabled()); retry->click();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 15000);
        QVERIFY(!retry->isEnabled()); QCOMPARE(table->item(2, 1)->text(), QStringLiteral("成功"));
        QCOMPARE(digest(goodOut), hash); QCOMPARE(QDir(out).entryList({"*.jpg"}, QDir::Files).size(), 2);
        QVERIFY(!QFileInfo::exists(QDir(out).filePath("a_skip_processed.jpg")));
        QFile report(window.lastReport()); QVERIFY(report.open(QIODevice::ReadOnly));
        QCOMPARE(report.readAll().count("\"success\""), 1); // Retry report contains only its own selected attempt.
    }
    void guiPreferencesSmallWindowAndAutoClose() {
        QTemporaryDir dir; const auto settingsFile = dir.filePath("prefs.ini");
        {
            MainWindow window(nullptr, settingsFile); window.show(); window.resize(860, 520);
            auto *scroll = window.findChild<QScrollArea *>("contentScroll");
            QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
            auto *start = window.findChild<QPushButton *>("startButton");
            QVERIFY(window.rect().contains(start->mapTo(&window, start->rect().center())));
            QVERIFY(!window.findChild<QLineEdit *>("inputPath")->acceptDrops());
            const auto artifacts = qEnvironmentVariable("IMAGEBATCH_ARTIFACT_DIR");
            if (!artifacts.isEmpty()) QVERIFY(window.grab().save(QDir(artifacts).filePath("small-window.png")));
            window.findChild<QSpinBox *>("maxEdge")->setValue(1280);
            window.findChild<QComboBox *>("outputFormat")->setCurrentIndex(3);
            window.findChild<QCheckBox *>("watermarkEnabled")->setChecked(true);
            window.findChild<QLineEdit *>("watermarkText")->setText(QStringLiteral("设置测试"));
            window.findChild<QSpinBox *>("workerCount")->setValue(3); window.close();
        }
        MainWindow restored(nullptr, settingsFile); restored.show();
        QCOMPARE(restored.findChild<QSpinBox *>("maxEdge")->value(), 1280);
        QCOMPARE(restored.findChild<QComboBox *>("outputFormat")->currentData().toString(), QString("original"));
        QCOMPARE(restored.findChild<QLineEdit *>("watermarkText")->text(), QStringLiteral("设置测试"));
        restored.findChild<QPushButton *>("resetOptionsButton")->click();
        QCOMPARE(restored.findChild<QSpinBox *>("maxEdge")->value(), 1920);
        QCOMPARE(restored.findChild<QSpinBox *>("workerCount")->value(), 2);
        for (int i = 0; i < 8; ++i) QVERIFY(scene(1200, 800).save(dir.filePath(QString::number(i) + ".png")));
        restored.addPaths({dir.path()});
        QSignalSpy done(&restored, &MainWindow::batchComplete);
        restored.findChild<QPushButton *>("startButton")->click(); QVERIFY(restored.busy()); restored.close();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000); QTRY_VERIFY(!restored.isVisible());
        QVERIFY(QFileInfo::exists(restored.lastReport()));
    }
    void guiOriginalFormatPreview() {
        QTemporaryDir dir; QImage image(200, 100, QImage::Format_ARGB32); image.fill(Qt::transparent);
        const auto path = dir.filePath("alpha.png"); QVERIFY(image.save(path));
        MainWindow window; window.show();
        window.findChild<QComboBox *>("outputFormat")->setCurrentIndex(3);
        QSignalSpy preview(&window, &MainWindow::previewReady); window.addPaths({path});
        QTRY_VERIFY_WITH_TIMEOUT(preview.size() > 0, 10000);
        QVERIFY(!window.findChild<QLabel *>("afterPreview")->pixmap().isNull());
        QSignalSpy done(&window, &MainWindow::batchComplete); window.findChild<QPushButton *>("startButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QImage output(dir.filePath("processed/alpha_processed.png")); QVERIFY(!output.isNull());
        QCOMPARE(output.pixelColor(0, 0).alpha(), 0);
    }
    void guiImportPreviewAndProcessing() {
        QTemporaryDir dir;
        const QString input = dir.filePath(QStringLiteral("中文图片 space")); QVERIFY(QDir().mkpath(input));
        const QString sample = QDir(input).filePath(QStringLiteral("山间旅行.png")); QVERIFY(scene().save(sample));
        QVERIFY(scene(1200, 1800).save(QDir(input).filePath(QStringLiteral("示例_竖图.png"))));
        QVERIFY(scene(1800, 1200).save(QDir(input).filePath(QStringLiteral("示例_照片.jpg"))));
        const auto hash = digest(sample);
        MainWindow window; window.show();
        QSignalSpy preview(&window, &MainWindow::previewReady), done(&window, &MainWindow::batchComplete);
        window.findChild<QLineEdit *>("inputPath")->setText(input);
        QTest::mouseClick(window.findChild<QPushButton *>("importButton"), Qt::LeftButton);
        QCOMPARE(window.findChild<QTableWidget *>("fileTable")->rowCount(), 3);
        window.findChild<QSpinBox *>("maxEdge")->setValue(1200);
        window.findChild<QComboBox *>("outputFormat")->setCurrentIndex(1);
        window.findChild<QCheckBox *>("watermarkEnabled")->setChecked(true);
        window.findChild<QLineEdit *>("watermarkText")->setText(QStringLiteral("© 我的作品 · ImageBatch"));
        QTRY_VERIFY_WITH_TIMEOUT(preview.size() >= 1, 30000);
        QVERIFY(!window.findChild<QLabel *>("afterPreview")->pixmap().isNull());
        QTest::mouseClick(window.findChild<QPushButton *>("startButton"), Qt::LeftButton);
        QVERIFY(window.busy());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30000);
        QVERIFY(!window.busy()); QVERIFY(QFileInfo::exists(window.lastReport()));
        const QString out = QDir(input).filePath("processed/山间旅行_processed.png");
        QVERIFY(QFileInfo::exists(out)); QCOMPARE(QImage(out).size(), QSize(1200, 800));
        QCOMPARE(digest(sample), hash);
        QVERIFY(window.findChild<QLabel *>("statusText")->text().contains(QStringLiteral("成功 3，失败 0")));
        const QString artifacts = qEnvironmentVariable("IMAGEBATCH_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            // Portable screenshots and original generated samples; no private photos.
            window.findChild<QLineEdit *>("inputPath")->setText(QStringLiteral("示例图片文件夹"));
            window.findChild<QLineEdit *>("outputPath")->setText(QStringLiteral("示例图片文件夹/processed"));
            window.findChild<QPlainTextEdit *>("fileDetails")->setPlainText(QStringLiteral("输入：示例图片文件夹/山间旅行.png\n输出：示例图片文件夹/processed/山间旅行_processed.png"));
            QTest::qWait(200);
            // Render the full content at a taller layout, without private fixture paths.
            auto *shell = window.centralWidget(); const auto previousSize = shell->size();
            const auto fullHeight = window.findChild<QScrollArea *>("contentScroll")->widget()->height()
                + window.findChild<QWidget *>("footerPanel")->height();
            shell->resize(shell->width(), fullHeight); shell->layout()->activate();
            QVERIFY(shell->grab().save(QDir(artifacts).filePath("screenshot.png"))); shell->resize(previousSize);
            QVERIFY(QFile::copy(sample, QDir(artifacts).filePath("sample_original.png")));
            QVERIFY(QFile::copy(out, QDir(artifacts).filePath("sample_processed.png")));
        }
    }
    void guiBrokenPreviewStaysClearedAfterResize() {
        QTemporaryDir dir;
        QVERIFY(scene(200, 100).save(dir.filePath("a_good.png")));
        QFile bad(dir.filePath("z_bad.jpg")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("broken"); bad.close();
        MainWindow window; window.show();
        QSignalSpy preview(&window, &MainWindow::previewReady);
        window.findChild<QLineEdit *>("inputPath")->setText(dir.path());
        QTest::mouseClick(window.findChild<QPushButton *>("importButton"), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(preview.size(), 1, 10000);
        QVERIFY(!window.findChild<QLabel *>("afterPreview")->pixmap().isNull());
        window.findChild<QTableWidget *>("fileTable")->selectRow(1);
        QTRY_COMPARE_WITH_TIMEOUT(preview.size(), 2, 10000);
        QVERIFY(window.findChild<QLabel *>("afterPreview")->pixmap().isNull());
        window.resize(1000, 760); QTest::qWait(100);
        QVERIFY(window.findChild<QLabel *>("afterPreview")->pixmap().isNull());
    }
    void guiEmptyInvalidAndCancel() {
        QTemporaryDir dir; MainWindow window; window.show();
        auto *path = window.findChild<QLineEdit *>("inputPath");
        auto *start = window.findChild<QPushButton *>("startButton");
        auto *import = window.findChild<QPushButton *>("importButton");
        path->setText(dir.path()); QTest::mouseClick(import, Qt::LeftButton); QVERIFY(!start->isEnabled());
        path->setText(dir.filePath("missing")); QTest::mouseClick(import, Qt::LeftButton); QVERIFY(!start->isEnabled());
        for (int i = 0; i < 8; ++i) QVERIFY(scene(1000, 700).save(dir.filePath(QString::number(i) + ".png")));
        path->setText(dir.path()); QTest::mouseClick(import, Qt::LeftButton);
        auto *output = window.findChild<QLineEdit *>("outputPath");
        output->setText(dir.path()); QTest::mouseClick(start, Qt::LeftButton); QVERIFY(!window.busy());
        QVERIFY(window.findChild<QLabel *>("statusText")->text().contains(QStringLiteral("必须与原图")));
        output->setText(dir.filePath("out"));
        QSignalSpy done(&window, &MainWindow::batchComplete);
        QTest::mouseClick(start, Qt::LeftButton);
        QTest::mouseClick(window.findChild<QPushButton *>("cancelButton"), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30000);
        const auto results = QDir(dir.filePath("out")).entryList({"*.jpg"}, QDir::Files);
        QVERIFY(results.size() < 8); QVERIFY(QFileInfo::exists(window.lastReport()));
        QVERIFY(start->isEnabled());
        QMap<QString, QByteArray> completedHashes;
        for (const auto &name : results) completedHashes.insert(name, digest(QDir(dir.filePath("out")).filePath(name)));
        auto *retry = window.findChild<QPushButton *>("retryButton"); QVERIFY(retry->isEnabled()); retry->click();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30000);
        QCOMPARE(QDir(dir.filePath("out")).entryList({"*.jpg"}, QDir::Files).size(), 8);
        for (auto it = completedHashes.cbegin(); it != completedHashes.cend(); ++it)
            QCOMPARE(digest(QDir(dir.filePath("out")).filePath(it.key())), it.value());
    }
};
QTEST_MAIN(ImageBatchTests)
#include "test_imagebatch.moc"
