#ifndef EXCERPTVIEW_H
#define EXCERPTVIEW_H

#include "../library/ExcerptStore.h"

#include <QString>
#include <QWidget>

class QScrollBar;

namespace Genesis {

// The book excerpt drawn in the shelf page's top-left cell (part1).
//
// Written as a custom widget rather than a QLabel because the text has to fit
// itself to whatever the cell currently is: the font is measured against the
// available box on every paint and picked to fill it, and when even the
// smallest size cannot fit the text it becomes horizontally pannable instead
// of being clipped. A QLabel's size model cannot express either.
//
// The face matches the about box's 独钓寒江雪 so the two read as one voice.
class ExcerptView : public QWidget {
    Q_OBJECT

public:
    explicit ExcerptView(QWidget* parent = nullptr);

    // Replace the displayed excerpt. An empty text falls back to the panel's
    // standing motto, so the cell never reads as blank.
    void setExcerpt(const Excerpt& excerpt);

    // Smallest size the text is allowed to shrink to. Below this the text
    // stops shrinking and starts panning instead.
    static constexpr int kMinFontPx = 14;
    // Largest size the text will grow to, however much room the cell has.
    static constexpr int kMaxFontPx = 26;

protected:
    void paintEvent(QPaintEvent* event) override;
    // Pan the text when it is too wide for the cell: the wheel scrolls it and
    // a left-drag moves it like a map. Both mirror TrendChartWidget, which
    // solves the same problem in the stats panel.
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // The text actually drawn: the excerpt, or the motto when it is empty.
    QString displayText() const;
    QColor displayColor() const;

    // Box the text is laid out in, inside the widget's padding.
    QRect contentRect() const;

    // The face at `px`, with the CJK stack first so Chinese still comes from
    // 楷体 and only Latin runs fall through to Times New Roman.
    QFont fontAt(int px) const;

    // Size the text needs at `px`, wrapped to `availW` (0 = no wrap needed).
    QSize measure(int px, int availW) const;

    // Pick the font size for the current content box: the largest size that
    // still fits, clamped to [kMinFontPx, kMaxFontPx].
    int pickFontPx() const;

    // True when the text cannot fit the cell at `fontPx` even wrapped, i.e.
    // the point where it is panned instead of wrapped further.
    bool needsPan(int fontPx) const;

    // Recompute the pan range for the chosen size and show/hide accordingly.
    void updateScrollRange(int fontPx);

    Excerpt m_excerpt;
    QScrollBar* m_scroll = nullptr;   // value store; never shown
    // Drag-to-pan state; a null origin means no drag is in progress.
    QPoint m_dragOrigin;
    int m_dragStartValue = 0;
};

} // namespace Genesis

#endif // EXCERPTVIEW_H
