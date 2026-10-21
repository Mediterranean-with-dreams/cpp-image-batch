#pragma once
#include <QImage>
#include <QStringList>
#include <QMetaType>

namespace imagebatch {
struct Options {
    int maxEdge = 1920; // 0 means keep original dimensions.
    QString format = "jpg";
    int jpegQuality = 90;
    QString watermark;
};
struct FileResult {
    QString inputPath;
    QString outputPath;
    QString status;
    QString error;
    QSize originalSize;
    QSize outputSize;
};
QString validateOptions(const Options &options);
QStringList scanImages(const QString &directory, QString *error = nullptr);
QImage loadImage(const QString &path, QString *error = nullptr);
QImage transformImage(const QImage &input, const Options &options);
FileResult processFile(const QString &input, const QString &outputDirectory,
                       const Options &options);
QString writeReport(const QList<FileResult> &results, const QString &directory,
                    QString *error = nullptr);
}
Q_DECLARE_METATYPE(imagebatch::FileResult)
