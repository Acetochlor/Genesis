#ifndef ICONBUTTON_H
#define ICONBUTTON_H

#include <QAbstractButton>

namespace Genesis {

// Small flat toolbar button that paints its icon as vector line art (like
// WindowButton) so all toolbar glyphs share stroke weight at any DPI. Rounded
// hover fill, theme colors.
class IconButton : public QAbstractButton {
    Q_OBJECT

public:
    enum class Glyph {
        Search,       // magnifier
        Kebab,        // three vertical dots
        Hamburger,    // three horizontal lines (TOC drawer)
        ZoomIn,       // plain +
        ZoomOut,      // plain -
        FitWidth,     // horizontal double arrow between bars
        FitPage,      // page outline with diagonal arrows
        ChevronUp,    // collapse the control bar
        ChevronDown,  // reveal the control bar
        ChevronLeft,  // previous page
        ChevronRight  // next page
    };

    explicit IconButton(Glyph glyph, QWidget* parent = nullptr);

    // Swap the drawn glyph (used for fit-width <-> fit-page toggling).
    void setGlyph(Glyph glyph);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    Glyph m_glyph;
    bool m_hovered;
};

} // namespace Genesis

#endif // ICONBUTTON_H
