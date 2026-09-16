#ifndef PINBUTTON_H
#define PINBUTTON_H

#include <QAbstractButton>
#include <QPixmap>

namespace Genesis {

// Title-bar pin toggle: shows an "opened pin" image when the window is not
// pinned, a "closed pin" image when it is. Same geometry/hover theme as
// WindowButton, but renders the raster pin icons instead of vector line art.
class PinButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit PinButton(QWidget* parent = nullptr);

    void setPinned(bool pinned);
    bool isPinned() const { return m_pinned; }

    // Scale the button (and its icon) by `scale` from the design size —
    // the title bar scales with the window height, so its controls must too.
    void setScale(qreal scale);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    // Re-scale cached icons when the widget moves to a different-DPR screen.
    bool event(QEvent* event) override;

private:
    // Pre-scale the raw resource pixmaps to the widget's current DPR (one
    // pixmap per icon) so HiDPI screens get crisp device pixels instead of a
    // hardware-stretched 1x bitmap.
    void ensureScaledIcons(qreal dpr);

    QPixmap m_sourceOpen;     // raw ":/Pin-opened.png" resource
    QPixmap m_sourceClosed;   // raw ":/Pin-closed.png" resource
    QPixmap m_openIcon;       // scaled to m_scaledDpr — drawn when pinned
    QPixmap m_closedIcon;     // scaled to m_scaledDpr — drawn when unpinned
    qreal m_scaledDpr;        // DPR the scaled icons were built for (0 = none)
    qreal m_scale;            // proportional size factor (default 1.0)
    bool m_pinned;
    bool m_hovered;
};

} // namespace Genesis

#endif // PINBUTTON_H
