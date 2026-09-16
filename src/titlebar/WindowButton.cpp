#include "WindowButton.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QEnterEvent>
#include <QFontDatabase>

namespace Genesis {

namespace {

// Button geometry and colour theme (matches the #33CCFF title-bar accent).
constexpr int kButtonWidth  = 44;
constexpr int kButtonHeight = 34;
constexpr int kGlyphSize    = 10;   // side length of the drawn glyph, in px

const QColor kIconColor("#5A5A5A");            // resting stroke — neutral, subtle
const QColor kHoverBg("#E6F7FF");              // light-blue hover for min/max
const QColor kHoverIcon("#1A1A1A");            // darker stroke on hover
const QColor kCloseHoverBg("#E81123");         // red hover for close
const QColor kCloseHoverIcon("#FFFFFF");       // white cross on red

} // namespace

WindowButton::WindowButton(Glyph glyph, QWidget* parent)
    : QAbstractButton(parent)
    , m_glyph(glyph)
    , m_hovered(false)
    , m_scale(1.0)
    , m_radius(0)
    , m_roundTL(false)
    , m_roundTR(false)
    , m_roundBL(false)
    , m_roundBR(false)
{
    setFixedSize(kButtonWidth, kButtonHeight);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void WindowButton::setScale(qreal scale)
{
    if (qFuzzyCompare(m_scale, scale))
        return;
    m_scale = scale;
    setFixedSize(qRound(kButtonWidth * scale), qRound(kButtonHeight * scale));
    update();
}

void WindowButton::setGlyph(Glyph glyph)
{
    if (m_glyph == glyph)
        return;
    m_glyph = glyph;
    update();
}

void WindowButton::setCornerRadius(qreal radius, bool topLeft, bool topRight,
                                   bool bottomLeft, bool bottomRight)
{
    m_radius  = radius;
    m_roundTL = topLeft;
    m_roundTR = topRight;
    m_roundBL = bottomLeft;
    m_roundBR = bottomRight;
    update();
}

void WindowButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void WindowButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void WindowButton::hideEvent(QHideEvent* event)
{
    // A reused dialog (e.g. 关于) closes via this button while the cursor is
    // still over it: Qt delivers no Leave to a hidden widget, so the red
    // hover fill would still be painted on the NEXT show. Clear it here.
    m_hovered = false;
    setDown(false);
    QAbstractButton::hideEvent(event);
}

void WindowButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);

    const bool isClose = (m_glyph == Glyph::Close);

    // --- hover / pressed background ---
    if (m_hovered || isDown()) {
        QColor bg = isClose ? kCloseHoverBg : kHoverBg;
        if (isDown())
            bg = bg.darker(112);
        if (m_radius > 0 && (m_roundTL || m_roundTR || m_roundBL || m_roundBR)) {
            // Fill with selected corners rounded (so the highlight follows a
            // rounded dialog corner). Path: rounded rect, then square off the
            // corners that must stay sharp.
            p.setRenderHint(QPainter::Antialiasing, true);
            QPainterPath path;
            path.addRoundedRect(rect(), m_radius, m_radius);
            const qreal r2 = m_radius;
            if (!m_roundTL) path.addRect(QRectF(0, 0, r2, r2));
            if (!m_roundTR) path.addRect(QRectF(width() - r2, 0, r2, r2));
            if (!m_roundBL) path.addRect(QRectF(0, height() - r2, r2, r2));
            if (!m_roundBR) path.addRect(QRectF(width() - r2, height() - r2, r2, r2));
            path.setFillRule(Qt::WindingFill);
            p.fillPath(path.simplified(), bg);
            p.setRenderHint(QPainter::Antialiasing, false);
        } else {
            p.fillRect(rect(), bg);
        }
    }

    // --- glyph stroke colour ---
    QColor stroke = kIconColor;
    if (m_hovered || isDown())
        stroke = isClose ? kCloseHoverIcon : kHoverIcon;

    // Use the Windows caption-glyph font (what native title bars use). These
    // glyphs are hand-hinted for pixel-perfect symmetry at small sizes —
    // hand-drawn antialiased diagonals never quite match them.
    //   E921 ChromeMinimize, E922 ChromeMaximize,
    //   E923 ChromeRestore,  E8BB ChromeClose
    static const bool hasSegoe =
        QFontDatabase::families().contains(QStringLiteral("Segoe MDL2 Assets"));

    if (hasSegoe) {
        QChar glyph;
        switch (m_glyph) {
        case Glyph::Minimize: glyph = QChar(0xE921); break;
        case Glyph::Maximize: glyph = QChar(0xE922); break;
        case Glyph::Restore:  glyph = QChar(0xE923); break;
        case Glyph::Close:    glyph = QChar(0xE8BB); break;
        }
        QFont f(QStringLiteral("Segoe MDL2 Assets"));
        f.setPixelSize(qRound(kGlyphSize * m_scale));
        p.setFont(f);
        p.setPen(stroke);
        p.drawText(rect(), Qt::AlignCenter, QString(glyph));
        return;
    }

    // --- fallback: vector-drawn glyphs (non-Windows / font missing) ---
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(stroke, 1.0 * m_scale);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);

    const qreal cx = width() / 2.0;
    const qreal cy = height() / 2.0;
    const qreal h = kGlyphSize * m_scale / 2.0;
    const qreal l = cx - h, r = cx + h, t = cy - h, b = cy + h;

    switch (m_glyph) {
    case Glyph::Minimize:
        p.drawLine(QPointF(l, cy), QPointF(r, cy));
        break;
    case Glyph::Maximize:
        p.drawRect(QRectF(QPointF(l, t), QPointF(r, b)));
        break;
    case Glyph::Restore: {
        const qreal off = 2.5 * m_scale;
        p.drawLine(QPointF(l + off, t), QPointF(r, t));
        p.drawLine(QPointF(r, t), QPointF(r, b - off));
        p.drawRect(QRectF(QPointF(l, t + off), QPointF(r - off, b)));
        break;
    }
    case Glyph::Close:
        p.drawLine(QPointF(l, t), QPointF(r, b));
        p.drawLine(QPointF(r, t), QPointF(l, b));
        break;
    }
}

} // namespace Genesis
