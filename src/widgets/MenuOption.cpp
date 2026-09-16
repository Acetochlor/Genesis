#include "MenuOption.h"

#include <QPainter>
#include <QMouseEvent>

namespace Genesis {

MenuOption::MenuOption(const QString& text, bool active,
                       std::function<void()> onClick)
    : m_text(text)
    , m_active(active)
    , m_hovered(false)
    , m_onClick(std::move(onClick))
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

MenuOption::MenuOption(const QString& textBefore, const QString& textAfter,
                       bool active, std::function<void()> onClick)
    : m_text(textBefore)
    , m_textAfter(textAfter)
    , m_active(active)
    , m_onClick(std::move(onClick))
{
    m_hovered = false;
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

QSize MenuOption::sizeHint() const
{
    const QFontMetrics fm = fontMetrics();
    const int gap = fm.horizontalAdvance(QString::fromUtf8("字"));
    if (!m_textAfter.isEmpty()) {
        // padding + before + gap + dot + gap + after + padding
        return QSize(kPad + fm.horizontalAdvance(m_text) + gap
                         + 2 * kDotRadius + gap
                         + fm.horizontalAdvance(m_textAfter) + kPad,
                     fm.height() + 12);
    }
    // padding + text + one-char gap + dot + padding
    return QSize(kPad + fm.horizontalAdvance(m_text) + gap
                     + 2 * kDotRadius + kPad,
                 fm.height() + 12);
}

void MenuOption::drawDot(QPainter& p, const QPointF& center, bool active)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (active) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2ECC71"));
        p.drawEllipse(center, kDotRadius, kDotRadius);
    } else {
        // Hollow slot: black outline, slightly larger than the filled dot so
        // the two states read as one control without competing visually.
        QPen pen(QColor("#333333"), 1.2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal r = kDotRadius + 0.5 - pen.widthF() / 2.0;
        p.drawEllipse(center, r, r);
    }
    p.restore();
}

void MenuOption::drawActiveDot(QPainter& p, const QPointF& center)
{
    drawDot(p, center, true);
}

void MenuOption::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (m_hovered)
        p.fillRect(rect(), QColor("#E6F7FF"));

    p.setPen(m_hovered ? QColor("#33CCFF") : QColor("#333333"));
    const QFontMetrics fm = fontMetrics();
    const int gap = fm.horizontalAdvance(QString::fromUtf8("字"));
    p.drawText(QRect(kPad, 0, fm.horizontalAdvance(m_text), height()),
               Qt::AlignVCenter | Qt::AlignLeft, m_text);

    // Dot slot: filled when active, hollow outline when not.
    const qreal cx = kPad + fm.horizontalAdvance(m_text) + gap
                     + kDotRadius;
    drawDot(p, QPointF(cx, height() / 2.0), m_active);

    if (!m_textAfter.isEmpty()) {
        // Second text segment after the dot: "双页 ● 视图".
        p.setPen(m_hovered ? QColor("#33CCFF") : QColor("#333333"));
        const int afterX = qRound(cx + kDotRadius + gap);
        p.drawText(QRect(afterX, 0, fm.horizontalAdvance(m_textAfter),
                         height()),
                   Qt::AlignVCenter | Qt::AlignLeft, m_textAfter);
    }
}

void MenuOption::enterEvent(QEnterEvent*)
{
    m_hovered = true;
    update();
}

void MenuOption::leaveEvent(QEvent*)
{
    m_hovered = false;
    update();
}

void MenuOption::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
        m_onClick();
}

} // namespace Genesis
