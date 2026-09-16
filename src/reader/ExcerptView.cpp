#include "ExcerptView.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

namespace Genesis {

namespace {
// Keeps the text off the cell's border rules.
constexpr int kPadX = 12;
constexpr int kPadY = 8;

// The panel's standing motto, shown until a real excerpt is stored.
const char* const kMotto = "独钓寒江雪";
} // namespace

ExcerptView::ExcerptView(QWidget* parent)
    : QWidget(parent)
{
    // A hidden scroll bar is used purely as the pan value store and range
    // calculator - the same trick TrendChartWidget uses. The bar itself is
    // never painted: in a cell this narrow a 14px strip is unusable as a
    // target, and the text pans by wheel/drag instead.
    m_scroll = new QScrollBar(Qt::Horizontal, this);
    m_scroll->setVisible(false);
    connect(m_scroll, &QScrollBar::valueChanged, this,
            [this](int) { update(); });

    // The widget handles wheel/drag itself, so it must be the event target.
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
}

QString ExcerptView::displayText() const
{
    return m_excerpt.text.isEmpty() ? QString::fromUtf8(kMotto) : m_excerpt.text;
}

QColor ExcerptView::displayColor() const
{
    // The motto stands in for real content rather than prompting for it, so it
    // is drawn black like an excerpt would be, not grey like a placeholder.
    if (m_excerpt.text.isEmpty())
        return QColor(Qt::black);
    return m_excerpt.color.isValid() ? m_excerpt.color : QColor(Qt::black);
}

void ExcerptView::setExcerpt(const Excerpt& excerpt)
{
    m_excerpt = excerpt;
    m_scroll->setValue(0);
    update();
}

QRect ExcerptView::contentRect() const
{
    return rect().adjusted(kPadX, kPadY, -kPadX, -kPadY);
}

QFont ExcerptView::fontAt(int px) const
{
    QFont f;
    // CJK faces first: Qt resolves per character, so 开卷有益 comes from 楷体
    // while Latin runs fall through to Times New Roman.
    f.setFamilies({QStringLiteral("KaiTi"), QStringLiteral("STKaiti"),
                   QString::fromUtf8("楷体"), QStringLiteral("Times New Roman"),
                   QStringLiteral("serif")});
    f.setPixelSize(px);
    return f;
}

QSize ExcerptView::measure(int px, int availW) const
{
    const QFontMetrics fm(fontAt(px));
    // A zero width means "do not wrap": report the natural single-line size.
    const QRect box(0, 0, availW, 0);
    return fm.boundingRect(box, availW > 0 ? Qt::TextWordWrap : Qt::TextDontClip,
                           displayText())
        .size();
}

int ExcerptView::pickFontPx() const
{
    const QRect box = contentRect();
    const int availW = qMax(1, box.width());
    const int availH = qMax(1, box.height());

    // Walk down from the largest candidate to the smallest that fits BOTH
    // ways. The first one that fits is by definition the largest that fits, so
    // this doubles as "grow until the cell is full".
    for (int px = kMaxFontPx; px > kMinFontPx; --px) {
        const QSize need = measure(px, availW);
        if (need.width() <= availW && need.height() <= availH)
            return px;
    }
    // Nothing fit - sit at the minimum and let the text pan sideways.
    return kMinFontPx;
}

bool ExcerptView::needsPan(int fontPx) const
{
    // Pan only once the text cannot fit at the minimum size even when wrapped:
    // that is the point where shrinking has given all it can and the extra
    // width has to be reached by scrolling. Text that still fits after
    // wrapping keeps wrapping instead - it is not "too long" in the sense the
    // panel means.
    const QRect box = contentRect();
    const QSize wrapped = measure(fontPx, qMax(1, box.width()));
    return wrapped.height() > qMax(1, box.height());
}

void ExcerptView::updateScrollRange(int fontPx)
{
    const QRect box = contentRect();
    if (!needsPan(fontPx)) {
        m_scroll->setRange(0, 0);
        return;
    }
    // Panning widens the line back out to a single row, so the travel is the
    // difference between the unwrapped width and the cell.
    const QSize natural = measure(fontPx, 0);
    const int overflow = natural.width() - box.width();
    if (overflow > 0) {
        m_scroll->setRange(0, overflow);
        m_scroll->setPageStep(box.width());
        m_scroll->setSingleStep(qMax(1, box.width() / 8));
    } else {
        m_scroll->setRange(0, 0);
    }
}

void ExcerptView::paintEvent(QPaintEvent*)
{
    const QString text = displayText();
    if (text.isEmpty())
        return;

    const QRect box = contentRect();
    const int px = pickFontPx();
    const QFont f = fontAt(px);
    // Keep the pan range in step with the size actually chosen this paint.
    updateScrollRange(px);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(f);
    p.setPen(displayColor());

    const QFontMetrics fm(f);
    // Two shapes: while the text still fits it wraps inside the cell; once it
    // cannot fit at all it is laid out as one long line reached by panning.
    const bool pan = needsPan(px);
    const Qt::TextFlag flag = pan ? Qt::TextDontClip : Qt::TextWordWrap;
    const int layoutW = pan ? 0 : box.width();
    const QSize need = fm.boundingRect(QRect(0, 0, layoutW, 0), flag, text)
                           .size();

    // Vertical placement: the block of text sits at the top, middle or bottom
    // of the cell.
    int y = box.top();
    switch (m_excerpt.vAlign) {
    case VAlign::Top:    y = box.top(); break;
    case VAlign::Middle: y = box.top() + (box.height() - need.height()) / 2; break;
    case VAlign::Bottom: y = box.bottom() - need.height(); break;
    }

    // Horizontal placement: left, centred, or right within the cell. When the
    // text overflows, the pan offset is added on top of that anchor.
    int x = box.left();
    const int slack = box.width() - need.width();
    switch (m_excerpt.hAlign) {
    case HAlign::Left:   x = box.left(); break;
    case HAlign::Center: x = box.left() + slack / 2; break;
    case HAlign::Right:  x = box.left() + slack; break;
    }
    x -= m_scroll->value();

    // Draw the glyphs only: the alignment above already positions the block,
    // so no Qt::Align* flags here (they would re-anchor it and double up).
    p.drawText(QRect(x, y, need.width(), need.height()),
               flag | Qt::TextDontClip, text);
}

void ExcerptView::wheelEvent(QWheelEvent* event)
{
    // Nothing to pan when the text fits: let the event through untouched.
    if (m_scroll->maximum() <= 0) {
        event->ignore();
        return;
    }
    // A horizontal gesture pans the text; a plain vertical wheel does too,
    // because over this widget the user's intent is unambiguous.
    const int d = event->angleDelta().x() != 0 ? event->angleDelta().x()
                                               : event->angleDelta().y();
    if (d == 0) {
        event->ignore();
        return;
    }
    const int before = m_scroll->value();
    m_scroll->setValue(before - d);
    if (m_scroll->value() == before) {
        event->ignore();   // already at the end
        return;
    }
    event->accept();
}

void ExcerptView::mousePressEvent(QMouseEvent* event)
{
    if (m_scroll->maximum() > 0 && event->button() == Qt::LeftButton) {
        m_dragOrigin = event->pos();
        m_dragStartValue = m_scroll->value();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ExcerptView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragOrigin.isNull()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    // Drag the text sideways, the way a map or photo pans.
    const int dx = event->pos().x() - m_dragOrigin.x();
    m_scroll->setValue(m_dragStartValue - dx);
    event->accept();
}

void ExcerptView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragOrigin.isNull()) {
        m_dragOrigin = QPoint();
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ExcerptView::resizeEvent(QResizeEvent* event)
{
    // The cell changed size, so the fitted font size may have changed with it;
    // repaint rather than trying to track the old offset.
    m_scroll->setValue(0);
    QWidget::resizeEvent(event);
}

} // namespace Genesis
