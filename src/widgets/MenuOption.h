#ifndef MENUOPTION_H
#define MENUOPTION_H

#include <QWidget>

#include <functional>

class QPainter;

namespace Genesis {

// Custom-drawn menu row for QWidgetAction: "文字 ●" — text on the left, then
// a one-character gap, then (when active) a centered green dot, with no
// reserved icon column and no trailing blank area. This is the app-wide look
// for toggle/choice menu entries (sort modes, view switches...). QMenu can't
// right-place icons, hence the custom widget.
class MenuOption : public QWidget {
public:
    MenuOption(const QString& text, bool active, std::function<void()> onClick);

    // Split layout: the dot sits BETWEEN the two text segments, e.g.
    // "双页 ● 视图" — used by the reader's double-page toggle.
    MenuOption(const QString& textBefore, const QString& textAfter,
               bool active, std::function<void()> onClick);

    QSize sizeHint() const override;

    // The app-wide green toggle dot. Shared by menu toggles and the
    // bookshelf's selection badge — reuse this instead of redrawing it.
    // active = filled dot; inactive = hollow outline (subtle placeholder).
    static constexpr qreal kDotRadius = 4.0;
    static void drawDot(QPainter& p, const QPointF& center, bool active);
    static void drawActiveDot(QPainter& p, const QPointF& center);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    static constexpr int kPad = 12;

    QString m_text;
    QString m_textAfter;   // non-empty = dot-in-the-middle layout
    bool m_active;
    bool m_hovered;
    std::function<void()> m_onClick;
};

} // namespace Genesis

#endif // MENUOPTION_H
