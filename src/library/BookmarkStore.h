#ifndef BOOKMARKSTORE_H
#define BOOKMARKSTORE_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

namespace Genesis {

// One reader bookmark: a named position (0-based page) inside a PDF.
struct Bookmark {
    QString bookPath;       // absolute path of the PDF this bookmark is in
    QString name;           // user-given display name
    int page;               // 0-based page number
    QDateTime createdAt;    // creation time (default in-group ordering)
};

// Persistent store for reader bookmarks, saved as one JSON file in the app
// data directory. Bookmarks are kept in a flat list; the bookmark dialog
// groups them by book. The list order within a book IS the display order —
// manual drag reordering rewrites it.
class BookmarkStore {
public:
    // All bookmarks of one book, in stored (display) order.
    static QList<Bookmark> forBook(const QString& bookPath);

    // All bookmarks, grouped by bookPath, preserving stored order per book.
    static QList<Bookmark> loadAll();

    // Append one bookmark (kept at the end of its book's group).
    static void add(const Bookmark& bookmark);

    // Remove a single bookmark (matched by book, page and name).
    static void remove(const Bookmark& bookmark);

    // Replace ALL bookmarks of one book with the given ordered list
    // (used by drag reordering; other books' entries are untouched).
    static void setForBook(const QString& bookPath,
                           const QList<Bookmark>& bookmarks);

    // Drop every bookmark whose book no longer exists on the shelf.
    static void purgeMissingBooks();

    // Group display name: user-renamable, defaults to the PDF's base name.
    static QString groupName(const QString& bookPath);
    static void setGroupName(const QString& bookPath, const QString& name);

private:
    static QString filePath();
    static QList<Bookmark> readAll();
    static void writeAll(const QList<Bookmark>& bookmarks);
    // Custom group names keyed by bookPath (only renamed ones are stored).
    static QHash<QString, QString> readGroupNames();
    static void writeGroupNames(const QHash<QString, QString>& names);
};

} // namespace Genesis

#endif // BOOKMARKSTORE_H
