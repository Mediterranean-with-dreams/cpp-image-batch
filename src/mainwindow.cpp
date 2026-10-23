#include "mainwindow.h"
#include "preferences.h"
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
#include <QDesktopServices>
#include <QUrl>
#include <QBuffer>
#include <QImageWriter>
#include <exception>

namespace imagebatch {
static QString sizeLabel(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KiB";
    return QString::number(bytes / 1048576.0, 'f', 2) + " MiB";
}
static QString colorLabel(ColorMode mode) {
    if (mode == ColorMode::Grayscale) return QStringLiteral("单通道灰度");
    if (mode == ColorMode::GrayscaleRgb) return QStringLiteral("RGB 三通道灰度");
    return QStringLiteral("保留彩色");
}
MainWindow::MainWindow(QWidget *parent, const QString &settingsFile) : QMainWindow(parent), settingsFile_(settingsFile) {
    qRegisterMetaType<FileResult>();
    setWindowTitle(QStringLiteral("ImageBatch %1 · 图片批处理工具").arg(IMAGEBATCH_VERSION));
    resize(1180, 820);
    setMinimumSize(860, 520);
    setAcceptDrops(true);
    if (settingsFile_.isEmpty() && !qEnvironmentVariableIsSet("IMAGEBATCH_NO_SETTINGS") &&
        !QCoreApplication::arguments().contains("--self-test"))
        settingsFile_ = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.ini";
    auto *scroll = new QScrollArea;
    scroll->setObjectName("contentScroll"); scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *central = new QWidget;
    central->setObjectName("contentPanel"); central->setMinimumSize(830, 680);
    scroll->setWidget(central);
    auto *shell = new QWidget;
    auto *outer = new QVBoxLayout(shell); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    outer->addWidget(scroll, 1); setCentralWidget(shell);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("ImageBatch  图片批处理"));
    title->setObjectName("title");
    root->addWidget(title);
    root->addWidget(new QLabel(QStringLiteral("缩放 · 灰度 · 图片压缩 · 文字水印    |    本地处理，保留原图")));

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
    addFiles_ = new QPushButton(QStringLiteral("选择图片…"));
    addFiles_->setObjectName("addFilesButton");
    folders->addWidget(new QLabel(QStringLiteral("图片来源")), 0, 0);
    folders->addWidget(input_, 0, 1);
    folders->addWidget(browse_, 0, 2);
    folders->addWidget(import_, 0, 3);
    folders->addWidget(new QLabel(QStringLiteral("输出目录")), 1, 0);
    folders->addWidget(output_, 1, 1, 1, 2);
    folders->addWidget(outputBrowse_, 1, 3);
    auto *importHint = new QLabel(QStringLiteral("可多选图片，也可拖入图片或文件夹。导入文件夹替换列表，添加图片追加到列表。"));
    importHint->setWordWrap(true); importHint->setObjectName("hint");
    folders->addWidget(importHint, 2, 0, 1, 3); folders->addWidget(addFiles_, 2, 3);
    root->addWidget(folderBox);
    auto *body = new QHBoxLayout;
    body->setSpacing(14);
    auto *listBox = new QGroupBox(QStringLiteral("02  图片列表"));
    auto *listLayout = new QVBoxLayout(listBox);
    count_ = new QLabel(QStringLiteral("尚未导入图片"));
    listLayout->addWidget(count_);
    auto *selection = new QHBoxLayout;
    selectAll_ = new QPushButton(QStringLiteral("全选")); selectAll_->setObjectName("selectAllButton");
    selectNone_ = new QPushButton(QStringLiteral("全不选")); selectNone_->setObjectName("selectNoneButton");
    remove_ = new QPushButton(QStringLiteral("移除勾选")); remove_->setObjectName("removeButton");
    clear_ = new QPushButton(QStringLiteral("清空")); clear_->setObjectName("clearButton");
    for (auto *button : {selectAll_, selectNone_, remove_, clear_}) {
        button->setStyleSheet("padding:5px 7px;"); selection->addWidget(button);
    }
    listLayout->addLayout(selection);
    table_ = new QTableWidget(0, 3);
    table_->setObjectName("fileTable");
    table_->setHorizontalHeaderLabels({QStringLiteral("文件名"), QStringLiteral("状态"), QStringLiteral("耗时 ms")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    listLayout->addWidget(table_);
    details_ = new QPlainTextEdit;
    details_->setObjectName("fileDetails"); details_->setReadOnly(true);
    details_->setPlainText(QStringLiteral("点击图片查看路径和处理详情。"));
    details_->setFixedHeight(80);
    listLayout->addWidget(details_);
    auto *listHint = new QLabel(QStringLiteral("支持 JPG / JPEG / PNG / BMP\n仅扫描当前目录；仅处理已勾选的图片"));
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
    format_->addItem(QStringLiteral("保持原格式"), "original");
    quality_ = new QSpinBox;
    quality_->setObjectName("jpegQuality"); quality_->setRange(1, 100); quality_->setValue(90);
    edge_->setToolTip(QStringLiteral("宽和高中较大的值。4000×3000 设为 1920 后输出 1920×1440；0 保持尺寸，小图不放大。"));
    quality_->setToolTip(QStringLiteral("JPG 有损编码质量，1～100，不是画质百分比。越高通常细节越好、文件越大；对 PNG / BMP 不生效。"));
    colorMode_ = new QComboBox; colorMode_->setObjectName("colorMode");
    colorMode_->addItem(QStringLiteral("保留彩色"), 0);
    colorMode_->addItem(QStringLiteral("灰度 · 单通道（建议 PNG）"), 1);
    colorMode_->addItem(QStringLiteral("灰度 · RGB 三通道"), 2);
    targetSize_ = new QSpinBox; targetSize_->setObjectName("jpegTargetSize"); targetSize_->setRange(0, 102400);
    targetSize_->setSpecialValueText(QStringLiteral("不限制")); targetSize_->setSuffix(" KiB"); targetSize_->setSingleStep(50);
    pngCompression_ = new QComboBox; pngCompression_->setObjectName("pngCompression");
    pngCompression_->addItem(QStringLiteral("快速 · 无损"), 0);
    pngCompression_->addItem(QStringLiteral("均衡 · 无损"), 50);
    pngCompression_->addItem(QStringLiteral("更小 · 无损"), 100); pngCompression_->setCurrentIndex(1);
    watermarkEnabled_ = new QCheckBox(QStringLiteral("启用文字水印"));
    watermarkEnabled_->setObjectName("watermarkEnabled");
    watermark_ = new QLineEdit;
    watermark_->setObjectName("watermarkText"); watermark_->setMaxLength(100);
    watermark_->setPlaceholderText(QStringLiteral("水印文字，例如 © 我的作品"));
    watermark_->setEnabled(false);
    watermarkPosition_ = new QComboBox;
    watermarkPosition_->setObjectName("watermarkPosition");
    watermarkPosition_->addItems({QStringLiteral("右下角"), QStringLiteral("左下角"), QStringLiteral("右上角"), QStringLiteral("左上角")});
    watermarkPosition_->setEnabled(false);
    workers_ = new QSpinBox;
    workers_->setObjectName("workerCount"); workers_->setRange(1, 8); workers_->setValue(2);
    budget_ = new QSpinBox;
    budget_->setObjectName("memoryBudget"); budget_->setRange(64, 4096); budget_->setValue(512);
    budget_->setSingleStep(64); budget_->setSuffix(" MiB");
    budget_->setToolTip(QStringLiteral("按图片尺寸估算工作缓冲，限制同时解码的任务；不代表整个程序的内存硬上限。"));
    settings->addWidget(new QLabel(QStringLiteral("最长边")), 0, 0);
    settings->addWidget(edge_, 0, 1);
    settings->addWidget(new QLabel(QStringLiteral("输出格式")), 0, 2);
    settings->addWidget(format_, 0, 3);
    settings->addWidget(new QLabel(QStringLiteral("JPEG 质量")), 1, 0);
    settings->addWidget(quality_, 1, 1);
    settings->addWidget(new QLabel(QStringLiteral("JPG 有损压缩质量")), 1, 2, 1, 2);
    auto *parameterHelp = new QLabel(QStringLiteral(
        "最长边＝宽和高中较大的值。例：4000×3000 设 1920 → 1920×1440；0 保持尺寸，小图不放大。\n"
        "JPEG 质量为 1～100，不是画质百分比；越高通常越清晰、文件越大。仅对 JPG 生效。"));
    parameterHelp->setObjectName("parameterHelp"); parameterHelp->setWordWrap(true);
    parameterHelp->setStyleSheet("font-size:12px;color:#50657d;");
    settings->addWidget(parameterHelp, 2, 0, 1, 4);
    settings->addWidget(new QLabel(QStringLiteral("颜色处理")), 3, 0); settings->addWidget(colorMode_, 3, 1, 1, 3);
    auto *grayHelp = new QLabel(QStringLiteral("灰度会将透明区转白。训练建议用 PNG 并关闭水印，通道数应与模型输入要求一致。"));
    grayHelp->setObjectName("grayscaleHelp"); grayHelp->setWordWrap(true); grayHelp->setStyleSheet("font-size:12px;color:#50657d;");
    settings->addWidget(grayHelp, 4, 0, 1, 4);
    settings->addWidget(new QLabel(QStringLiteral("JPG 大小上限")), 5, 0); settings->addWidget(targetSize_, 5, 1);
    settings->addWidget(new QLabel(QStringLiteral("PNG 压缩")), 5, 2); settings->addWidget(pngCompression_, 5, 3);
    compressionHelp_ = new QLabel; compressionHelp_->setObjectName("compressionHelp"); compressionHelp_->setWordWrap(true);
    compressionHelp_->setStyleSheet("font-size:12px;color:#50657d;"); settings->addWidget(compressionHelp_, 6, 0, 1, 4);
    settings->addWidget(watermarkEnabled_, 7, 0); settings->addWidget(watermarkPosition_, 7, 1);
    settings->addWidget(watermark_, 7, 2, 1, 2);
    settings->addWidget(new QLabel(QStringLiteral("并行任务")), 8, 0); settings->addWidget(workers_, 8, 1);
    settings->addWidget(new QLabel(QStringLiteral("批处理预算")), 8, 2); settings->addWidget(budget_, 8, 3);
    auto *hint = new QLabel(QStringLiteral("保留彩色时 PNG 保留透明度；JPG / BMP 使用白底。预算用于估算批处理缓冲。"));
    hint->setWordWrap(true); hint->setObjectName("hint");
    settings->addWidget(hint, 9, 0, 1, 4);
    auto *reset = new QPushButton(QStringLiteral("恢复默认参数")); reset->setObjectName("resetOptionsButton");
    auto *presets = new QHBoxLayout;
    auto *training = new QPushButton(QStringLiteral("训练灰度 PNG")); training->setObjectName("trainingPresetButton");
    auto *photo = new QPushButton(QStringLiteral("照片压缩")); photo->setObjectName("photoPresetButton");
    presets->addWidget(training); presets->addWidget(photo); presets->addWidget(reset);
    settings->addLayout(presets, 10, 0, 1, 4);
    connect(training, &QPushButton::clicked, this, [this] {
        format_->setCurrentIndex(1); colorMode_->setCurrentIndex(1); edge_->setValue(0);
        targetSize_->setValue(0); pngCompression_->setCurrentIndex(1); watermarkEnabled_->setChecked(false);
        status_->setText(QStringLiteral("已应用训练灰度 PNG：单通道、保持尺寸、关闭水印。请确认模型需要的通道数。"));
    });
    connect(photo, &QPushButton::clicked, this, [this] {
        format_->setCurrentIndex(0); colorMode_->setCurrentIndex(0); edge_->setValue(1920);
        quality_->setValue(85); targetSize_->setValue(0); watermarkEnabled_->setChecked(false);
        status_->setText(QStringLiteral("已应用照片压缩：JPG、最长边 1920、质量 85、关闭水印。可继续设置大小上限。"));
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        edge_->setValue(1920); quality_->setValue(90); format_->setCurrentIndex(0);
        workers_->setValue(2); budget_->setValue(512); watermarkEnabled_->setChecked(false);
        watermark_->clear(); watermarkPosition_->setCurrentIndex(0);
        colorMode_->setCurrentIndex(0); targetSize_->setValue(0); pngCompression_->setCurrentIndex(1);
    });
    right->addWidget(optionBox);

    auto *previewBox = new QGroupBox(QStringLiteral("04  效果预览"));
    auto *previews = new QGridLayout(previewBox);
    before_ = new QLabel(QStringLiteral("导入图片后显示原图"));
    after_ = new QLabel(QStringLiteral("调整参数后查看效果"));
    before_->setObjectName("beforePreview"); after_->setObjectName("afterPreview");
    for (auto *label : {before_, after_}) {
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumSize(190, 130);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        label->setStyleSheet("background:#eaf0f6;border:1px solid #d6e1ec;border-radius:8px;color:#61748a;");
    }
    beforeInfo_ = new QLabel(QStringLiteral("原图"));
    afterInfo_ = new QLabel(QStringLiteral("处理效果"));
    beforeInfo_->setObjectName("beforeInfo"); afterInfo_->setObjectName("afterInfo");
    beforeInfo_->setAlignment(Qt::AlignCenter); afterInfo_->setAlignment(Qt::AlignCenter);
    beforeInfo_->setFixedHeight(44); afterInfo_->setFixedHeight(44);
    previews->addWidget(before_, 0, 0); previews->addWidget(after_, 0, 1);
    previews->addWidget(beforeInfo_, 1, 0); previews->addWidget(afterInfo_, 1, 1);
    previews->setColumnStretch(0, 1); previews->setColumnStretch(1, 1);
    previews->setRowStretch(0, 1);
    right->addWidget(previewBox, 1);
    body->addLayout(right, 7);
    root->addLayout(body, 1);

    auto *footer = new QWidget; footer->setObjectName("footerPanel");
    auto *footerLayout = new QVBoxLayout(footer); footerLayout->setContentsMargins(20, 6, 20, 12);
    outer->addWidget(footer);
    progress_ = new QProgressBar;
    progress_->setObjectName("batchProgress"); progress_->setValue(0);
    footerLayout->addWidget(progress_);
    status_ = new QLabel(QStringLiteral("准备就绪。请选择图片文件夹。"));
    status_->setObjectName("statusText"); status_->setWordWrap(true);
    footerLayout->addWidget(status_);
    metrics_ = new QLabel(QStringLiteral("可设置 1～8 个并行任务；未开始处理。"));
    metrics_->setObjectName("metricsText"); metrics_->setWordWrap(true);
    footerLayout->addWidget(metrics_);
    auto *actions = new QHBoxLayout;
    openOutput_ = new QPushButton(QStringLiteral("打开输出目录"));
    openReport_ = new QPushButton(QStringLiteral("查看报告"));
    openReport_->setEnabled(false);
    start_ = new QPushButton(QStringLiteral("处理勾选图片"));
    start_->setObjectName("startButton"); start_->setProperty("primary", true); start_->setEnabled(false);
    cancel_ = new QPushButton(QStringLiteral("取消后续任务"));
    cancel_->setObjectName("cancelButton"); cancel_->setEnabled(false);
    retry_ = new QPushButton(QStringLiteral("重试失败/取消")); retry_->setObjectName("retryButton"); retry_->setEnabled(false);
    actions->addWidget(openOutput_); actions->addWidget(openReport_); actions->addStretch();
    actions->addWidget(retry_); actions->addWidget(cancel_); actions->addWidget(start_);
    footerLayout->addLayout(actions);
    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget#contentPanel,QWidget#footerPanel{background:#f4f7fb;} QWidget{font-family:'Microsoft YaHei UI','Segoe UI';font-size:13px;color:#20334c;}"
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
        folderDirty_ = true;
        refreshSelection();
        status_->setText(QStringLiteral("来源路径已修改，请点击“导入图片”刷新列表。"));
    });
    connect(addFiles_, &QPushButton::clicked, this, [this] {
        const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("选择图片（可多选）"), input_->text(),
            QStringLiteral("图片 (*.jpg *.jpeg *.png *.bmp *.JPG *.JPEG *.PNG *.BMP)"));
        if (!paths.isEmpty()) addPaths(paths);
    });
    connect(selectAll_, &QPushButton::clicked, this, [this] {
        updatingTable_ = true;
        for (int row = 0; row < table_->rowCount(); ++row) table_->item(row, 0)->setCheckState(Qt::Checked);
        updatingTable_ = false; refreshSelection();
    });
    connect(selectNone_, &QPushButton::clicked, this, [this] {
        updatingTable_ = true;
        for (int row = 0; row < table_->rowCount(); ++row) table_->item(row, 0)->setCheckState(Qt::Unchecked);
        updatingTable_ = false; refreshSelection();
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        updatingTable_ = true;
        for (int row = table_->rowCount() - 1; row >= 0; --row) if (table_->item(row, 0)->checkState() == Qt::Checked) {
            files_.removeAt(row); results_.removeAt(row); table_->removeRow(row);
        }
        updatingTable_ = false; ++previewVersion_;
        if (files_.isEmpty()) clearFiles(); else { refreshSelection(); showDetails(); schedulePreview(); }
    });
    connect(clear_, &QPushButton::clicked, this, &MainWindow::clearFiles);
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!updatingTable_ && item->column() == 0) refreshSelection();
    });
    connect(retry_, &QPushButton::clicked, this, [this] { startBatch(true); });
    connect(outputBrowse_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出文件夹"), output_->text());
        if (!path.isEmpty()) output_->setText(path);
    });
    connect(table_, &QTableWidget::currentCellChanged, this, [this] { showDetails(); schedulePreview(); });
    connect(edge_, &QSpinBox::valueChanged, this, [this] { schedulePreview(); });
    connect(quality_, &QSpinBox::valueChanged, this, [this] { schedulePreview(); });
    connect(targetSize_, &QSpinBox::valueChanged, this, [this] { schedulePreview(); });
    connect(pngCompression_, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    connect(colorMode_, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    connect(format_, &QComboBox::currentIndexChanged, this, [this] {
        updateFormatControls(); schedulePreview();
    });
    connect(watermarkEnabled_, &QCheckBox::toggled, this, [this](bool checked) {
        watermark_->setEnabled(checked && !busy()); schedulePreview();
        watermarkPosition_->setEnabled(checked && !busy());
    });
    connect(watermarkPosition_, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    connect(watermark_, &QLineEdit::textChanged, this, [this] { schedulePreview(); });
    connect(start_, &QPushButton::clicked, this, [this] { startBatch(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (cancelled_) cancelled_->store(true);
        cancel_->setEnabled(false);
        status_->setText(QStringLiteral("正在取消：完成已开始处理的图片后停止，已生成文件保留。"));
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
        Preview p;
        try { p = previewWatcher_.result(); }
        catch (const std::exception &e) { p.error = QStringLiteral("预览异常：") + QString::fromUtf8(e.what()); }
        catch (...) { p.error = QStringLiteral("预览异常，请尝试缩小图片或调整参数。"); }
        if (!p.error.isEmpty()) {
            previewBefore_ = p.before; previewAfter_ = {};
            if (previewBefore_.isNull()) before_->setText(QStringLiteral("无法读取图片")); else renderPreviews();
            after_->setText(p.error); after_->setWordWrap(true);
            beforeInfo_->setText(p.inputSize.isValid() ? QStringLiteral("原图 %1 × %2\n%3").arg(p.inputSize.width()).arg(p.inputSize.height()).arg(sizeLabel(p.inputBytes)) : QStringLiteral("原图"));
            afterInfo_->setText(QStringLiteral("预览失败，请调整参数"));
            afterInfo_->setToolTip({});
        } else {
            previewBefore_ = p.before; previewAfter_ = p.after;
            renderPreviews();
            beforeInfo_->setText(QStringLiteral("原图 %1 × %2\n%3").arg(p.inputSize.width()).arg(p.inputSize.height()).arg(sizeLabel(p.inputBytes)));
            afterInfo_->setText(QStringLiteral("输出 %1 × %2 · %3\n预计 %4%5")
                .arg(p.outputSize.width()).arg(p.outputSize.height()).arg(p.format.toUpper()).arg(sizeLabel(p.outputBytes))
                .arg(p.jpegQualityUsed > 0 ? QStringLiteral(" · 质量 %1").arg(p.jpegQualityUsed) : QString()));
            afterInfo_->setToolTip(QStringLiteral("预计编码后：%1 字节%2").arg(p.outputBytes)
                .arg(p.jpegQualityUsed > 0 ? QStringLiteral("；JPEG 实际质量：%1").arg(p.jpegQualityUsed) : QString()));
        }
        emit previewReady();
    });
    restorePreferences();
    updateFormatControls();
    refreshSelection();
    // File drops belong to the importer, including drops over editable fields.
    for (auto *widget : findChildren<QWidget *>()) widget->setAcceptDrops(false);
    const auto available = QGuiApplication::primaryScreen()->availableGeometry().size();
    resize(qMin(1180, available.width()), qMin(900, available.height()));
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
    o.jpegTargetKiB = targetSize_->value(); o.pngCompression = pngCompression_->currentData().toInt();
    o.colorMode = static_cast<ColorMode>(colorMode_->currentIndex());
    if (watermarkEnabled_->isChecked()) o.watermark = watermark_->text();
    o.watermarkPosition = static_cast<WatermarkPosition>(watermarkPosition_->currentIndex());
    return o;
}
BatchSettings MainWindow::batchSettings() const {
    BatchSettings settings;
    settings.workers = workers_->value();
    settings.queueCapacity = settings.workers * 2;
    settings.memoryBudgetBytes = static_cast<quint64>(budget_->value()) * 1024 * 1024;
    return settings;
}
void MainWindow::importFolder() {
    if (busy()) return;
    QString error;
    const auto files = scanImages(input_->text(), &error);
    clearFiles(); folderDirty_ = false;
    output_->setText(error.isEmpty() ? QDir(input_->text()).absoluteFilePath("processed") : QString());
    appendFiles(files);
    if (error.isEmpty()) {
        status_->setText(files_.isEmpty() ? QStringLiteral("该文件夹没有支持的图片。")
                                         : QStringLiteral("导入完成。勾选需要处理的图片后开始。"));
    } else status_->setText(error);
}
void MainWindow::clearFiles() {
    if (busy()) return;
    updatingTable_ = true; ++previewVersion_; previewTimer_.stop();
    files_.clear(); results_.clear(); batchRows_.clear(); table_->setRowCount(0);
    previewBefore_ = {}; previewAfter_ = {};
    before_->setText(QStringLiteral("请选择图片")); after_->setText(QStringLiteral("暂无预览"));
    beforeInfo_->setText(QStringLiteral("原图")); afterInfo_->setText(QStringLiteral("处理效果"));
    afterInfo_->setToolTip({});
    details_->setPlainText(QStringLiteral("点击图片查看路径和处理详情。"));
    report_.clear(); openReport_->setEnabled(false); activeTasks_.clear();
    progress_->setRange(0, 100); progress_->setValue(0);
    status_->setText(QStringLiteral("列表已清空，原文件和已生成结果均保留。"));
    metrics_->setText(QStringLiteral("就绪，可调整并行任务数及批处理预算。"));
    updatingTable_ = false; refreshSelection();
}
void MainWindow::appendFiles(const QStringList &files) {
    updatingTable_ = true;
    QSet<QString> known;
    const auto identity = [](const QString &path) {
        QString value = QFileInfo(path).canonicalFilePath();
        if (value.isEmpty()) value = QFileInfo(path).absoluteFilePath();
#ifdef Q_OS_WIN
        value = value.toCaseFolded();
#endif
        return value;
    };
    for (const auto &path : files_) known.insert(identity(path));
    for (const auto &file : files) {
        const QString key = identity(file);
        if (known.contains(key)) continue;
        known.insert(key); files_ << QFileInfo(file).absoluteFilePath(); results_.append(FileResult{});
        const int row = table_->rowCount(); table_->insertRow(row);
        auto *item = new QTableWidgetItem(QFileInfo(file).fileName());
        item->setToolTip(file); item->setData(Qt::UserRole, file);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(Qt::Checked);
        table_->setItem(row, 0, item);
        table_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("待处理")));
        table_->setItem(row, 2, new QTableWidgetItem("—"));
    }
    updatingTable_ = false; refreshSelection();
    if (!files_.isEmpty() && table_->currentRow() < 0) table_->selectRow(0);
}
void MainWindow::addPaths(const QStringList &paths) {
    if (busy() || paths.isEmpty()) return;
    QStringList accepted; int unsupported = 0;
    for (const auto &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) accepted.append(scanImages(path));
        else if (info.isFile() && info.isReadable() && QStringList{"jpg", "jpeg", "png", "bmp"}.contains(info.suffix().toLower()))
            accepted.append(info.absoluteFilePath());
        else ++unsupported;
    }
    if (accepted.isEmpty()) { status_->setText(QStringLiteral("没有可添加的图片（仅支持 JPG / JPEG / PNG / BMP，不扫描子文件夹）。")); return; }
    if (files_.isEmpty()) {
        input_->setText(QFileInfo(accepted.first()).absolutePath());
        output_->setText(QDir(input_->text()).filePath("processed"));
    }
    if (folderDirty_ && !files_.isEmpty()) input_->setText(QFileInfo(files_.first()).absolutePath());
    folderDirty_ = false;
    const auto previous = files_.size(); appendFiles(accepted);
    status_->setText(QStringLiteral("已添加 %1 张；重复图片已忽略。%2")
        .arg(files_.size() - previous).arg(unsupported ? QStringLiteral("已跳过 %1 个不支持的项目。").arg(unsupported) : QString()));
}
void MainWindow::refreshSelection() {
    int checked = 0, retryable = 0;
    for (int row = 0; row < table_->rowCount(); ++row) if (table_->item(row, 0)->checkState() == Qt::Checked) {
        ++checked;
        if (results_[row].status == "failed" || results_[row].status == "cancelled") ++retryable;
    }
    count_->setText(QStringLiteral("共 %1 张，已勾选 %2 张").arg(files_.size()).arg(checked));
    start_->setEnabled(!busy() && !folderDirty_ && checked > 0);
    retry_->setEnabled(!busy() && !folderDirty_ && retryable > 0);
    remove_->setEnabled(!busy() && checked > 0); clear_->setEnabled(!busy() && !files_.isEmpty());
}
void MainWindow::showDetails() {
    const int row = table_->currentRow();
    if (row < 0 || row >= files_.size()) return;
    const auto &result = results_[row];
    QString text = QStringLiteral("输入：") + files_[row];
    if (!result.outputPath.isEmpty()) text += QStringLiteral("\n输出：") + result.outputPath;
    if (result.inputBytes > 0) text += QStringLiteral("\n原文件：") + sizeLabel(result.inputBytes);
    if (result.outputBytes > 0) {
        text += QStringLiteral("\n处理后：") + sizeLabel(result.outputBytes);
        if (result.inputBytes > 0) text += QStringLiteral("（%1%2%）")
            .arg(result.outputBytes <= result.inputBytes ? QStringLiteral("缩小 ") : QStringLiteral("增大 "))
            .arg(qAbs(result.outputBytes - result.inputBytes) * 100.0 / result.inputBytes, 0, 'f', 1);
        text += QStringLiteral("\n颜色：") + colorLabel(result.colorMode);
        if (result.jpegQualityUsed > 0) text += QStringLiteral("；JPEG 实际质量：%1").arg(result.jpegQualityUsed);
    }
    if (!result.error.isEmpty()) text += QStringLiteral("\n原因：") + result.error;
    details_->setPlainText(text);
}
void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (!busy() && event->mimeData()->hasUrls())
        for (const auto &url : event->mimeData()->urls()) if (url.isLocalFile()) { event->acceptProposedAction(); return; }
    event->ignore();
}
void MainWindow::dropEvent(QDropEvent *event) {
    if (busy()) { event->ignore(); return; }
    QStringList paths;
    for (const auto &url : event->mimeData()->urls()) if (url.isLocalFile()) paths << url.toLocalFile();
    if (paths.isEmpty()) { event->ignore(); return; }
    addPaths(paths); event->acceptProposedAction();
}
void MainWindow::restorePreferences() {
    if (settingsFile_.isEmpty()) return;
    const auto p = readPreferences(settingsFile_);
    edge_->setValue(p.options.maxEdge); quality_->setValue(p.options.jpegQuality);
    targetSize_->setValue(p.options.jpegTargetKiB); colorMode_->setCurrentIndex(static_cast<int>(p.options.colorMode));
    const int compressionIndex = pngCompression_->findData(p.options.pngCompression);
    pngCompression_->setCurrentIndex(compressionIndex >= 0 ? compressionIndex : 1);
    format_->setCurrentIndex(format_->findData(p.options.format));
    watermark_->setText(p.options.watermark); watermarkEnabled_->setChecked(p.watermarkEnabled);
    watermarkPosition_->setCurrentIndex(static_cast<int>(p.options.watermarkPosition));
    workers_->setValue(p.batch.workers); budget_->setValue(static_cast<int>(p.batch.memoryBudgetBytes / 1024 / 1024));
    input_->setText(p.lastFolder);
}
void MainWindow::savePreferences() {
    if (settingsFile_.isEmpty()) return;
    Preferences p; p.options = options(); p.options.watermark = watermark_->text();
    p.watermarkEnabled = watermarkEnabled_->isChecked(); p.batch = batchSettings(); p.lastFolder = input_->text();
    QString error;
    if (!writePreferences(settingsFile_, p, &error)) status_->setText(error + QStringLiteral("；本次图片处理结果不受影响。"));
}
void MainWindow::updateFormatControls() {
    const auto format = format_->currentData().toString();
    const bool jpeg = format == "jpg" || format == "original";
    const bool png = format == "png" || format == "original";
    quality_->setEnabled(!busy() && jpeg); targetSize_->setEnabled(!busy() && jpeg);
    pngCompression_->setEnabled(!busy() && png);
    QString text;
    if (format == "png") text = QStringLiteral("PNG 各档位均无损；“更小”可能更慢。已压缩的图片重新保存后不保证更小。");
    else if (format == "bmp") text = QStringLiteral("BMP 通常较大。需要压缩体积可选 JPG；需要保留透明度可选 PNG。");
    else text = QStringLiteral("JPG 上限为 0 时按所选质量保存；设置上限后自动调低质量，尺寸不变。达不到时请降低最长边。1 KiB＝1024 字节。");
    if (format == "original") text += QStringLiteral(" PNG 项仅使用 PNG 无损压缩设置。");
    compressionHelp_->setText(text);
}
void MainWindow::schedulePreview() {
    ++previewVersion_;
    if (!busy()) previewTimer_.start();
}
void MainWindow::beginPreview() {
    if (property("skipPreview").toBool()) return;
    if (busy() || previewWatcher_.isRunning()) return;
    const int row = table_->currentRow();
    if (row < 0 || row >= files_.size()) return;
    const QString file = files_[row]; const Options o = options();
    runningPreviewVersion_ = previewVersion_;
    previewWatcher_.setFuture(QtConcurrent::run([file, o] {
        Preview p;
        Options effective = o;
        effective.format = outputFormatForFile(file, o, &p.error);
        if (effective.format.isEmpty()) return p;
        p.format = effective.format;
        QImage original = loadImage(file, &p.error);
        if (original.isNull()) return p;
        p.inputSize = original.size(); p.inputBytes = QFileInfo(file).size(); p.colorMode = o.colorMode;
        p.before = original.scaled(800, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QImage transformed = transformImage(original, effective);
        if (transformed.isNull()) { p.error = QStringLiteral("无法生成预览"); return p; }
        const auto encoded = encodeImage(transformed, effective);
        if (!encoded.error.isEmpty()) { p.error = encoded.error; return p; }
        p.outputBytes = encoded.data.size(); p.jpegQualityUsed = encoded.jpegQualityUsed;
        transformed = QImage::fromData(encoded.data, effective.format.toLatin1());
        if (transformed.isNull()) { p.error = QStringLiteral("无法读取编码后的预览"); return p; }
        p.outputSize = transformed.size();
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
    for (QWidget *w : QList<QWidget *>{input_, output_, edge_, format_, colorMode_, workers_, budget_, watermarkEnabled_, import_, browse_, outputBrowse_, addFiles_, selectAll_, selectNone_, table_})
        w->setEnabled(!working);
    findChild<QPushButton *>("resetOptionsButton")->setEnabled(!working);
    findChild<QPushButton *>("trainingPresetButton")->setEnabled(!working);
    findChild<QPushButton *>("photoPresetButton")->setEnabled(!working);
    updateFormatControls();
    watermark_->setEnabled(!working && watermarkEnabled_->isChecked());
    watermarkPosition_->setEnabled(!working && watermarkEnabled_->isChecked());
    refreshSelection(); cancel_->setEnabled(working);
    openOutput_->setEnabled(!working); openReport_->setEnabled(!working && !report_.isEmpty());
}
void MainWindow::startBatch(bool retry) {
    if (busy() || files_.isEmpty() || folderDirty_) return;
    batchRows_.clear(); QStringList selected;
    for (int row = 0; row < files_.size(); ++row)
        if (table_->item(row, 0)->checkState() == Qt::Checked &&
            (!retry || results_[row].status == "failed" || results_[row].status == "cancelled")) {
            batchRows_ << row; selected << files_[row];
        }
    if (selected.isEmpty()) { status_->setText(QStringLiteral("请勾选需要处理的图片。")); return; }
    const QString out = output_->text().trimmed();
    if (out.isEmpty()) { status_->setText(QStringLiteral("请选择输出文件夹。")); return; }
    const QString outputCanonical = QFileInfo(out).canonicalFilePath();
    for (const auto &file : selected) {
        const QString parent = QFileInfo(file).absolutePath();
        const QString canonical = QFileInfo(parent).canonicalFilePath();
        if ((!canonical.isEmpty() && canonical.compare(outputCanonical, Qt::CaseInsensitive) == 0) ||
            parent.compare(QDir(out).absolutePath(), Qt::CaseInsensitive) == 0) {
            status_->setText(QStringLiteral("输出文件夹必须与原图文件夹不同。")); return;
        }
    }
    if (!QDir().mkpath(out)) { status_->setText(QStringLiteral("无法创建输出文件夹，请检查路径和权限。")); return; }
    { QTemporaryFile probe(QDir(out).filePath(".imagebatch-write-check-XXXXXX"));
      if (!probe.open()) { status_->setText(QStringLiteral("输出文件夹不可写：") + probe.errorString()); return; } }
    const auto o = options();
    const auto error = validateOptions(o);
    if (!error.isEmpty()) { status_->setText(error); return; }
    report_.clear();
    activeTasks_.clear();
    progress_->setRange(0, selected.size()); progress_->setValue(0);
    for (const int row : batchRows_) {
        results_[row] = FileResult{};
        table_->item(row, 1)->setText(QStringLiteral("待处理"));
        table_->item(row, 1)->setToolTip({});
        table_->item(row, 2)->setText("—");
    }
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    thread_ = new QThread(this);
    savePreferences();
    auto *worker = new BatchWorker(selected, out, o, cancelled_, batchSettings());
    worker->moveToThread(thread_);
    connect(thread_, &QThread::started, worker, &BatchWorker::run);
    connect(worker, &BatchWorker::progress, this, [this](int done, int total, FileResult result) {
        if (result.index < 0 || result.index >= batchRows_.size()) return;
        result.index = batchRows_[result.index]; updateResult(done, total, result);
    });
    connect(worker, &BatchWorker::taskStarted, this, [this](int index) {
        if (index >= 0 && index < batchRows_.size()) {
            const int row = batchRows_[index]; activeTasks_.insert(row);
            table_->item(row, 1)->setText(QStringLiteral("处理中"));
        }
    });
    connect(worker, &BatchWorker::metrics, this, [this](qint64 elapsed, int active, quint64 reserved, int queued, const QString &json) {
        metrics_->setText(QStringLiteral("用时 %1 秒  |  工作线程峰值 %2  |  估算预留峰值 %3 MiB  |  排队峰值 %4")
            .arg(elapsed / 1000.0, 0, 'f', 2).arg(active).arg(reserved / 1048576.0, 0, 'f', 1).arg(queued));
        metrics_->setToolTip(json);
    });
    connect(worker, &BatchWorker::finished, this,
            [this](int good, int bad, int cancelled, const QString &report, const QString &reportError) {
        report_ = report;
        status_->setText(QStringLiteral("处理结束：成功 %1，失败 %2，取消 %3。%4")
            .arg(good).arg(bad).arg(cancelled)
            .arg(reportError.isEmpty() ? QStringLiteral("CSV 与 JSON 汇总已保存到输出目录。")
                                      : QStringLiteral("报告或调度异常：") + reportError));
    });
    connect(worker, &BatchWorker::finished, thread_, &QThread::quit);
    connect(worker, &BatchWorker::finished, worker, &QObject::deleteLater);
    connect(thread_, &QThread::finished, this, [this] {
        thread_->deleteLater(); thread_ = nullptr;
        setBusy(false); schedulePreview(); emit batchComplete();
        if (closeAfterBatch_) QTimer::singleShot(0, this, &QWidget::close);
    });
    setBusy(true);
    status_->setText(QStringLiteral("正在处理 %1 张勾选图片；可以取消尚未开始的任务。").arg(selected.size()));
    thread_->start();
}
void MainWindow::updateResult(int done, int total, const FileResult &r) {
    progress_->setMaximum(total); progress_->setValue(done);
    const int row = r.index >= 0 ? r.index : files_.indexOf(r.inputPath);
    if (row < 0 || row >= files_.size()) return;
    results_[row] = r;
    activeTasks_.remove(row);
    auto *item = table_->item(row, 1);
    item->setText(r.status == "success" ? QStringLiteral("成功") : r.status == "cancelled" ? QStringLiteral("取消") : QStringLiteral("失败"));
    item->setToolTip(r.error.isEmpty() ? r.outputPath : r.error);
    table_->item(row, 2)->setText(r.status == "success" ? QString::number(r.elapsedMs) : "—");
    metrics_->setText(QStringLiteral("正在处理 %1  |  等待 %2  |  已完成 %3 / %4")
        .arg(activeTasks_.size()).arg(std::max(0, total - done - static_cast<int>(activeTasks_.size()))).arg(done).arg(total));
    if (row == table_->currentRow()) showDetails();
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (busy()) {
        closeAfterBatch_ = true;
        if (cancelled_) cancelled_->store(true);
        status_->setText(QStringLiteral("正在停止后续任务并保存报告，完成后窗口将自动关闭。"));
        cancel_->setEnabled(false); event->ignore(); return;
    }
    savePreferences();
    QMainWindow::closeEvent(event);
}
}
