#include "BookCardWidget.h"
#include "DurationFormat.h"
#include "../settings/Theme.h"
#include "../library/Library.h"
#include "../library/MetadataStore.h"
#include "../widgets/MenuOption.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QPdfDocument>

namespace Genesis {

BookCardWidget::BookCardWidget(const QString& path, const BookInfo& info,
                               QWidget* parent)
    : QWidget(parent)
    , m_path(path)
    , m_info(info)
    , m_selectionMode(false)
    , m_selected(false)
    , m_hovered(false)
{
    setFixedWidth(kCardWidth);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: transparent;");

    QVBoxLayout* l = new QVBoxLayout(this);
    // Top margin reserves the thumbnail area — the labels flow BELOW the
    // cover, which is painted in paintEvent within this reserved band.
    l->setContentsMargins(10, kThumbH + 14, 10, 8);
    l->setSpacing(4);

    // ---- thumbnail is drawn in paintEvent ----

    // title — word-wrapped, max 2 lines. Colours come from applyTextTheme(),
    // which is re-run when the shelf text colour is switched.
    m_title = new QLabel(this);
    m_title->setAlignment(Qt::AlignHCenter);
    m_title->setWordWrap(true);
    m_title->setMaximumHeight(
        QFontMetrics(m_title->font()).lineSpacing() * 2 + 2);
    l->addWidget(m_title);

    // last opened (relative time)
    m_lastOpened = new QLabel(this);
    m_lastOpened->setAlignment(Qt::AlignHCenter);
    l->addWidget(m_lastOpened);

    // read duration
    m_readTime = new QLabel(this);
    m_readTime->setAlignment(Qt::AlignHCenter);
    l->addWidget(m_readTime);

    l->addStretch();   // bottom spacer so all cards stay same height

    connect(&Theme::instance(), &Theme::changed, this,
            [this]() { applyTextTheme(); });
    applyTextTheme();
    updateInfo(info);
}

void BookCardWidget::applyTextTheme()
{
    const Theme& t = Theme::instance();
    m_title->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; font-weight: bold;"
                       " background: transparent;")
            .arg(t.textCss()));
    const QString mutedCss =
        QStringLiteral("color: %1; font-size: 10px; background: transparent;")
            .arg(t.mutedTextCss());
    m_lastOpened->setStyleSheet(mutedCss);
    m_readTime->setStyleSheet(mutedCss);
}

void BookCardWidget::updateInfo(const BookInfo& info)
{
    m_info = info;

    // Title: up to 2 lines, elide the last line if it overflows.
    const QString titleText = info.title.isEmpty()
        ? QFileInfo(m_path).completeBaseName()
        : info.title;
    m_title->setText(titleText);

    if (info.lastOpened.isValid())
        m_lastOpened->setText(relativeTime(info.lastOpened));
    else
        m_lastOpened->setText(QString());

    m_readTime->setText(formatDuration(info.readSeconds));

    // Invalidate cached thumbnail and re-render on next paint.
    m_thumbnail = QPixmap();
    update();
}

QString BookCardWidget::relativeTime(const QDateTime& dt)
{
    const int secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 0)   // future? treat as now
        return QStringLiteral("0分钟前");
    if (secs < 3600)
        return QString::fromUtf8("%1分钟前").arg(qMax(1, secs / 60));
    if (secs < 86400)
        return QString::fromUtf8("%1小时前").arg(secs / 3600);
    if (secs < 3 * 86400)
        return QString::fromUtf8("%1天前").arg(secs / 86400);
    return dt.toString(QStringLiteral("yyyy-MM-dd"));
}

QPixmap BookCardWidget::ensureThumbnail()
{
    if (!m_thumbnail.isNull())
        return m_thumbnail;

    const qreal dpr = devicePixelRatioF();

    QDir cacheDir(Library::directory());
    // "v2" invalidates older, low-resolution caches; DPR in the key keeps
    // caches from different display scales apart.
    const QString cacheKey = QFileInfo(m_path).completeBaseName().toLower()
                             + QStringLiteral(".thumb-v2-%1.png").arg(qRound(dpr * 100));
    QString thumbPath = cacheDir.filePath(cacheKey);
    if (QFileInfo::exists(thumbPath)) {
        QPixmap pm(thumbPath);
        if (!pm.isNull()) {
            // Restore the DPR — QPixmap(file) always loads at DPR 1, which
            // would draw the high-res image at its full pixel size (blurry
            // AND oversized). With the DPR set, it draws at logical size
            // from the device-resolution pixels: crisp.
            pm.setDevicePixelRatio(dpr);
            m_thumbnail = pm;
            return m_thumbnail;
        }
    }

    // Render the first page directly at the exact physical pixel size of the
    // thumbnail (kThumbW x dpr wide). Rendering at the final size — instead
    // of rendering large and downscaling — keeps text hinting sharp: pdfium
    // rasterizes glyphs for the actual output resolution.
    QPdfDocument doc;
    doc.load(m_path);
    if (doc.pageCount() > 0) {
        const QSizeF ptSize = doc.pagePointSize(0);
        if (ptSize.width() > 0 && ptSize.height() > 0) {
            // Fit the page into kThumbW x kThumbH (logical), then scale by dpr
            // for physical pixels.
            const qreal fit = qMin(kThumbW / ptSize.width(),
                                   kThumbH / ptSize.height());
            const QSize renderSize(qRound(ptSize.width() * fit * dpr),
                                   qRound(ptSize.height() * fit * dpr));
            const QImage img = doc.render(0, renderSize);
            if (!img.isNull()) {
                QPixmap pm = QPixmap::fromImage(img);
                pm.setDevicePixelRatio(dpr);
                pm.save(thumbPath, "PNG");
                m_thumbnail = pm;
            }
        }
    }
    return m_thumbnail;
}

void BookCardWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Hover fill comes from the theme: the light blue that reads as "hover" on
    // white would smear behind white text on a dark background.
    if (m_hovered)
        p.fillRect(rect(), Theme::instance().hoverFill());
    else
        p.fillRect(rect(), Qt::transparent);

    // Cover thumbnail at the top of the card; text labels flow below it
    // (the layout's top margin reserves this band).
    const QPixmap thumb = ensureThumbnail();
    if (!thumb.isNull()) {
        const QSizeF logical = thumb.deviceIndependentSize();
        const int x = qRound((kCardWidth - logical.width()) / 2.0);
        const int y = 8 + qRound((kThumbH - logical.height()) / 2.0);
        p.drawPixmap(x, y, thumb);

        // Selection mode: reuse the app-wide toggle dot from MenuOption at
        // the top-right corner of the cover — hollow when unselected, filled
        // green when selected.
        if (m_selectionMode) {
            const qreal dotR = MenuOption::kDotRadius;
            const qreal cx = qMin(qreal(x + kThumbW), qreal(width() - 3))
                             - dotR - 3;
            const qreal cy = 8 + 3 + dotR;
            MenuOption::drawDot(p, QPointF(cx, cy), m_selected);
        }
    }
}

void BookCardWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked(m_path);
    QWidget::mousePressEvent(event);
}

void BookCardWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit activated(m_path);
    QWidget::mouseDoubleClickEvent(event);
}

void BookCardWidget::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void BookCardWidget::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}

} // namespace Genesis
