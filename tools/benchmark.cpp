#include "batchengine.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QThreadPool>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#include <unistd.h>
#endif
using namespace imagebatch;
static quint64 workingSet() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return counters.WorkingSetSize;
    return 0;
#else
    std::ifstream stat("/proc/self/statm"); quint64 total = 0, resident = 0;
    if (stat >> total >> resident) return resident * static_cast<quint64>(sysconf(_SC_PAGESIZE));
    return 0;
#endif
}
class MemorySampler {
public:
    MemorySampler() : baseline_(workingSet()), peak_(baseline_), worker_([this] {
        while (!stop_.load()) {
            peak_.store(std::max(peak_.load(), workingSet()));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        peak_.store(std::max(peak_.load(), workingSet()));
    }) {}
    ~MemorySampler() { finish(); }
    void finish() { stop_.store(true); if (worker_.joinable()) worker_.join(); }
    quint64 peak() const { return peak_.load(); }
    quint64 baseline() const { return baseline_; }
private:
    quint64 baseline_;
    std::atomic<quint64> peak_;
    std::atomic_bool stop_{false};
    std::thread worker_;
};
static QString sha256(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot hash image");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) throw std::runtime_error("Cannot read image for hashing");
    return QString::fromLatin1(hash.result().toHex());
}
static void generate(const QString &directory, int count) {
    QDir dir(directory);
    if (dir.exists() && !dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        throw std::runtime_error("Corpus directory must be empty; refusing to overwrite");
    if (!QDir().mkpath(directory)) throw std::runtime_error("Cannot create corpus directory");
    QJsonArray manifest;
    for (int n = 0; n < count; ++n) {
        const int width = n % 3 == 0 ? 2560 : n % 3 == 1 ? 2400 : 1600;
        const int height = n % 3 == 0 ? 1440 : n % 3 == 1 ? 1600 : 2400;
        const bool alpha = n % 3 == 1;
        QImage image(width, height, QImage::Format_ARGB32);
        for (int y = 0; y < height; ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                quint32 seed = quint32(x) * 747796405U + quint32(y) * 2891336453U + quint32(n + 1) * 277803737U;
                seed ^= seed >> 16; seed *= 2246822519U; seed ^= seed >> 13;
                const int noise = static_cast<int>(seed & 31);
                row[x] = qRgba((x * 211 / width + noise + n * 7) % 256,
                    (y * 211 / height + noise + n * 11) % 256, ((x / 12 + y / 12) * 5 + n * 17) % 256,
                    alpha ? 80 + (x * 175 / width) : 255);
            }
        }
        const QString format = alpha ? "png" : "jpg";
        const QString name = QString("sample_%1.%2").arg(n, 3, 10, QChar('0')).arg(format);
        QImageWriter writer(dir.filePath(name), format.toLatin1());
        if (alpha) writer.setCompression(3); else writer.setQuality(92);
        if (!writer.write(image)) throw std::runtime_error(writer.errorString().toStdString());
        manifest.append(QJsonObject{{"file", name}, {"width", width}, {"height", height}, {"sha256", sha256(dir.filePath(name))}});
    }
    QFile file(dir.filePath("corpus.json"));
    if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot write corpus manifest");
    file.write(QJsonDocument(manifest).toJson());
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationVersion(IMAGEBATCH_VERSION);
    QImageReader::setAllocationLimit(256);
    // Avoid nested Qt scaling work expanding with the outer worker count.
    QThreadPool::globalInstance()->setMaxThreadCount(1);
    QCommandLineParser parser;
    parser.setApplicationDescription("Reproducible ImageBatch benchmark; each invocation measures one fresh process.");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({{"generate", "Generate deterministic mixed JPEG/PNG corpus in an empty directory", "directory"},
        {"count", "Corpus image count (1..300)", "count", "36"},
        {"input", "Input corpus directory", "directory"}, {"output", "Output directory", "directory"},
        {"threads", "Worker count", "count", "1"}, {"budget-mib", "Estimated image buffer budget", "MiB", "512"},
        {"result", "Measurement JSON file", "file"}});
    parser.process(app);
    try {
        if (parser.isSet("generate")) {
            bool ok = false; const int count = parser.value("count").toInt(&ok);
            if (!ok || count < 1 || count > 300) throw std::runtime_error("Count must be 1..300");
            generate(parser.value("generate"), count);
            return 0;
        }
        if (!parser.isSet("input") || !parser.isSet("output") || !parser.isSet("result"))
            throw std::runtime_error("Specify --input, --output and --result");
        QString error;
        const auto files = scanImages(parser.value("input"), &error);
        if (!error.isEmpty() || files.isEmpty()) throw std::runtime_error("Input corpus is empty or invalid");
        const QString output = parser.value("output");
        if (QDir(output).exists() && !QDir(output).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
            throw std::runtime_error("Output directory must be empty");
        QJsonObject before;
        for (const auto &file : files) before.insert(QFileInfo(file).fileName(), sha256(file));
        BatchSettings settings;
        bool threadsValid = false, budgetValid = false;
        settings.workers = parser.value("threads").toInt(&threadsValid);
        const quint64 budget = parser.value("budget-mib").toULongLong(&budgetValid);
        if (!threadsValid || !budgetValid || budget > 4096) throw std::runtime_error("Invalid worker or budget parameter");
        settings.memoryBudgetBytes = budget * 1024 * 1024;
        settings.queueCapacity = settings.workers * 2;
        error = validateBatchSettings(settings);
        if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
        Options options; options.watermark = "ImageBatch Benchmark";
        std::atomic_bool cancelled{false};
        MemorySampler memory;
        QElapsedTimer clock; clock.start();
        const auto summary = runBatch(files, output, options, settings, cancelled);
        const qint64 wall = clock.elapsed();
        memory.finish();
        bool outputsValid = summary.succeeded == files.size() && summary.failed == 0 && summary.error.isEmpty();
        QJsonObject after, hashes;
        for (const auto &file : files) after.insert(QFileInfo(file).fileName(), sha256(file));
        for (const auto &result : summary.results) {
            if (result.status != "success") { outputsValid = false; continue; }
            const QSize expected = std::max(result.originalSize.width(), result.originalSize.height()) > options.maxEdge
                ? result.originalSize.scaled(options.maxEdge, options.maxEdge, Qt::KeepAspectRatio) : result.originalSize;
            if (QImageReader(result.outputPath).size() != expected) outputsValid = false;
            hashes.insert(QFileInfo(result.inputPath).fileName(), sha256(result.outputPath));
        }
        QJsonObject result{{"version", IMAGEBATCH_VERSION}, {"qt", qVersion()}, {"compiler", __VERSION__},
            {"os", QSysInfo::prettyProductName()}, {"architecture", QSysInfo::currentCpuArchitecture()},
            {"hardware_threads", static_cast<int>(std::thread::hardware_concurrency())},
            {"workers", settings.workers}, {"queue_capacity", settings.queueCapacity}, {"qt_helper_threads", 1},
            {"images", files.size()}, {"max_edge", options.maxEdge}, {"format", options.format},
            {"jpeg_quality", options.jpegQuality}, {"watermark", options.watermark},
            {"wall_ms", wall}, {"images_per_second", wall > 0 ? files.size() * 1000.0 / wall : 0.0},
            {"sampled_peak_working_set_bytes", static_cast<qint64>(memory.peak())},
            {"baseline_working_set_bytes", static_cast<qint64>(memory.baseline())}, {"memory_sampling_interval_ms", 5},
            {"estimated_peak_reserved_bytes", static_cast<qint64>(summary.peakReservedBytes)},
            {"budget_bytes", static_cast<qint64>(settings.memoryBudgetBytes)},
            {"peak_active_workers", summary.peakActive}, {"peak_queued", summary.peakQueued},
            {"success", summary.succeeded}, {"failed", summary.failed},
            {"inputs_unchanged", before == after}, {"outputs_valid", outputsValid},
            {"input_hashes", before}, {"output_hashes", hashes}};
        QFile report(parser.value("result"));
        if (!report.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot write measurement JSON");
        const QByteArray data = QJsonDocument(result).toJson();
        if (report.write(data) != data.size()) throw std::runtime_error("Measurement JSON write failed");
        return outputsValid && before == after ? 0 : 1;
    } catch (const std::exception &e) {
        std::cerr << "Benchmark error: " << e.what() << '\n';
        return 2;
    }
}
