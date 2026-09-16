#include "BackgroundWidget.h"

#include <QPainter>

namespace Genesis {

BackgroundWidget::BackgroundWidget(QWidget* parent)
    : QWidget(parent)
    , m_opacity(1.0)
{
    // White base + optional image, both painted in paintEvent. No stylesheet:
    // an unscoped background rule here would cascade into every descendant
    // (the bookshelf, the part panels...) and paint them opaque white.
}

void BackgroundWidget::setImagePath(const QString& path)
{
    m_imagePath = path;
    m_pixmap = path.isEmpty() ? QPixmap() : QPixmap(path);
    update();
}

void BackgroundWidget::setImageOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    update();
}

void BackgroundWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);   // base
    if (m_pixmap.isNull() || m_opacity <= 0.0)
        return;

    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(m_opacity);

    // Scale to cover the whole widget, keeping aspect ratio; center-crop.
    const QSize scaled =
        m_pixmap.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
    const QRect target(QPoint((width() - scaled.width()) / 2,
                              (height() - scaled.height()) / 2),
                       scaled);
    painter.drawPixmap(target, m_pixmap);
}

} // namespace Genesis
