#ifndef LIBRARY_H
#define LIBRARY_H

#include <QString>
#include <QStringList>

namespace Genesis {

// The on-disk book library backing the bookshelf ("书架"). Imported books are
// copied into a per-user library directory, so the shelf keeps working even if
// the original files are moved or deleted. The shelf's contents are simply
// the PDF files present in that directory.
class Library {
public:
    // Absolute path of the library directory (created on demand).
    static QString directory();

    // Outcome of an import, grouped by what happened to each file.
    struct ImportResult {
        QStringList imported;    // newly copied onto the shelf
        QStringList duplicates;  // skipped: same file name already on the shelf
        QStringList failed;      // copy failed (unreadable source, disk full...)
    };

    // Copy the given local PDF files onto the shelf.
    static ImportResult importFiles(const QStringList& paths);

    // How books() orders the shelf.
    enum class SortMode { ByName, ByTime };

    // Absolute paths of every book on the shelf.
    static QStringList books(SortMode mode = SortMode::ByName);

    // Delete the given books (absolute paths) from the shelf. Returns the
    // file names that could not be deleted. Deletion is forced: read-only is
    // cleared and the attempt is retried briefly, so only a file genuinely
    // locked by another process is reported back.
    static QStringList removeFiles(const QStringList& paths);

private:
    // Delete one file, clearing read-only and retrying past a transient
    // Windows lock. True when the file is gone afterwards.
    static bool removeForcibly(const QString& path);

public:
};

} // namespace Genesis

#endif // LIBRARY_H
