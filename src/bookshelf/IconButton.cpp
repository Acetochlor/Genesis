#include "IconButton.h"

#include <QPainter>
#include <QPaintEvent>
#include <QEnterEvent>

namespace Genesis {

namespace {

constexpr int kButtonSize = 30;
constexpr int kGlyphSize  = 14;   // side length of the drawn glyph, in px

const QColor kIconColor("#5A5A5A");   // resting stroke — neutral, subtle
const QColor kHoverBg("#E6F7FF");     // light-blue rounded hover fill
const QColor kHoverIcon("#33CCFF");   // accent stroke on hover

} // namespace

IconButton::IconButton(Glyph glyph, QWidget* parent)
    : QAbstractButton(parent)
    , m_glyph(glyph)
    , m_hovered(false)
{
    setFixedSize(kButtonSize, kButtonSize);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void IconButton::setGlyph(Glyph glyph)
{
    if (m_glyph == glyph)
        return;
    m_glyph = glyph;
    update();
}

void IconButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void IconButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void IconButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_hovered || isDown()) {
        QColor bg = kHoverBg;
        if (isDown())
            bg = bg.darker(112);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(rect(), 6, 6);
    }

    QPen pen(m_hovered || isDown() ? kHoverIcon : kIconColor, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal cx = width() / 2.0;
    const qreal cy = height() / 2.0;
    const qreal h = kGlyphSize / 2.0;

    switch (m_glyph) {
    case Glyph::Search: {
        // Magnifier: circle upper-left + handle to lower-right.
        const qreal r = h * 0.62;
        const QPointF c(cx - h * 0.18, cy - h * 0.18);
        p.drawEllipse(c, r, r);
        const qreal d = r * 0.7071;   // 45° offset onto the circle's rim
        p.drawLine(c + QPointF(d, d), QPointF(cx + h * 0.72, cy + h * 0.72));
        break;
    }
    case Glyph::Kebab: {
        // Three vertical dots.
        p.setPen(Qt::NoPen);
        p.setBrush(m_hovered || isDown() ? kHoverIcon : kIconColor);
        const qreal r = 1.6;
        const qreal gap = h * 0.75;
        p.drawEllipse(QPointF(cx, cy - gap), r, r);
        p.drawEllipse(QPointF(cx, cy), r, r);
        p.drawEllipse(QPointF(cx, cy + gap), r, r);
        break;
    }
    case Glyph::Hamburger: {
        // Three horizontal lines.
        const qreal gap = h * 0.62;
        p.drawLine(QPointF(cx - h, cy - gap), QPointF(cx + h, cy - gap));
        p.drawLine(QPointF(cx - h, cy),       QPointF(cx + h, cy));
        p.drawLine(QPointF(cx - h, cy + gap), QPointF(cx + h, cy + gap));
        break;
    }
    case Glyph::ZoomIn:
    case Glyph::ZoomOut: {
        // Plain +/- strokes.
        const qreal s = h * 0.8;
        p.drawLine(QPointF(cx - s, cy), QPointF(cx + s, cy));
        if (m_glyph == Glyph::ZoomIn)
            p.drawLine(QPointF(cx, cy - s), QPointF(cx, cy + s));
        break;
    }
    case Glyph::FitWidth: {
        // "|<->|": two vertical bars with a double-headed arrow between them.
        p.drawLine(QPointF(cx - h, cy - h * 0.7), QPointF(cx - h, cy + h * 0.7));
        p.drawLine(QPointF(cx + h, cy - h * 0.7), QPointF(cx + h, cy + h * 0.7));
        const qreal a = h * 0.45;   // arrowhead size
        p.drawLine(QPointF(cx - h * 0.6, cy), QPointF(cx + h * 0.6, cy));
        p.drawLine(QPointF(cx - h * 0.6, cy), QPointF(cx - h * 0.6 + a, cy - a * 0.7));
        p.drawLine(QPointF(cx - h * 0.6, cy), QPointF(cx - h * 0.6 + a, cy + a * 0.7));
        p.drawLine(QPointF(cx + h * 0.6, cy), QPointF(cx + h * 0.6 - a, cy - a * 0.7));
        p.drawLine(QPointF(cx + h * 0.6, cy), QPointF(cx + h * 0.6 - a, cy + a * 0.7));
        break;
    }
    case Glyph::FitPage: {
        // Page outline with inward corner arrows: a rectangle plus diagonal
        // arrow from center toward each corner.
        const qreal w = h * 0.85;
        p.drawRect(QRectF(QPointF(cx - w, cy - h), QPointF(cx + w, cy + h)));
        const qreal a = h * 0.42;
        // Diagonal double arrow across the page.
        p.drawLine(QPointF(cx - w * 0.45, cy - h * 0.45),
                   QPointF(cx + w * 0.45, cy + h * 0.45));
        p.drawLine(QPointF(cx - w * 0.45, cy - h * 0.45),
                   QPointF(cx - w * 0.45 + a, cy - h * 0.45));
        p.drawLine(QPointF(cx - w * 0.45, cy - h * 0.45),
                   QPointF(cx - w * 0.45, cy - h * 0.45 + a));
        p.drawLine(QPointF(cx + w * 0.45, cy + h * 0.45),
                   QPointF(cx + w * 0.45 - a, cy + h * 0.45));
        p.drawLine(QPointF(cx + w * 0.45, cy + h * 0.45),
                   QPointF(cx + w * 0.45, cy + h * 0.45 - a));
        break;
    }
    case Glyph::ChevronUp: {
        p.drawLine(QPointF(cx - h * 0.8, cy + h * 0.35),
                   QPointF(cx, cy - h * 0.35));
        p.drawLine(QPointF(cx, cy - h * 0.35),
                   QPointF(cx + h * 0.8, cy + h * 0.35));
        break;
    }
    case Glyph::ChevronDown: {
        p.drawLine(QPointF(cx - h * 0.8, cy - h * 0.35),
                   QPointF(cx, cy + h * 0.35));
        p.drawLine(QPointF(cx, cy + h * 0.35),
                   QPointF(cx + h * 0.8, cy - h * 0.35));
        break;
    }
    case Glyph::ChevronLeft: {
        p.drawLine(QPointF(cx + h * 0.35, cy - h * 0.8),
                   QPointF(cx - h * 0.35, cy));
        p.drawLine(QPointF(cx - h * 0.35, cy),
                   QPointF(cx + h * 0.35, cy + h * 0.8));
        break;
    }
    case Glyph::ChevronRight: {
        p.drawLine(QPointF(cx - h * 0.35, cy - h * 0.8),
                   QPointF(cx + h * 0.35, cy));
        p.drawLine(QPointF(cx + h * 0.35, cy),
                   QPointF(cx - h * 0.35, cy + h * 0.8));
        break;
    }
    }
}

} // namespace Genesis
