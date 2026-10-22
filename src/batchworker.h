#pragma once
#include "batchengine.h"
#include <QObject>
#include <atomic>
#include <memory>

namespace imagebatch {
class BatchWorker : public QObject {
    Q_OBJECT
public:
    BatchWorker(QStringList files, QString directory, Options options,
                std::shared_ptr<std::atomic_bool> cancelled, BatchSettings settings = {}, QObject *parent = nullptr);
public slots:
    void run();
signals:
    void taskStarted(int index);
    void metrics(qint64 elapsedMs, int peakActive, quint64 reservedBytes, int peakQueued, QString jsonReport);
    void progress(int completed, int total, imagebatch::FileResult result);
    void finished(int succeeded, int failed, int cancelled, QString report, QString reportError);
private:
    QStringList files_;
    QString directory_;
    Options options_;
    BatchSettings settings_;
    std::shared_ptr<std::atomic_bool> cancelled_;
};
}
