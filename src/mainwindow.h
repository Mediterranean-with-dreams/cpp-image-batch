#pragma once
#include "batchworker.h"
#include <QMainWindow>
#include <QFutureWatcher>
#include <QTimer>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QThread;
namespace imagebatch {
struct Preview { QImage before, after; QSize inputSize, outputSize; QString error; };
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool busy() const { return thread_ != nullptr; }
    QString lastReport() const { return report_; }
signals:
    void previewReady();
    void batchComplete();
protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    Options options() const;
    void importFolder();
    void schedulePreview();
    void beginPreview();
    void renderPreviews();
    void startBatch();
    void setBusy(bool busy);
    void updateResult(int completed, int total, const FileResult &result);
    QLineEdit *input_, *output_, *watermark_;
    QSpinBox *edge_, *quality_;
    QComboBox *format_;
    QCheckBox *watermarkEnabled_;
    QTableWidget *table_;
    QLabel *before_, *after_, *beforeInfo_, *afterInfo_, *status_, *count_;
    QPushButton *start_, *cancel_, *import_, *browse_, *outputBrowse_, *openOutput_, *openReport_;
    QProgressBar *progress_;
    QStringList files_;
    QString report_;
    QThread *thread_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancelled_;
    QTimer previewTimer_;
    QFutureWatcher<Preview> previewWatcher_;
    int previewVersion_ = 0;
    int runningPreviewVersion_ = 0;
    QImage previewBefore_, previewAfter_;
};
}
