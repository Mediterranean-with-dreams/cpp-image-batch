#include "batchworker.h"
#include <utility>
namespace imagebatch {
BatchWorker::BatchWorker(QStringList files, QString directory, Options options,
                         std::shared_ptr<std::atomic_bool> cancelled, QObject *parent)
    : QObject(parent), files_(std::move(files)), directory_(std::move(directory)),
      options_(std::move(options)), cancelled_(std::move(cancelled)) {}
void BatchWorker::run() {
    QList<FileResult> results;
    int succeeded = 0, failed = 0, cancelled = 0;
    for (const auto &file : files_) {
        FileResult r;
        if (cancelled_->load()) {
            r.inputPath = file;
            r.status = "cancelled";
            r.error = QStringLiteral("用户取消，未处理");
            ++cancelled;
        } else {
            r = processFile(file, directory_, options_);
            if (r.status == "success") ++succeeded; else ++failed;
        }
        results.append(r);
        emit progress(results.size(), files_.size(), r);
    }
    QString error;
    const QString report = writeReport(results, directory_, &error);
    emit finished(succeeded, failed, cancelled, report, error);
}
}
