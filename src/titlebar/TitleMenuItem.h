#ifndef TITLEMENUITEM_H
#define TITLEMENUITEM_H

#include <QWidget>

#include <functional>

class QMenu;

namespace Genesis {

// One entry of the title-bar menu strip (书架 / 更多 / 设置 / 关于).
// Custom-drawn so it scales continuously with the window — a native QMenuBar
// would fold its items into an overflow kebab once the window gets narrow,
// which cannot be disabled.
class TitleMenuItem : public QWidget {
    Q_OBJECT

public:
    explicit TitleMenuItem(const QString& text, QWidget* parent = nullptr);

    // Exactly one of the two behaviors is set:
    void setOnClick(std::function<void()> onClick);
    void setMenu(QMenu* menu);   // popup below the item on click

    void setScale(qreal scale);  // font + padding scaled from the design size

protected:
    QSize sizeHint() const override;
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QString m_text;
    std::function<void()> m_onClick;
    QMenu* m_menu;
    bool m_hovered;
    qreal m_scale;
};

} // namespace Genesis

#endif // TITLEMENUITEM_H
