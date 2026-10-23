#pragma once
#include "batchworker.h"
#include <QMainWindow>
#include <QFutureWatcher>
#include <QTimer>
#include <QSet>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QThread;
class QPlainTextEdit;
namespace imagebatch {
struct Preview { QImage before, after; QSize inputSize, outputSize; QString error, format; };
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, const QString &settingsFile = {});
    ~MainWindow() override;
    bool busy() const { return thread_ != nullptr; }
    QString lastReport() const { return report_; }
    void addPaths(const QStringList &paths); // Also used by file dialog and native drop events.
signals:
    void previewReady();
    void batchComplete();
protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    Options options() const;
    BatchSettings batchSettings() const;
    void importFolder();
    void schedulePreview();
    void beginPreview();
    void renderPreviews();
    void startBatch(bool retry = false);
    void refreshSelection();
    void showDetails();
    void clearFiles();
    void appendFiles(const QStringList &files);
    void restorePreferences();
    void savePreferences();
    void setBusy(bool busy);
    void updateResult(int completed, int total, const FileResult &result);
    QLineEdit *input_, *output_, *watermark_;
    QSpinBox *edge_, *quality_, *workers_, *budget_;
    QComboBox *format_, *watermarkPosition_;
    QCheckBox *watermarkEnabled_;
    QTableWidget *table_;
    QLabel *before_, *after_, *beforeInfo_, *afterInfo_, *status_, *count_;
    QLabel *metrics_;
    QPlainTextEdit *details_;
    QPushButton *start_, *cancel_, *import_, *browse_, *outputBrowse_, *openOutput_, *openReport_;
    QPushButton *addFiles_, *selectAll_, *selectNone_, *remove_, *clear_, *retry_;
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
    QSet<int> activeTasks_;
    QList<int> batchRows_;
    QList<FileResult> results_;
    QString settingsFile_;
    bool folderDirty_ = false, closeAfterBatch_ = false, updatingTable_ = false;
};
}
