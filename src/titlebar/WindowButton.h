#ifndef WINDOWBUTTON_H
#define WINDOWBUTTON_H

#include <QAbstractButton>

namespace Genesis {

// A frameless-window caption button that paints its glyph as vector line art
// (via QPainter) instead of a text character, so minimize / maximize / restore
// / close all share identical stroke weight, size and alignment at any DPI.
class WindowButton : public QAbstractButton {
    Q_OBJECT

public:
    enum class Glyph { Minimize, Maximize, Restore, Close };

    explicit WindowButton(Glyph glyph, QWidget* parent = nullptr);

    // Swap the drawn glyph (used to flip maximize <-> restore).
    void setGlyph(Glyph glyph);

    // Scale the button (and its glyph) by `scale` from the design size —
    // the title bar scales with the window height, so its controls must too.
    void setScale(qreal scale);

    // Round the given corners of the hover/pressed background fill — used when
    // the button sits flush against a rounded dialog corner so the highlight
    // doesn't poke past the curve.
    void setCornerRadius(qreal radius, bool topLeft, bool topRight,
                         bool bottomLeft, bool bottomRight);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    // Reused dialogs hide with the cursor still on the button — no leave
    // event ever arrives, so the hover/pressed state must be reset here.
    void hideEvent(QHideEvent* event) override;

private:
    Glyph m_glyph;
    bool m_hovered;
    qreal m_scale;

    qreal m_radius;          // corner radius of the background fill (0 = square)
    bool m_roundTL, m_roundTR, m_roundBL, m_roundBR;
};

} // namespace Genesis

#endif // WINDOWBUTTON_H
