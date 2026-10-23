#pragma once
#include <QImage>
#include <QByteArray>
#include <QStringList>
#include <QMetaType>

namespace imagebatch {
enum class WatermarkPosition { BottomRight, BottomLeft, TopRight, TopLeft };
enum class ColorMode { KeepColor, Grayscale, GrayscaleRgb };
struct Options {
    int maxEdge = 1920; // 0 means keep original dimensions.
    QString format = "jpg";
    int jpegQuality = 90;
    int jpegTargetKiB = 0; // 0: fixed quality; otherwise reduce quality to fit this upper limit.
    int pngCompression = 50; // Qt PNG handler: 0..100, lossless at every level.
    ColorMode colorMode = ColorMode::KeepColor;
    QString watermark;
    WatermarkPosition watermarkPosition = WatermarkPosition::BottomRight;
};
struct FileResult {
    QString inputPath;
    QString outputPath;
    QString status;
    QString error;
    QSize originalSize;
    QSize outputSize;
    int index = -1;
    qint64 elapsedMs = 0;
    qint64 inputBytes = 0, outputBytes = 0;
    int jpegQualityUsed = -1;
    ColorMode colorMode = ColorMode::KeepColor;
};
struct EncodedImage { QByteArray data; QString error; int jpegQualityUsed = -1; };
QString validateOptions(const Options &options);
QStringList scanImages(const QString &directory, QString *error = nullptr);
QString outputFormatForFile(const QString &path, const Options &options, QString *error = nullptr);
QImage loadImage(const QString &path, QString *error = nullptr);
QImage transformImage(const QImage &input, const Options &options);
EncodedImage encodeImage(const QImage &transformed, const Options &options);
FileResult processFile(const QString &input, const QString &outputDirectory,
                       const Options &options);
QString writeReport(const QList<FileResult> &results, const QString &directory,
                    QString *error = nullptr);
}
Q_DECLARE_METATYPE(imagebatch::FileResult)
