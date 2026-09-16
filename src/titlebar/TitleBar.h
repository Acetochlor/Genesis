#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>
#include <QVector>

class QLabel;
class QMenuBar;
class QHBoxLayout;

namespace Genesis {

class WindowButton;
class PinButton;
class TitleMenuItem;

// A self-drawn title bar for the frameless main window: app icon + menu strip
// on the left, minimize / maximize-restore / close buttons on the right.
// Dragging the bar moves the window; double-clicking toggles maximize.
// Colours follow the menu-bar theme (white background, #33CCFF accents). The
// caption buttons are vector-drawn (WindowButton); the menu strip items are
// custom-drawn (TitleMenuItem) so everything scales with the window.
class TitleBar : public QWidget {
    Q_OBJECT

public:
    // `topLevel` is the window this bar controls (moved / min / max / closed).
    explicit TitleBar(QWidget* topLevel, QWidget* parent = nullptr);

    // Insert the application menu strip right after the icon.
    void setMenuBar(QWidget* menuBar);

    // Swap the middle button's glyph/tooltip between maximize and restore.
    void updateMaximizeButton(bool maximized);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void toggleMaximize();
    // Title-bar height = a fixed percentage of the window height, anchored
    // at 30px in fullscreen (30/1080). Scales all inner controls to match.
    void updateBarHeight();

    QWidget* m_win;          // the top-level window being controlled
    QHBoxLayout* m_layout;   // main row layout (icon | menu | stretch | buttons)
    QVector<TitleMenuItem*> m_menuItems;   // its items, scaled individually
    QLabel* m_iconLabel;
    PinButton* m_pinButton;  // always-on-top toggle
    WindowButton* m_minButton;
    WindowButton* m_maxButton;
    WindowButton* m_closeButton;
    qreal m_scale;           // last applied scale (1.0 = design size)

    bool m_dragging;         // true while the bar is being dragged to move
    QPoint m_dragStartGlobal;
    QPoint m_winStartPos;
};

} // namespace Genesis

#endif // TITLEBAR_H
