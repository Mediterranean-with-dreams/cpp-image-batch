#include "batchengine.h"
#include "resourcebudget.h"
#include "threadpool.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <limits>
#include <memory>
namespace imagebatch {
QString validateBatchSettings(const BatchSettings &s) {
    if (s.workers < 1 || s.workers > 8) return QStringLiteral("并行任务数必须为 1～8");
    if (s.queueCapacity < 1 || s.queueCapacity > 1024) return QStringLiteral("队列容量必须为 1～1024");
    if (s.memoryBudgetBytes < 64ULL * 1024 * 1024 || s.memoryBudgetBytes > 4096ULL * 1024 * 1024)
        return QStringLiteral("批处理估算预算必须为 64～4096 MiB");
    return {};
}
quint64 estimateWorkingBytes(const QString &file, const Options &options, QString *error) {
    if (error) error->clear();
    QImageReader reader(file);
    const QSize input = reader.size();
    if (!input.isValid() || input.isEmpty()) {
        if (error) *error = reader.errorString();
        return 0;
    }
    const auto pixels = quint64(input.width()) * quint64(input.height());
    const auto output = options.maxEdge > 0 && std::max(input.width(), input.height()) > options.maxEdge
        ? input.scaled(options.maxEdge, options.maxEdge, Qt::KeepAspectRatio) : input;
    const auto outputPixels = quint64(output.width()) * quint64(output.height());
    constexpr quint64 overhead = 8ULL * 1024 * 1024;
    constexpr auto maximum = std::numeric_limits<quint64>::max();
    if (pixels > (maximum - overhead) / 20 || outputPixels > pixels) return maximum;
    // Three estimated RGBA input buffers + two output buffers + workspace.
    // Admission accounting is separate from actual process working set.
    return pixels * 12 + outputPixels * 8 + overhead;
}
static FileResult terminalResult(int index, const QString &file, const QString &status, const QString &error) {
    FileResult r; r.index = index; r.inputPath = file; r.status = status; r.error = error; return r;
}
BatchSummary runBatch(const QStringList &files, const QString &directory, const Options &options,
                      const BatchSettings &settings, const std::atomic_bool &cancelled,
                      const BatchCallbacks &callbacks) {
    BatchSummary summary;
    summary.results.resize(files.size());
    std::vector<bool> finished(static_cast<std::size_t>(files.size()), false);
    std::mutex resultMutex;
    int completed = 0, callbackErrors = 0;
    QElapsedTimer clock; clock.start();
    auto complete = [&](int index, FileResult result) {
        std::lock_guard<std::mutex> lock(resultMutex);
        if (finished[static_cast<std::size_t>(index)]) return;
        finished[static_cast<std::size_t>(index)] = true;
        result.index = index;
        summary.results[index] = result;
        if (result.status == "success") ++summary.succeeded;
        else if (result.status == "cancelled") ++summary.cancelled;
        else ++summary.failed;
        ++completed;
        try { if (callbacks.completed) callbacks.completed(completed, files.size(), result); }
        catch (...) { ++callbackErrors; }
    };
    QString error = validateOptions(options);
    if (error.isEmpty()) error = validateBatchSettings(settings);
    if (error.isEmpty() && directory.trimmed().isEmpty()) error = QStringLiteral("输出文件夹不能为空");
    if (!error.isEmpty()) {
        for (int i = 0; i < files.size(); ++i) complete(i, terminalResult(i, files[i], "failed", error));
        summary.error = error;
        return summary;
    }
    ResourceBudget budget(settings.memoryBudgetBytes);
    std::unique_ptr<ThreadPool> pool;
    std::vector<std::pair<int, std::future<void>>> futures;
    try {
        pool = std::make_unique<ThreadPool>(settings.workers, settings.queueCapacity);
        for (int i = 0; i < files.size(); ++i) {
            if (cancelled.load()) {
                complete(i, terminalResult(i, files[i], "cancelled", QStringLiteral("用户取消，未处理")));
                continue;
            }
            auto future = pool->submit([&, i] {
                FileResult result;
                try {
                    if (cancelled.load()) {
                        complete(i, terminalResult(i, files[i], "cancelled", QStringLiteral("用户取消，未处理")));
                        return;
                    }
                    QString readError;
                    const quint64 bytes = estimateWorkingBytes(files[i], options, &readError);
                    if (!bytes) result = terminalResult(i, files[i], "failed", readError);
                    else if (bytes > settings.memoryBudgetBytes)
                        result = terminalResult(i, files[i], "failed", QStringLiteral("单图估算需求超过批处理预算，请提高预算或先缩小原图"));
                    else {
                        auto lease = budget.acquire(bytes, cancelled);
                        if (!lease) result = terminalResult(i, files[i], "cancelled", QStringLiteral("用户取消，未处理"));
                        else {
                            {
                                std::lock_guard<std::mutex> lock(resultMutex);
                                try { if (callbacks.started) callbacks.started(i); } catch (...) { ++callbackErrors; }
                            }
                            QElapsedTimer imageClock; imageClock.start();
                            result = processFile(files[i], directory, options);
                            result.elapsedMs = imageClock.elapsed();
                        }
                    }
                } catch (const std::exception &e) {
                    result = terminalResult(i, files[i], "failed", QStringLiteral("处理异常：") + QString::fromUtf8(e.what()));
                } catch (...) {
                    result = terminalResult(i, files[i], "failed", QStringLiteral("未知处理异常"));
                }
                complete(i, result);
            });
            futures.emplace_back(i, std::move(future));
        }
    } catch (const std::exception &e) {
        error = QStringLiteral("任务调度失败：") + QString::fromUtf8(e.what());
    }
    for (auto &item : futures) {
        try { item.second.get(); }
        catch (const std::exception &e) { complete(item.first, terminalResult(item.first, files[item.first], "failed", QString::fromUtf8(e.what()))); }
        catch (...) { complete(item.first, terminalResult(item.first, files[item.first], "failed", QStringLiteral("任务异常"))); }
    }
    // Join accepted jobs before reading final state and destroying the budget.
    if (pool) {
        const auto stats = pool->stats();
        summary.peakActive = static_cast<int>(stats.peakActive);
        summary.peakQueued = static_cast<int>(stats.peakQueued);
        pool.reset();
    }
    for (int i = 0; i < files.size(); ++i)
        if (!finished[static_cast<std::size_t>(i)]) complete(i, terminalResult(i, files[i], "failed", error));
    summary.peakReservedBytes = budget.peakReserved();
    summary.elapsedMs = clock.elapsed();
    QString reportError;
    summary.report = writeReport(summary.results, directory, &reportError);
    if (callbackErrors) error += QStringLiteral("；%1 次进度通知异常").arg(callbackErrors);
    summary.error = error + reportError;
    if (!summary.report.isEmpty()) {
        summary.metricsReport = summary.report.left(summary.report.size() - 4) + ".json";
        QJsonObject metrics{{"version", IMAGEBATCH_VERSION}, {"workers", settings.workers},
            {"queue_capacity", settings.queueCapacity}, {"peak_queued", summary.peakQueued},
            {"peak_active_workers", summary.peakActive}, {"memory_budget_bytes", static_cast<qint64>(settings.memoryBudgetBytes)},
            {"peak_reserved_bytes", static_cast<qint64>(summary.peakReservedBytes)},
            {"memory_accounting", "estimated image buffers; not a process memory hard limit"},
            {"elapsed_ms", summary.elapsedMs}, {"success", summary.succeeded},
            {"failed", summary.failed}, {"cancelled", summary.cancelled}, {"callback_errors", callbackErrors}};
        QFile file(summary.metricsReport);
        const QByteArray json = QJsonDocument(metrics).toJson();
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(json) != json.size() || !file.flush()) {
            summary.error += QStringLiteral("；汇总 JSON 保存失败：") + file.errorString();
            summary.metricsReport.clear();
        }
    }
    return summary;
}
}
