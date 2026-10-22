#include "processor.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QFont>
#include <QColorSpace>
#include <QTemporaryFile>
#include <QDateTime>
#include <QTextStream>
#include <QScopeGuard>
#include <algorithm>
#include <mutex>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace imagebatch {
QString validateOptions(const Options &o) {
    if (o.maxEdge < 0 || o.maxEdge > 20000)
        return QStringLiteral("最长边必须为 0～20000 像素（0 表示保持原尺寸）");
    if (!QStringList{"jpg", "png", "bmp"}.contains(o.format))
        return QStringLiteral("输出格式必须为 JPG、PNG 或 BMP");
    if (o.jpegQuality < 1 || o.jpegQuality > 100)
        return QStringLiteral("JPEG 质量必须为 1～100");
    if (o.watermark.size() > 100)
        return QStringLiteral("水印文字最多 100 个字符");
    return {};
}
QStringList scanImages(const QString &path, QString *error) {
    if (error) error->clear();
    QDir dir(path);
    if (path.trimmed().isEmpty() || !dir.exists()) {
        if (error) *error = QStringLiteral("输入文件夹不存在");
        return {};
    }
    QStringList files;
    for (const auto &f : dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
        if (QStringList{"jpg", "jpeg", "png", "bmp"}.contains(f.suffix().toLower()))
            files.append(f.absoluteFilePath());
    }
    return files;
}
QImage loadImage(const QString &path, QString *error) {
    if (error) error->clear();
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull() && error) *error = reader.errorString();
    return img;
}
QImage transformImage(const QImage &input, const Options &o) {
    if (input.isNull() || !validateOptions(o).isEmpty()) return {};
    QImage image = input;
    if (o.maxEdge > 0 && std::max(image.width(), image.height()) > o.maxEdge)
        image = image.scaled(o.maxEdge, o.maxEdge, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    // Work in a predictable color space for display/export.
    if (image.colorSpace().isValid()) image = image.convertedToColorSpace(QColorSpace::SRgb);
    image = image.convertToFormat(QImage::Format_ARGB32);
    image.setDevicePixelRatio(1.0);
    if (o.format == "jpg" || o.format == "bmp") {
        QImage opaque(image.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter painter(&opaque);
        painter.drawImage(0, 0, image);
        painter.end();
        image = opaque;
    }
    if (!o.watermark.trimmed().isEmpty()) {
        // Repeated Windows benchmarks found intermittent glyph differences
        // during parallel painting. Serialize only this short text-paint section;
        // image decoding, resizing and encoding remain parallel.
        static std::mutex textPaintingMutex;
        std::lock_guard<std::mutex> textLock(textPaintingMutex);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing);
        QFont font;
        font.setPixelSize(std::max(1, std::min(64, std::min(image.width(), image.height()) / 18)));
        font.setBold(true);
        painter.setFont(font);
        const int margin = std::max(1, std::min(image.width(), image.height()) / 30);
        QRect area = image.rect().adjusted(margin, margin, -margin, -margin);
        while (font.pixelSize() > 1 && painter.fontMetrics().horizontalAdvance(o.watermark) > area.width()) {
            font.setPixelSize(font.pixelSize() - 1);
            painter.setFont(font);
        }
        int alignment = Qt::AlignBottom | Qt::AlignRight;
        switch (o.watermarkPosition) {
        case WatermarkPosition::BottomLeft: alignment = Qt::AlignBottom | Qt::AlignLeft; break;
        case WatermarkPosition::TopRight: alignment = Qt::AlignTop | Qt::AlignRight; break;
        case WatermarkPosition::TopLeft: alignment = Qt::AlignTop | Qt::AlignLeft; break;
        case WatermarkPosition::BottomRight: break;
        }
        painter.setPen(QColor(0, 0, 0, 200));
        painter.drawText(area.translated(1, 1), alignment, o.watermark);
        painter.setPen(QColor(255, 255, 255, 230));
        painter.drawText(area, alignment, o.watermark);
    }
    return image;
}
static QString candidatePath(const QString &dir, const QString &stem, const QString &suffix, int n) {
    return QDir(dir).filePath(stem + (n ? "_" + QString::number(n) : QString()) + "." + suffix);
}
// Publish a fully closed temporary file without Qt's copy/truncate fallback.
// The temporary file is in the destination directory, on the same filesystem.
static bool publishNoReplace(const QString &source, const QString &destination, QString *error) {
#ifdef Q_OS_WIN
    const auto from = QDir::toNativeSeparators(source).toStdWString();
    const auto to = QDir::toNativeSeparators(destination).toStdWString();
    if (MoveFileW(from.c_str(), to.c_str())) return true;
    *error = QStringLiteral("保存文件失败，Windows 错误码：%1").arg(GetLastError());
#else
    // link() fails if destination exists. Unlink only our temporary name.
    if (::link(QFile::encodeName(source).constData(), QFile::encodeName(destination).constData()) == 0) {
        QFile::remove(source);
        return true;
    }
    *error = QString::fromLocal8Bit(std::strerror(errno));
#endif
    return false;
}
FileResult processFile(const QString &input, const QString &outputDir, const Options &o) {
    FileResult r;
    r.inputPath = input;
    r.status = "failed";
    r.error = validateOptions(o);
    if (!r.error.isEmpty()) return r;
    if (outputDir.trimmed().isEmpty()) { r.error = QStringLiteral("输出文件夹不能为空"); return r; }
    if (QFileInfo(input).absolutePath() == QDir(outputDir).absolutePath()) {
        r.error = QStringLiteral("输出文件夹必须与原图文件夹不同"); return r;
    }
    QImage image = loadImage(input, &r.error);
    if (image.isNull()) return r;
    r.originalSize = image.size();
    image = transformImage(image, o);
    if (image.isNull()) { r.error = QStringLiteral("图像处理失败或内存不足"); return r; }
    r.outputSize = image.size();
    if (!QDir().mkpath(outputDir)) { r.error = QStringLiteral("无法创建输出文件夹"); return r; }
    QString temporaryPath;
    const auto cleanup = qScopeGuard([&] { if (!temporaryPath.isEmpty()) QFile::remove(temporaryPath); });
    {
        QTemporaryFile temp(QDir(outputDir).filePath(".imagebatch-XXXXXX"));
        if (!temp.open()) { r.error = temp.errorString(); return r; }
        QImageWriter writer(&temp, o.format.toLatin1());
        if (o.format == "jpg") writer.setQuality(o.jpegQuality);
        if (!writer.write(image)) { r.error = writer.errorString(); return r; }
        if (!temp.flush()) { r.error = temp.errorString(); return r; }
        temporaryPath = temp.fileName();
        temp.setAutoRemove(false);
    } // QTemporaryFile::close() alone keeps its native handle open; destroy it.
    const QString stem = QFileInfo(input).completeBaseName() + "_processed";
    for (int n = 0; n < 100000; ++n) {
        const QString out = candidatePath(outputDir, stem, o.format, n);
        if (QFileInfo::exists(out)) continue;
        if (publishNoReplace(temporaryPath, out, &r.error)) {
            r.outputPath = out;
            r.status = "success";
            r.error.clear();
            return r;
        }
        if (!QFileInfo::exists(out)) return r;
    }
    r.error = QStringLiteral("同名文件过多，无法分配输出文件名");
    return r;
}
static QString csv(QString s) { s.replace('"', "\"\""); return '"' + s + '"'; }
QString writeReport(const QList<FileResult> &results, const QString &directory, QString *error) {
    if (error) error->clear();
    if (!QDir().mkpath(directory)) { if (error) *error = QStringLiteral("无法创建报告目录"); return {}; }
    const QString stem = "batch_report_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    for (int n = 0; n < 100000; ++n) {
        QString path = candidatePath(directory, stem, "csv", n);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (QFileInfo::exists(path)) continue;
            if (error) *error = file.errorString();
            return {};
        }
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream.setGenerateByteOrderMark(true);
        stream << "input,output,status,error,original_width,original_height,output_width,output_height,elapsed_ms\n";
        for (const auto &r : results)
            stream << csv(r.inputPath) << ',' << csv(r.outputPath) << ',' << csv(r.status) << ','
                   << csv(r.error) << ',' << r.originalSize.width() << ',' << r.originalSize.height()
                   << ',' << r.outputSize.width() << ',' << r.outputSize.height() << ',' << r.elapsedMs << '\n';
        stream.flush();
        if (stream.status() != QTextStream::Ok || !file.flush()) {
            if (error) *error = QStringLiteral("报告写入失败：") + file.errorString();
            file.close(); file.remove(); return {};
        }
        return path;
    }
    if (error) *error = QStringLiteral("报告同名文件过多");
    return {};
}
}
