// Regenerates picture/genesis.ico from picture/Genesis.png.
//
// The .ico is what Windows shows for Genesis.exe in Explorer, the taskbar and
// Task Manager; it is a separate artifact from the runtime window icon, which
// is loaded from the PNG via the Qt resource system. Re-run this whenever
// Genesis.png changes, or the executable keeps the old artwork.
//
// Why this is a hand-rolled ICO writer rather than QImageWriter:
// QImageWriter writes exactly ONE image per file, which yields an .ico with a
// single resolution — Windows then scales that one bitmap to every size it
// needs, and small sizes turn to mush. A proper icon carries several sizes and
// lets the shell pick. Qt has no multi-image ICO writer, so the container is
// assembled here (ICONDIR + one ICONDIRENTRY per size + the image payloads).
//
// The transparent margin is trimmed before scaling, for the same reason as the
// runtime icon in main.cpp: the artwork sits inside a wide border in the source
// canvas, and scaling that border along with it makes the glyph look far
// smaller than the icon box it occupies.

#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTextStream>

namespace {

// Sizes embedded in the .ico. 16/24/32/48 cover the classic shell views,
// 64/128/256 the large-icon and preview ones. Windows requires the 256 entry
// to be PNG-compressed, which is what pngBytes() produces for every size here.
const QList<int> kSizes = {16, 24, 32, 48, 64, 128, 256};

// Alpha values at or below this are treated as empty margin.
constexpr int kAlphaThreshold = 16;

// Size of the PNG embedded by resources.qrc for the runtime window icon.
// Comfortably above the largest size main.cpp pre-scales to (256), so nothing
// is ever upscaled from it, with room for high-DPI displays.
constexpr int kIconPngSize = 512;

// Crop the fully-transparent margin around the artwork.
QImage trimTransparentEdges(const QImage& src)
{
    if (src.isNull())
        return src;
    int minX = src.width(), minY = src.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            if (qAlpha(src.pixel(x, y)) > kAlphaThreshold) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0)
        return src;   // fully transparent: nothing to trim to
    return src.copy(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

// PNG bytes for one square icon entry: the artwork scaled to fit `box`,
// centered on a transparent canvas so the entry is exactly box x box.
QByteArray iconEntry(const QImage& src, int box)
{
    QImage canvas(box, box, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    if (!src.isNull()) {
        const QImage scaled = src.scaled(box, box, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
        QPainter p(&canvas);
        p.drawImage((box - scaled.width()) / 2, (box - scaled.height()) / 2,
                    scaled);
        p.end();
    }
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    canvas.save(&buf, "PNG");
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    // Paths default to the repository layout; argv can override for testing.
    const QString repoRoot = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral(SOURCE_DIR);
    const QString srcPath = repoRoot + QStringLiteral("/picture/Genesis.png");
    const QString outPath = repoRoot + QStringLiteral("/picture/genesis.ico");

    QTextStream err(stderr);
    QTextStream out(stdout);

    QImage source(srcPath);
    if (source.isNull()) {
        err << "mkico: cannot load " << srcPath << "\n";
        return 1;
    }
    out << "source " << source.width() << "x" << source.height() << "\n";

    source = trimTransparentEdges(source);
    out << "trimmed " << source.width() << "x" << source.height() << "\n";

    QList<QByteArray> entries;
    for (int s : kSizes)
        entries << iconEntry(source, s);

    QByteArray ico;
    QDataStream ds(&ico, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);

    // ICONDIR: reserved, type (1 = icon), image count.
    ds << quint16(0);
    ds << quint16(1);
    ds << quint16(kSizes.size());

    // ICONDIRENTRY per image; the payloads follow all entries.
    quint32 offset = 6 + 16 * quint32(kSizes.size());
    for (int i = 0; i < kSizes.size(); ++i) {
        const int s = kSizes.at(i);
        ds << quint8(s >= 256 ? 0 : s);    // width  (0 encodes 256)
        ds << quint8(s >= 256 ? 0 : s);    // height (0 encodes 256)
        ds << quint8(0);                   // palette entry count
        ds << quint8(0);                   // reserved
        ds << quint16(1);                  // colour planes
        ds << quint16(32);                 // bits per pixel
        ds << quint32(entries.at(i).size());
        ds << quint32(offset);
        offset += quint32(entries.at(i).size());
    }
    for (const QByteArray& e : entries)
        ds.writeRawData(e.constData(), e.size());

    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly)) {
        err << "mkico: cannot write " << outPath << "\n";
        return 1;
    }
    f.write(ico);
    f.close();

    out << "wrote " << outPath << " (" << ico.size() << " bytes, "
        << kSizes.size() << " sizes)\n";

    // Also refresh the PNG that resources.qrc embeds for the runtime window
    // icon. It is generated here, from the same master art, so the two derived
    // files can never drift apart — and it is written at the largest size the
    // UI can ask for, rather than the master's full resolution, which keeps
    // roughly a third of the executable's bytes out of the binary.
    const QString pngPath = repoRoot + QStringLiteral("/picture/genesis-icon.png");
    QImage iconPng(kIconPngSize, kIconPngSize, QImage::Format_ARGB32);
    iconPng.fill(Qt::transparent);
    {
        const QImage scaled = source.scaled(kIconPngSize, kIconPngSize,
                                            Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
        QPainter p(&iconPng);
        p.drawImage((kIconPngSize - scaled.width()) / 2,
                    (kIconPngSize - scaled.height()) / 2, scaled);
    }
    if (!iconPng.save(pngPath, "PNG")) {
        err << "mkico: cannot write " << pngPath << "\n";
        return 1;
    }
    out << "wrote " << pngPath << " (" << QFileInfo(pngPath).size()
        << " bytes, " << kIconPngSize << "px)\n";
    return 0;
}
