#ifndef FRAMELESSDIALOG_H
#define FRAMELESSDIALOG_H

#include <QDialog>

class QVBoxLayout;

namespace Genesis {

// A frameless dialog with a self-drawn title bar (title text + close button),
// matching the app theme (white background, #33CCFF accents, 1px border).
// Dragging the title bar moves the dialog. Use contentLayout() to fill the
// area below the title bar, or the static information()/question() helpers
// as themed replacements for QMessageBox.
class FramelessDialog : public QDialog {
    Q_OBJECT

public:
    explicit FramelessDialog(const QString& title, QWidget* parent = nullptr);

    // Chrome style: Titled shows the normal title bar (text + separator);
    // CloseOnly hides the title text and separator, keeping just the close
    // button floating at the top-right corner.
    enum class Chrome { Titled, CloseOnly };
    FramelessDialog(const QString& title, Chrome chrome, QWidget* parent);

    // Layout for the dialog body (below the title bar).
    QVBoxLayout* contentLayout() const { return m_contentLayout; }

    // Pin this dialog to its parent window: it stays centered over it (with
    // an optional vertical offset), follows the parent's moves/resizes, and
    // can no longer be dragged. Its own size is never touched.
    //
    // When wRatio/hRatio (> 0) are given, the size instead tracks the parent
    // LIVE: dialog = parent current size × (wRatio, hRatio) minus bottomPad
    // px, recomputed on every parent resize. leftAligned anchors such a
    // dialog flush-left instead of centered (Markdown editor only).
    void followParent(const QPoint& offset = QPoint(),
                      qreal wRatio = 0, qreal hRatio = 0,
                      int bottomPad = 0, bool leftAligned = false);

    // Open this dialog as THE app's active standalone dialog: any other
    // standalone dialog (one opened with showStandalone) is closed first, so
    // only one is ever open. Modal message boxes are unaffected.
    void showStandalone();

    // Themed message boxes (modal). question() returns true when confirmed.
    static void information(QWidget* parent, const QString& title,
                            const QString& text);
    static bool question(QWidget* parent, const QString& title,
                         const QString& text);

protected:
    // Drag-to-move via the title-bar area; center over parent on first show.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    // Pinned dialogs follow the parent window's moves/resizes.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void centerOverParent();
    // Re-center over the parent's current geometry.
    void updateFollow();

    QVBoxLayout* m_contentLayout;
    bool m_dragging;
    QPoint m_dragStartGlobal;
    QPoint m_startPos;

    // Follow state (only when followParent() was called).
    QWidget* m_followParent;
    QPoint m_followOffset;
    qreal m_ratioW;          // > 0: size tracks parent × ratio (live)
    qreal m_ratioH;
    int m_bottomPad;         // px subtracted from the height
    bool m_leftAligned;      // anchor left instead of centering
    bool m_followActive;
};

} // namespace Genesis

#endif // FRAMELESSDIALOG_H
