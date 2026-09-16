#ifndef SEGMENTEDTOGGLE_H
#define SEGMENTEDTOGGLE_H

#include <QStringList>
#include <QWidget>

namespace Genesis {

// A horizontal, always-visible "pick one of N" control: the items laid out in a
// single strip with the current one on an accent pill. Used for the stats
// panel's 月/年/总 range selector and its 阅读时长/阅读次数 metric switch.
//
// Deliberately NOT MenuOption: that one is a QMenu ROW (sized for a
// QWidgetAction, paints an on/off green dot, has no notion of a selection), so
// reusing it here would draw a dot meaning "enabled" where this needs "which
// one of three".
class SegmentedToggle : public QWidget {
    Q_OBJECT

public:
    explicit SegmentedToggle(const QStringList& items, QWidget* parent = nullptr);

    int currentIndex() const { return m_index; }
    // Set the selection without emitting changed().
    void setCurrentIndex(int index);

    QSize sizeHint() const override;

signals:
    void changed(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Rect of item `i` in local coordinates.
    QRect itemRect(int i) const;

    QStringList m_items;
    int m_index = 0;
    int m_hover = -1;
};

} // namespace Genesis

#endif // SEGMENTEDTOGGLE_H
