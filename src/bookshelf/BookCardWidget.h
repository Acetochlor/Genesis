#ifndef BOOKCARDWIDGET_H
#define BOOKCARDWIDGET_H

#include "../library/BookInfo.h"

#include <QWidget>
#include <QPixmap>
#include <QString>

class QLabel;

namespace Genesis {

// A single book card in the bookshelf grid, arranged left-to-right top-to-bottom:
//   [PDF cover thumbnail — high-res, rendered at 2× and downscaled for crispness]
//   [title — up to 2 lines, word-wrapped, ellipsed]
//   [last-opened — relative: "x分钟前" / "x小时前" / "x天前" / "yyyy-MM-dd"]
//   [read duration — "未读" / "x分" / "x时x分"]
class BookCardWidget : public QWidget {
    Q_OBJECT

public:
    // path = absolute file path on the shelf; info carries metadata.
    explicit BookCardWidget(const QString& path, const BookInfo& info,
                            QWidget* parent = nullptr);

    QString filePath() const { return m_path; }
    void updateInfo(const BookInfo& info);

    // Selection mode: every cover shows a dot at its top-right corner —
    // hollow outline when unselected, filled green when selected.
    void setSelectionMode(bool on) { m_selectionMode = on; update(); }
    void setSelected(bool selected) { m_selected = selected; update(); }
    bool isSelected() const { return m_selected; }

signals:
    void clicked(const QString& path);      // single click (selection toggle)
    void activated(const QString& path);    // double click (open the book)

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    // Let the grid or parent set a different size on reflow.
    bool hasHeightForWidth() const override { return false; }

private:
    // Ensure the cached thumbnail exists and return it (lazy on first call).
    QPixmap ensureThumbnail();
    // Relative time string for lastOpened.
    static QString relativeTime(const QDateTime& dt);
    // Re-colour the three labels from the current shelf text theme.
    void applyTextTheme();

    static constexpr int kCardWidth  = 140;
    static constexpr int kThumbW     = 120;
    static constexpr int kThumbH     = 160;

    QString m_path;
    QPixmap m_thumbnail;
    QLabel* m_title;
    QLabel* m_lastOpened;
    QLabel* m_readTime;
    BookInfo m_info;
    bool m_selectionMode;   // dot slot shown on every cover corner
    bool m_selected;        // filled (vs hollow) state of the dot
    bool m_hovered;
};

} // namespace Genesis

#endif // BOOKCARDWIDGET_H
