#ifndef EXCERPTDIALOG_H
#define EXCERPTDIALOG_H

#include "FramelessDialog.h"

#include "../library/ExcerptStore.h"

#include <QColor>
#include <QList>

class QPlainTextEdit;

namespace Genesis {

class SegmentedToggle;

// Add/edit the app's single book excerpt (更多 → 书摘). A text area plus six
// colour swatches, two alignment strips, and 确定 / 取消. Editing an existing
// excerpt pre-fills all of them.
class ExcerptDialog : public FramelessDialog {
    Q_OBJECT

public:
    // Existing text/colour/alignment seed the editor; pass an empty text for
    // "add".
    ExcerptDialog(const QString& text, const QColor& color, HAlign hAlign,
                  VAlign vAlign, QWidget* parent = nullptr);

    QString text() const;
    QColor color() const { return m_color; }
    HAlign hAlign() const;
    VAlign vAlign() const;

protected:
    // Picks the colour when a swatch is clicked (the swatches are plain
    // QWidgets, so they are watched rather than subclassed with signals).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Repaint the swatch row so the chosen colour is visibly selected.
    void refreshSwatches();

    QPlainTextEdit* m_edit = nullptr;
    QList<QWidget*> m_swatches;
    QColor m_color;
    SegmentedToggle* m_hAlign = nullptr;
    SegmentedToggle* m_vAlign = nullptr;
};

} // namespace Genesis

#endif // EXCERPTDIALOG_H
