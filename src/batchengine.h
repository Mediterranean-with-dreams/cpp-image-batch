#pragma once
#include "processor.h"
#include <atomic>
#include <functional>
namespace imagebatch {
struct BatchSettings {
    int workers = 1;
    int queueCapacity = 8;
    quint64 memoryBudgetBytes = 512ULL * 1024 * 1024;
};
struct BatchSummary {
    QList<FileResult> results; // Input order, independent of completion order.
    int succeeded = 0, failed = 0, cancelled = 0;
    int peakActive = 0, peakQueued = 0;
    quint64 peakReservedBytes = 0;
    qint64 elapsedMs = 0;
    QString report, metricsReport, error;
};
struct BatchCallbacks {
    // Notifications are serialized; exceptions are contained and reported.
    std::function<void(int index)> started;
    std::function<void(int completed, int total, const FileResult &)> completed;
};
QString validateBatchSettings(const BatchSettings &settings);
quint64 estimateWorkingBytes(const QString &file, const Options &options, QString *error = nullptr);
BatchSummary runBatch(const QStringList &files, const QString &directory, const Options &options,
                      const BatchSettings &settings, const std::atomic_bool &cancelled,
                      const BatchCallbacks &callbacks = {});
}
