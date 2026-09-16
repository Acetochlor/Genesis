#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTimer>

namespace {

// Crop the fully-transparent margin around the artwork. Without this the
// icon carries the source's padding, so at a fixed pixel size the visible
// glyph ends up only a fraction of the box.
QImage trimTransparentEdges(const QImage& src)
{
    if (src.isNull())
        return src;
    int minX = src.width(), minY = src.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            if (qAlpha(src.pixel(x, y)) > 16) {
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

// Scale the artwork to fill a `box`-square, preserving its aspect ratio and
// centering it on a transparent canvas. The result is exactly box x box, which
// matters because QIcon::pixmap() prefers the LARGEST stored pixmap that will
// fit the request: storing odd-sized pixmaps makes the icon render bigger than
// the layout asked for.
QPixmap fitIntoBox(const QImage& src, int box)
{
    QImage canvas(box, box, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    if (src.isNull())
        return QPixmap::fromImage(canvas);
    const QImage scaled = src.scaled(box, box, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
    QPainter p(&canvas);
    p.drawImage((box - scaled.width()) / 2, (box - scaled.height()) / 2,
                scaled);
    p.end();
    return QPixmap::fromImage(canvas);
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Genesis");
    app.setApplicationDisplayName("Genesis");

    // Window / taskbar icon (top-left corner, taskbar while running).
    // The resource is pre-scaled by tools/mkico rather than embedded at the
    // master art's full resolution: only sizes up to 256px are ever requested,
    // so shipping the 2000px original cost roughly a third of the executable
    // for pixels that were always downscaled away. QIcon would still decode it
    // lazily on the FIRST pixmap request — which used to happen when the first
    // dialog opened, making 导入/关于 feel slow — so the sizes the UI actually
    // uses are built once here and served from cache afterwards.
    {
        QImage source(QStringLiteral(":/genesis-icon.png"));

        // mkico already trims the transparent margin when it writes this
        // resource; re-trimming is a cheap no-op that keeps the sizing correct
        // if the resource is ever swapped for one that still has its padding.
        source = trimTransparentEdges(source);

        QIcon icon;
        for (const int s : {16, 20, 24, 32, 48, 64, 128, 256})
            icon.addPixmap(fitIntoBox(source, s));
        app.setWindowIcon(icon);
    }

    Genesis::MainWindow window;
    window.show();

    // Build the 导入/关于 dialogs during startup idle so their first click
    // shows a ready-made window (see MainWindow::prewarmDialogs).
    QTimer::singleShot(200, &window,
                       [&window]() { window.prewarmDialogs(); });

    return app.exec();
}
