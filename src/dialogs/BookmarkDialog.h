#ifndef BOOKMARKDIALOG_H
#define BOOKMARKDIALOG_H

#include "../library/BookmarkStore.h"

#include "FramelessDialog.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;

namespace Genesis {

class BookmarkRowDelegate;

// Bookmark browser opened from the title-bar 书签 menu entry. Two levels
// inside one dialog:
//   page 0 — groups: one row per PDF (auto-named from the book, renamable).
//            选择 (becomes 取消 while active) toggles selection mode: each
//            row shows the app-wide dot (hollow black / filled green);
//            全选 / 重命名 / 删除 appear alongside. The bookmark count sits
//            in a rounded badge at the row's right edge.
//   page 1 — bookmarks of the chosen book. 返回 / 选择 on the left; drag
//            reorder is always armed (persisted immediately), 选择 mirrors
//            the group page's selection mode incl. 全选. The page number
//            and creation time sit in rounded badges; the group name
//            (elided) and a 单击拖拽排序/双击跳转 hint live at the bottom.
class BookmarkDialog : public FramelessDialog {
    Q_OBJECT

public:
    explicit BookmarkDialog(QWidget* parent = nullptr);

signals:
    // User activated a bookmark: open bookPath at page (0-based).
    void bookmarkActivated(const QString& bookPath, int page);

private:
    void showGroups();
    void showBook(const QString& bookPath);
    void persistOrder();   // rewrite the store from the current list order

    // Selection-mode toggles (one per page).
    void setGroupSelectionMode(bool on);
    void setBookSelectionMode(bool on);

    // Selected (dot-checked) rows of a list.
    QList<QListWidgetItem*> checkedItems(QListWidget* list) const;
    // 全选 with toggle semantics: if every row is checked, uncheck all.
    void toggleCheckAll(QListWidget* list);

    // Header actions.
    void renameCheckedGroup();
    void deleteCheckedGroups();
    void renameCheckedBookmark();
    void deleteCheckedBookmarks();

    // Themed one-line text prompt; returns trimmed text or null on cancel.
    QString promptText(const QString& title, const QString& hint,
                       const QString& initial, bool* ok);

    QStackedWidget* m_stack;

    // page 0
    QListWidget* m_groupList;
    BookmarkRowDelegate* m_groupDelegate;
    QPushButton* m_groupSelectBtn;
    QPushButton* m_groupAllBtn;
    QPushButton* m_groupRenameBtn;
    QPushButton* m_groupDeleteBtn;
    bool m_groupSelMode;

    // page 1
    QListWidget* m_bookmarkList;
    BookmarkRowDelegate* m_bookmarkDelegate;
    QPushButton* m_backButton;
    QPushButton* m_bookSelectBtn;
    QPushButton* m_bookAllBtn;
    QPushButton* m_bmRenameBtn;
    QPushButton* m_bmDeleteBtn;
    QLabel* m_bookTitle;        // bottom-left: elided group name
    bool m_bookSelMode;

    QString m_currentBook;
};

} // namespace Genesis

#endif // BOOKMARKDIALOG_H
