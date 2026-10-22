#include "batchworker.h"
#include <utility>
namespace imagebatch {
BatchWorker::BatchWorker(QStringList files, QString directory, Options options,
                         std::shared_ptr<std::atomic_bool> cancelled, BatchSettings settings, QObject *parent)
    : QObject(parent), files_(std::move(files)), directory_(std::move(directory)),
      options_(std::move(options)), settings_(settings), cancelled_(std::move(cancelled)) {}
void BatchWorker::run() {
    BatchCallbacks callbacks;
    callbacks.started = [this](int index) { emit taskStarted(index); };
    callbacks.completed = [this](int done, int total, const FileResult &result) { emit progress(done, total, result); };
    const auto summary = runBatch(files_, directory_, options_, settings_, *cancelled_, callbacks);
    emit metrics(summary.elapsedMs, summary.peakActive, summary.peakReservedBytes, summary.peakQueued, summary.metricsReport);
    emit finished(summary.succeeded, summary.failed, summary.cancelled, summary.report, summary.error);
}
}
