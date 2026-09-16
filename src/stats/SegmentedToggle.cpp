#include "SegmentedToggle.h"

#include <QMouseEvent>
#include <QPainter>

#include "../settings/Theme.h"

namespace Genesis {

namespace {
const QColor kAccent("#33CCFF");
const QColor kHoverBg("#E6F7FF");

constexpr int kPadX = 10;     // gap between items
constexpr int kHeight = 26;
} // namespace

SegmentedToggle::SegmentedToggle(const QStringList& items, QWidget* parent)
    : QWidget(parent)
    , m_items(items)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);
    // Label colours come from the shelf text theme; repaint when it changes.
    connect(&Theme::instance(), &Theme::changed, this,
            [this]() { update(); });
}

void SegmentedToggle::setCurrentIndex(int index)
{
    index = qBound(0, index, m_items.size() - 1);
    if (index == m_index)
        return;
    m_index = index;
    update();
}

QSize SegmentedToggle::sizeHint() const
{
    const QFontMetrics fm(font());
    int w = 0;
    for (const QString& s : m_items)
        w += fm.horizontalAdvance(s) + 2 * kPadX;
    return QSize(w, kHeight);
}

QRect SegmentedToggle::itemRect(int i) const
{
    const QFontMetrics fm(font());
    int x = 0;
    for (int k = 0; k < i; ++k)
        x += fm.horizontalAdvance(m_items.at(k)) + 2 * kPadX;
    return QRect(x, 0, fm.horizontalAdvance(m_items.at(i)) + 2 * kPadX,
                 height());
}

void SegmentedToggle::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < m_items.size(); ++i) {
        const QRect r = itemRect(i);
        if (i == m_index) {
            // Selection: accent pill, white label.
            p.setPen(Qt::NoPen);
            p.setBrush(kAccent);
            p.drawRoundedRect(r.adjusted(1, 2, -1, -2), 6, 6);
            p.setPen(Qt::white);
        } else {
            if (i == m_hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(kHoverBg);
                p.drawRoundedRect(r.adjusted(1, 2, -1, -2), 6, 6);
            }
            p.setPen(i == m_hover ? Theme::instance().text()
                       : Theme::instance().mutedText());
        }
        p.drawText(r, Qt::AlignCenter, m_items.at(i));
    }
}

void SegmentedToggle::mousePressEvent(QMouseEvent* event)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (itemRect(i).contains(event->pos())) {
            if (i != m_index) {
                m_index = i;
                update();
                emit changed(i);
            }
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void SegmentedToggle::mouseMoveEvent(QMouseEvent* event)
{
    int hover = -1;
    for (int i = 0; i < m_items.size(); ++i)
        if (itemRect(i).contains(event->pos())) {
            hover = i;
            break;
        }
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void SegmentedToggle::leaveEvent(QEvent* event)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

} // namespace Genesis
