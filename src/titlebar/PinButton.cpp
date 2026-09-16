#include "PinButton.h"

#include <QPainter>
#include <QPaintEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QScreen>

namespace Genesis {

namespace {
constexpr int kButtonWidth  = 44;
constexpr int kButtonHeight = 34;
constexpr int kIconSize     = 18;   // logical (device-independent) icon size
const QColor kHoverBg("#E6F7FF");
} // namespace

PinButton::PinButton(QWidget* parent)
    : QAbstractButton(parent)
    , m_pinned(false)
    , m_hovered(false)
    , m_scaledDpr(0)
    , m_scale(1.0)
{
    setFixedSize(kButtonWidth, kButtonHeight);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    // Compiled into the resources.qrc bundle (see resources.qrc).
    m_sourceOpen   = QPixmap(QStringLiteral(":/Pin-opened.png"));
    m_sourceClosed = QPixmap(QStringLiteral(":/Pin-closed.png"));
}

void PinButton::setScale(qreal scale)
{
    if (qFuzzyCompare(m_scale, scale))
        return;
    m_scale = scale;
    m_scaledDpr = 0;   // icons must be re-scaled at the new size
    setFixedSize(qRound(kButtonWidth * scale),
                 qRound(kButtonHeight * scale));
    update();
}

void PinButton::ensureScaledIcons(qreal dpr)
{
    if (qFuzzyCompare(m_scaledDpr, dpr) || dpr <= 0)
        return;
    m_scaledDpr = dpr;
    const int size = qRound(kIconSize * m_scale * dpr);
    // Downscale the high-res source once, to exactly the target device
    // pixels, then mark it with the DPR so it draws at the logical size.
    m_openIcon = m_sourceOpen.scaled(size, size,
                                     Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
    m_openIcon.setDevicePixelRatio(dpr);
    m_closedIcon = m_sourceClosed.scaled(size, size,
                                         Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    m_closedIcon.setDevicePixelRatio(dpr);
}

void PinButton::setPinned(bool pinned)
{
    if (m_pinned == pinned)
        return;
    m_pinned = pinned;
    update();
}

void PinButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void PinButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

bool PinButton::event(QEvent* event)
{
    // Moving across screens with a different scale invalidates the cached
    // icons; regenerate them on the new DPR before the next paint.
    if (event->type() == QEvent::DevicePixelRatioChange) {
        m_scaledDpr = 0;
        ensureScaledIcons(devicePixelRatioF());
        update();
    }
    return QAbstractButton::event(event);
}

void PinButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);

    // Hover/pressed fill, matching the WindowButton theme.
    if (m_hovered || isDown()) {
        QColor bg = kHoverBg;
        if (isDown())
            bg = bg.darker(112);
        p.fillRect(rect(), bg);
    }

    ensureScaledIcons(devicePixelRatioF());
    // Icon semantics: Pin-opened = the window IS pinned (always on top);
    // Pin-closed = not pinned.
    const QPixmap& icon = m_pinned ? m_openIcon : m_closedIcon;
    if (icon.isNull())
        return;
    // Draw centered at the logical size; the pixmap's DPR carries the device
    // resolution, so no hardware upscaling happens and it stays crisp.
    p.drawPixmap((width() - icon.deviceIndependentSize().width()) / 2.0,
                 (height() - icon.deviceIndependentSize().height()) / 2.0,
                 icon);
}

} // namespace Genesis
