#include "mainwindow.h"
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
#include <QDesktopServices>
#include <QUrl>
#include <QBuffer>
#include <QImageWriter>

namespace imagebatch {
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    qRegisterMetaType<FileResult>();
    setWindowTitle(QStringLiteral("ImageBatch · 图片批处理工具"));
    resize(1180, 820);
    setMinimumSize(1000, 760);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);
    auto *title = new QLabel(QStringLiteral("ImageBatch  图片批处理"));
    title->setObjectName("title");
    root->addWidget(title);
    root->addWidget(new QLabel(QStringLiteral("缩放 · 格式转换 · 文字水印    |    本地处理，保留原图")));

    auto *folderBox = new QGroupBox(QStringLiteral("01  导入与保存位置"));
    auto *folders = new QGridLayout(folderBox);
    input_ = new QLineEdit;
    input_->setObjectName("inputPath");
    input_->setPlaceholderText(QStringLiteral("选择或输入图片文件夹路径"));
    output_ = new QLineEdit;
    output_->setObjectName("outputPath");
    output_->setPlaceholderText(QStringLiteral("默认保存到原文件夹下的 processed 子目录"));
    browse_ = new QPushButton(QStringLiteral("选择文件夹"));
    import_ = new QPushButton(QStringLiteral("导入图片"));
    import_->setObjectName("importButton");
    outputBrowse_ = new QPushButton(QStringLiteral("更改位置"));
    folders->addWidget(new QLabel(QStringLiteral("图片来源")), 0, 0);
    folders->addWidget(input_, 0, 1);
    folders->addWidget(browse_, 0, 2);
    folders->addWidget(import_, 0, 3);
    folders->addWidget(new QLabel(QStringLiteral("输出目录")), 1, 0);
    folders->addWidget(output_, 1, 1, 1, 2);
    folders->addWidget(outputBrowse_, 1, 3);
    root->addWidget(folderBox);
    auto *body = new QHBoxLayout;
    body->setSpacing(14);
    auto *listBox = new QGroupBox(QStringLiteral("02  图片列表"));
    auto *listLayout = new QVBoxLayout(listBox);
    count_ = new QLabel(QStringLiteral("尚未导入图片"));
    listLayout->addWidget(count_);
    table_ = new QTableWidget(0, 2);
    table_->setObjectName("fileTable");
    table_->setHorizontalHeaderLabels({QStringLiteral("文件名"), QStringLiteral("状态")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    listLayout->addWidget(table_);
    auto *listHint = new QLabel(QStringLiteral("支持 JPG / JPEG / PNG / BMP\n仅导入当前目录；点击文件查看预览"));
    listHint->setObjectName("hint");
    listLayout->addWidget(listHint);
    body->addWidget(listBox, 4);

    auto *right = new QVBoxLayout;
    right->setSpacing(14);
    auto *optionBox = new QGroupBox(QStringLiteral("03  处理参数"));
    auto *settings = new QGridLayout(optionBox);
    edge_ = new QSpinBox;
    edge_->setObjectName("maxEdge"); edge_->setRange(0, 20000); edge_->setValue(1920);
    edge_->setSpecialValueText(QStringLiteral("保持原尺寸")); edge_->setSuffix(QStringLiteral(" px"));
    format_ = new QComboBox;
    format_->setObjectName("outputFormat");
    format_->addItem("JPG", "jpg"); format_->addItem("PNG", "png"); format_->addItem("BMP", "bmp");
    quality_ = new QSpinBox;
    quality_->setObjectName("jpegQuality"); quality_->setRange(1, 100); quality_->setValue(90);
    watermarkEnabled_ = new QCheckBox(QStringLiteral("启用文字水印"));
    watermarkEnabled_->setObjectName("watermarkEnabled");
    watermark_ = new QLineEdit;
    watermark_->setObjectName("watermarkText"); watermark_->setMaxLength(100);
    watermark_->setPlaceholderText(QStringLiteral("右下角水印，例如 © 我的作品"));
    watermark_->setEnabled(false);
    settings->addWidget(new QLabel(QStringLiteral("最长边")), 0, 0);
    settings->addWidget(edge_, 0, 1);
    settings->addWidget(new QLabel(QStringLiteral("输出格式")), 0, 2);
    settings->addWidget(format_, 0, 3);
    settings->addWidget(new QLabel(QStringLiteral("JPEG 质量")), 1, 0);
    settings->addWidget(quality_, 1, 1);
    settings->addWidget(new QLabel(QStringLiteral("等比例缩小，不放大小图")), 1, 2, 1, 2);
    settings->addWidget(watermarkEnabled_, 2, 0, 1, 2);
    settings->addWidget(watermark_, 2, 2, 1, 2);
    auto *hint = new QLabel(QStringLiteral("JPG / BMP 使用白色背景；PNG 保留透明度。水印自动适应图片宽度。"));
    hint->setWordWrap(true); hint->setObjectName("hint");
    settings->addWidget(hint, 3, 0, 1, 4);
    right->addWidget(optionBox);

    auto *previewBox = new QGroupBox(QStringLiteral("04  效果预览"));
    auto *previews = new QGridLayout(previewBox);
    before_ = new QLabel(QStringLiteral("导入图片后显示原图"));
    after_ = new QLabel(QStringLiteral("调整参数后查看效果"));
    before_->setObjectName("beforePreview"); after_->setObjectName("afterPreview");
    for (auto *label : {before_, after_}) {
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumSize(190, 150);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        label->setStyleSheet("background:#eaf0f6;border:1px solid #d6e1ec;border-radius:8px;color:#61748a;");
    }
    beforeInfo_ = new QLabel(QStringLiteral("原图"));
    afterInfo_ = new QLabel(QStringLiteral("处理效果"));
    beforeInfo_->setAlignment(Qt::AlignCenter); afterInfo_->setAlignment(Qt::AlignCenter);
    beforeInfo_->setFixedHeight(25); afterInfo_->setFixedHeight(25);
    previews->addWidget(before_, 0, 0); previews->addWidget(after_, 0, 1);
    previews->addWidget(beforeInfo_, 1, 0); previews->addWidget(afterInfo_, 1, 1);
    previews->setColumnStretch(0, 1); previews->setColumnStretch(1, 1);
    previews->setRowStretch(0, 1);
    right->addWidget(previewBox, 1);
    body->addLayout(right, 7);
    root->addLayout(body, 1);

    progress_ = new QProgressBar;
    progress_->setObjectName("batchProgress"); progress_->setValue(0);
    root->addWidget(progress_);
    status_ = new QLabel(QStringLiteral("准备就绪。请选择图片文件夹。"));
    status_->setObjectName("statusText"); status_->setWordWrap(true);
    root->addWidget(status_);
    auto *actions = new QHBoxLayout;
    openOutput_ = new QPushButton(QStringLiteral("打开输出目录"));
    openReport_ = new QPushButton(QStringLiteral("查看报告"));
    openReport_->setEnabled(false);
    start_ = new QPushButton(QStringLiteral("开始批量处理"));
    start_->setObjectName("startButton"); start_->setProperty("primary", true); start_->setEnabled(false);
    cancel_ = new QPushButton(QStringLiteral("取消后续任务"));
    cancel_->setObjectName("cancelButton"); cancel_->setEnabled(false);
    actions->addWidget(openOutput_); actions->addWidget(openReport_); actions->addStretch();
    actions->addWidget(cancel_); actions->addWidget(start_);
    root->addLayout(actions);
    setStyleSheet(QStringLiteral(
        "QMainWindow{background:#f4f7fb;} QWidget{font-family:'Microsoft YaHei UI','Segoe UI';font-size:13px;color:#20334c;}"
        "QLabel#title{font-size:26px;font-weight:700;color:#153453;} QLabel#hint{font-size:12px;color:#62758b;}"
        "QGroupBox{background:white;border:1px solid #dce4ee;border-radius:10px;margin-top:13px;padding:13px 10px 10px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:15px;padding:0 5px;font-weight:600;}"
        "QLineEdit,QSpinBox,QComboBox{background:white;border:1px solid #cdd9e5;border-radius:5px;padding:6px;min-height:20px;}"
        "QLineEdit:focus,QSpinBox:focus{border:1px solid #16857d;}"
        "QPushButton{background:#eef3f8;border:1px solid #cfdae6;border-radius:6px;padding:8px 15px;}"
        "QPushButton:hover{background:#dfebf5;} QPushButton[primary=true]{background:#087f78;color:white;border-color:#087f78;font-weight:600;}"
        "QPushButton[primary=true]:hover{background:#06665f;} QPushButton:disabled{color:#96a4b4;background:#edf0f4;border-color:#e1e6ec;}"
        "QTableWidget{border:1px solid #e0e7ef;background:white;alternate-background-color:#f6f9fc;gridline-color:#edf1f5;}"
        "QTableWidget::item{padding:6px;} QTableWidget::item:selected{background:#dcefeb;color:#164b44;}"
        "QHeaderView::section{background:#edf3f8;border:0;padding:8px;color:#50657d;}"
        "QProgressBar{border:0;border-radius:5px;background:#dfe7ef;text-align:center;height:18px;}"
        "QProgressBar::chunk{background:#15968a;border-radius:5px;}"));

    connect(browse_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择图片文件夹"), input_->text());
        if (!path.isEmpty()) { input_->setText(path); importFolder(); }
    });
    connect(import_, &QPushButton::clicked, this, &MainWindow::importFolder);
    connect(input_, &QLineEdit::returnPressed, this, &MainWindow::importFolder);
    connect(input_, &QLineEdit::textEdited, this, [this] {
        start_->setEnabled(false);
        status_->setText(QStringLiteral("来源路径已修改，请点击“导入图片”刷新列表。"));
    });
    connect(outputBrowse_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出文件夹"), output_->text());
        if (!path.isEmpty()) output_->setText(path);
    });
    connect(table_, &QTableWidget::currentCellChanged, this, [this] { schedulePreview(); });
    connect(edge_, &QSpinBox::valueChanged, this, [this] { schedulePreview(); });
    connect(quality_, &QSpinBox::valueChanged, this, [this] { schedulePreview(); });
    connect(format_, &QComboBox::currentIndexChanged, this, [this] {
        quality_->setEnabled(!busy() && format_->currentData().toString() == "jpg"); schedulePreview();
    });
    connect(watermarkEnabled_, &QCheckBox::toggled, this, [this](bool checked) {
        watermark_->setEnabled(checked && !busy()); schedulePreview();
    });
    connect(watermark_, &QLineEdit::textChanged, this, [this] { schedulePreview(); });
    connect(start_, &QPushButton::clicked, this, &MainWindow::startBatch);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (cancelled_) cancelled_->store(true);
        cancel_->setEnabled(false);
        status_->setText(QStringLiteral("正在取消：完成当前图片后停止，已生成的文件会保留。"));
    });
    connect(openOutput_, &QPushButton::clicked, this, [this] {
        if (!QDir(output_->text()).exists() || output_->text().isEmpty()) {
            status_->setText(QStringLiteral("输出目录尚未创建，请先完成处理。")); return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(output_->text()));
    });
    connect(openReport_, &QPushButton::clicked, this, [this] {
        if (!report_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(report_));
    });
    previewTimer_.setSingleShot(true); previewTimer_.setInterval(180);
    connect(&previewTimer_, &QTimer::timeout, this, &MainWindow::beginPreview);
    connect(&previewWatcher_, &QFutureWatcher<Preview>::finished, this, [this] {
        if (runningPreviewVersion_ != previewVersion_) { previewTimer_.start(); return; }
        const auto p = previewWatcher_.result();
        if (!p.error.isEmpty()) {
            previewBefore_ = {}; previewAfter_ = {};
            before_->setText(QStringLiteral("无法读取图片")); after_->setText(p.error);
            beforeInfo_->setText(QStringLiteral("原图")); afterInfo_->setText(QStringLiteral("预览失败"));
        } else {
            previewBefore_ = p.before; previewAfter_ = p.after;
            renderPreviews();
            beforeInfo_->setText(QStringLiteral("原图  %1 × %2").arg(p.inputSize.width()).arg(p.inputSize.height()));
            afterInfo_->setText(QStringLiteral("输出  %1 × %2  ·  %3").arg(p.outputSize.width()).arg(p.outputSize.height()).arg(format_->currentText()));
        }
        emit previewReady();
    });
}
MainWindow::~MainWindow() {
    if (cancelled_) cancelled_->store(true);
    if (thread_) { thread_->quit(); thread_->wait(); }
    previewWatcher_.waitForFinished();
}
Options MainWindow::options() const {
    Options o;
    o.maxEdge = edge_->value(); o.format = format_->currentData().toString();
    o.jpegQuality = quality_->value();
    if (watermarkEnabled_->isChecked()) o.watermark = watermark_->text();
    return o;
}
void MainWindow::importFolder() {
    if (busy()) return;
    QString error;
    files_ = scanImages(input_->text(), &error);
    ++previewVersion_;
    table_->setRowCount(0);
    previewBefore_ = {}; previewAfter_ = {};
    before_->setText(QStringLiteral("请选择图片")); after_->setText(QStringLiteral("暂无预览"));
    beforeInfo_->setText(QStringLiteral("原图")); afterInfo_->setText(QStringLiteral("处理效果"));
    for (const auto &file : files_) {
        const int row = table_->rowCount(); table_->insertRow(row);
        auto *item = new QTableWidgetItem(QFileInfo(file).fileName());
        item->setToolTip(file); item->setData(Qt::UserRole, file);
        table_->setItem(row, 0, item);
        table_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("待处理")));
    }
    count_->setText(QStringLiteral("共 %1 张图片").arg(files_.size()));
    start_->setEnabled(!files_.isEmpty());
    progress_->setValue(0);
    report_.clear(); openReport_->setEnabled(false);
    if (error.isEmpty()) {
        output_->setText(QDir(input_->text()).absoluteFilePath("processed"));
        status_->setText(files_.isEmpty() ? QStringLiteral("该文件夹没有支持的图片。")
                                         : QStringLiteral("导入完成。调整参数后可开始批量处理。"));
    } else status_->setText(error);
    if (!files_.isEmpty()) table_->selectRow(0);
}
void MainWindow::schedulePreview() {
    ++previewVersion_;
    if (!busy()) previewTimer_.start();
}
void MainWindow::beginPreview() {
    if (busy() || previewWatcher_.isRunning()) return;
    const int row = table_->currentRow();
    if (row < 0 || row >= files_.size()) return;
    const QString file = files_[row]; const Options o = options();
    runningPreviewVersion_ = previewVersion_;
    previewWatcher_.setFuture(QtConcurrent::run([file, o] {
        Preview p;
        QImage original = loadImage(file, &p.error);
        if (original.isNull()) return p;
        QImage transformed = transformImage(original, o);
        if (transformed.isNull()) { p.error = QStringLiteral("无法生成预览"); return p; }
        if (o.format == "jpg") {
            QByteArray encoded;
            QBuffer buffer(&encoded); buffer.open(QIODevice::WriteOnly);
            QImageWriter writer(&buffer, "jpg"); writer.setQuality(o.jpegQuality);
            if (!writer.write(transformed)) { p.error = writer.errorString(); return p; }
            transformed = QImage::fromData(encoded, "jpg");
        }
        p.inputSize = original.size(); p.outputSize = transformed.size();
        p.before = original.scaled(800, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.after = transformed.scaled(800, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return p;
    }));
}
void MainWindow::renderPreviews() {
    if (!previewBefore_.isNull())
        before_->setPixmap(QPixmap::fromImage(previewBefore_).scaled(before_->size() - QSize(12, 12), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (!previewAfter_.isNull())
        after_->setPixmap(QPixmap::fromImage(previewAfter_).scaled(after_->size() - QSize(12, 12), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    // Construction calls resize before the preview widgets exist.
    if (!previewBefore_.isNull()) QTimer::singleShot(0, this, [this] { renderPreviews(); });
}
void MainWindow::setBusy(bool working) {
    for (QWidget *w : QList<QWidget *>{input_, output_, edge_, format_, watermarkEnabled_, import_, browse_, outputBrowse_})
        w->setEnabled(!working);
    quality_->setEnabled(!working && format_->currentData().toString() == "jpg");
    watermark_->setEnabled(!working && watermarkEnabled_->isChecked());
    start_->setEnabled(!working && !files_.isEmpty()); cancel_->setEnabled(working);
    openOutput_->setEnabled(!working); openReport_->setEnabled(!working && !report_.isEmpty());
}
void MainWindow::startBatch() {
    if (busy() || files_.isEmpty()) return;
    const QString out = output_->text().trimmed();
    if (out.isEmpty()) { status_->setText(QStringLiteral("请选择输出文件夹。")); return; }
    const QString inputCanonical = QFileInfo(input_->text()).canonicalFilePath();
    const QString outputCanonical = QFileInfo(out).canonicalFilePath();
    if ((!inputCanonical.isEmpty() && inputCanonical == outputCanonical) ||
        QDir(input_->text()).absolutePath() == QDir(out).absolutePath()) {
        status_->setText(QStringLiteral("输出文件夹必须与原图文件夹不同。")); return;
    }
    if (!QDir().mkpath(out)) { status_->setText(QStringLiteral("无法创建输出文件夹，请检查路径和权限。")); return; }
    const auto o = options();
    const auto error = validateOptions(o);
    if (!error.isEmpty()) { status_->setText(error); return; }
    report_.clear();
    progress_->setRange(0, files_.size()); progress_->setValue(0);
    for (int row = 0; row < table_->rowCount(); ++row) {
        table_->item(row, 1)->setText(QStringLiteral("待处理"));
        table_->item(row, 1)->setToolTip({});
    }
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    thread_ = new QThread(this);
    auto *worker = new BatchWorker(files_, out, o, cancelled_);
    worker->moveToThread(thread_);
    connect(thread_, &QThread::started, worker, &BatchWorker::run);
    connect(worker, &BatchWorker::progress, this, &MainWindow::updateResult);
    connect(worker, &BatchWorker::finished, this,
            [this](int good, int bad, int cancelled, const QString &report, const QString &reportError) {
        report_ = report;
        status_->setText(QStringLiteral("处理结束：成功 %1，失败 %2，取消 %3。%4")
            .arg(good).arg(bad).arg(cancelled)
            .arg(reportError.isEmpty() ? QStringLiteral("CSV 报告已保存到输出目录。")
                                      : QStringLiteral("报告保存失败：") + reportError));
    });
    connect(worker, &BatchWorker::finished, thread_, &QThread::quit);
    connect(worker, &BatchWorker::finished, worker, &QObject::deleteLater);
    connect(thread_, &QThread::finished, this, [this] {
        thread_->deleteLater(); thread_ = nullptr;
        setBusy(false); emit batchComplete();
    });
    setBusy(true);
    status_->setText(QStringLiteral("正在后台处理图片；可以取消后续任务。"));
    thread_->start();
}
void MainWindow::updateResult(int done, int total, const FileResult &r) {
    progress_->setMaximum(total); progress_->setValue(done);
    const int row = files_.indexOf(r.inputPath);
    if (row < 0) return;
    auto *item = table_->item(row, 1);
    item->setText(r.status == "success" ? QStringLiteral("成功") : r.status == "cancelled" ? QStringLiteral("取消") : QStringLiteral("失败"));
    item->setToolTip(r.error.isEmpty() ? r.outputPath : r.error);
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (busy()) {
        if (cancelled_) cancelled_->store(true);
        status_->setText(QStringLiteral("正在停止任务并保存报告，请在任务结束后关闭窗口。"));
        cancel_->setEnabled(false); event->ignore(); return;
    }
    QMainWindow::closeEvent(event);
}
}
