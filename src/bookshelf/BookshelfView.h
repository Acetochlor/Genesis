#ifndef BOOKSHELFVIEW_H
#define BOOKSHELFVIEW_H

#include "../library/Library.h"

#include <QWidget>
#include <QVector>

class QLineEdit;
class QPushButton;

namespace Genesis {

class IconButton;
class BookCardWidget;

// The bookshelf (part3 of the main window). Top-right toolbar hosts a search
// icon (expands an inline filter box) and a kebab menu (refresh / selection
// mode with select-all + delete / sort by name or time). Below, books are
// shown as a grid of BookCardWidget (PDF cover, title, last-opened date,
// read duration). Transparent background so the main window's background
// image shows through.
class BookshelfView : public QWidget {
    Q_OBJECT

public:
    explicit BookshelfView(QWidget* parent = nullptr);

    // Re-scan the library directory and rebuild the book cards.
    void refresh();

signals:
    void bookActivated(const QString& path);   // double-clicked a book (open it)

private:
    void buildToolbar();
    void toggleSearch();
    void showKebabMenu();
    void applyFilter(const QString& text);

    // Selection mode: checkboxes on covers, plus the select-all / delete /
    // cancel action row.
    void enterSelectionMode();
    void leaveSelectionMode();
    void deleteSelected();

    QLineEdit* m_searchBox;
    IconButton* m_searchButton;
    IconButton* m_kebabButton;
    QWidget* m_selectionBar;
    QPushButton* m_selectAll;
    QWidget* m_gridContainer;          // scroll + grid of BookCardWidgets
    QVector<BookCardWidget*> m_cards;
    QSet<QString> m_selectedPaths;     // paths selected in selection mode

    Library::SortMode m_sortMode;
    bool m_selectionMode;
};

} // namespace Genesis

#endif // BOOKSHELFVIEW_H
