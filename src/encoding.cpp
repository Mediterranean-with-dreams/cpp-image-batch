#include "processor.h"
#include <QBuffer>
#include <QImageWriter>
#include <utility>
namespace imagebatch {
EncodedImage encodeImage(const QImage &image, const Options &options) {
    EncodedImage result;
    result.error = validateOptions(options);
    if (!result.error.isEmpty()) return result;
    if (image.isNull()) { result.error = QStringLiteral("待编码图片为空"); return result; }
    if (options.format == "original") { result.error = QStringLiteral("编码前需要先解析原始格式"); return result; }
    auto encode = [&](int quality) {
        EncodedImage encoded;
        QBuffer buffer(&encoded.data);
        if (!buffer.open(QIODevice::WriteOnly)) { encoded.error = buffer.errorString(); return encoded; }
        QImageWriter writer(&buffer, options.format.toLatin1());
        if (options.format == "jpg") { writer.setQuality(quality); encoded.jpegQualityUsed = quality; }
        if (options.format == "png") writer.setCompression(options.pngCompression);
        if (!writer.write(image)) { encoded.error = writer.errorString(); encoded.data.clear(); }
        return encoded;
    };
    result = encode(options.jpegQuality);
    if (!result.error.isEmpty() || options.format != "jpg" || options.jpegTargetKiB == 0) return result;
    const qint64 limit = qint64(options.jpegTargetKiB) * 1024;
    if (result.data.size() <= limit) return result;
    // Keep a verified feasible result throughout a bounded quality search.
    // JPEG size is not strictly monotone: claim a fitting result, not a globally optimal quality.
    auto best = encode(1);
    if (!best.error.isEmpty()) return best;
    if (best.data.size() > limit) {
        result.error = QStringLiteral("当前尺寸在 JPEG 质量 1 时仍为 %1 KiB，超过 %2 KiB 上限。请降低最长边或提高上限。")
            .arg(best.data.size() / 1024.0, 0, 'f', 1).arg(options.jpegTargetKiB);
        result.data.clear(); result.jpegQualityUsed = -1; return result;
    }
    int low = 2, high = options.jpegQuality - 1;
    result.data.clear(); // Release the failed high-quality buffer before repeated encoding.
    while (low <= high) {
        const int middle = low + (high - low) / 2;
        auto candidate = encode(middle);
        if (!candidate.error.isEmpty()) return candidate;
        if (candidate.data.size() <= limit) { best = std::move(candidate); low = middle + 1; }
        else high = middle - 1;
    }
    return best;
}
}
