#include "TitleMenuItem.h"

#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

namespace Genesis {

namespace {
constexpr int kDesignHeight = 36;   // bar height the design size assumes
} // namespace

TitleMenuItem::TitleMenuItem(const QString& text, QWidget* parent)
    : QWidget(parent)
    , m_text(text)
    , m_onClick(nullptr)
    , m_menu(nullptr)
    , m_hovered(false)
    , m_scale(1.0)
{
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

void TitleMenuItem::setOnClick(std::function<void()> onClick)
{
    m_onClick = std::move(onClick);
    m_menu = nullptr;
}

void TitleMenuItem::setMenu(QMenu* menu)
{
    m_menu = menu;
    m_onClick = nullptr;
}

void TitleMenuItem::setScale(qreal scale)
{
    if (qFuzzyCompare(m_scale, scale))
        return;
    m_scale = scale;
    updateGeometry();
}

QSize TitleMenuItem::sizeHint() const
{
    const QFontMetrics fm = fontMetrics();
    const int fontPx = qRound(13 * m_scale);
    const int padH = qRound(14 * m_scale);
    const int padV = qRound(5 * m_scale);
    // Measure at the scaled font.
    QFont f = font();
    f.setPixelSize(fontPx);
    const int textW = QFontMetrics(f).horizontalAdvance(m_text);
    return QSize(textW + 2 * padH, kDesignHeight);
}

void TitleMenuItem::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (m_hovered)
        p.fillRect(rect(), QColor("#E6F7FF"));

    QFont f = font();
    f.setPixelSize(qRound(13 * m_scale));
    p.setFont(f);
    // Resting item text is the body colour; only hover picks up the accent.
    // (Both branches used to be the accent, so an idle title-bar item rendered
    // in cyan and never looked unfocused.)
    p.setPen(m_hovered ? QColor("#33CCFF") : QColor("#333333"));
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

void TitleMenuItem::enterEvent(QEnterEvent*)
{
    m_hovered = true;
    update();
}

void TitleMenuItem::leaveEvent(QEvent*)
{
    m_hovered = false;
    update();
}

void TitleMenuItem::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !rect().contains(e->pos()))
        return;
    if (m_menu) {
        m_menu->popup(mapToGlobal(QPoint(0, height())));
    } else if (m_onClick) {
        m_onClick();
    }
}

} // namespace Genesis
