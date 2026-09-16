#ifndef METADATASTORE_H
#define METADATASTORE_H

#include "BookInfo.h"

#include <QHash>
#include <QString>

#include <optional>

namespace Genesis {

// Persistent metadata store for the bookshelf: stores one BookInfo per PDF
// (keyed by file path) as a JSON file next to the library directory. All
// reads/writes hit the disk synchronously — this is called only on shelf
// refresh or on explicit save, so latency is negligible.
class MetadataStore {
public:
    // Load metadata for a single book from the store on disk.
    static std::optional<BookInfo> load(const QString& filePath);

    // Save (insert or update) a single book's metadata.
    static void save(const BookInfo& info);

    // Load all metadata entries (full scan).
    static QHash<QString, BookInfo> loadAll();

    // Wipe every book's READING HISTORY - lastOpened and readSeconds - while
    // keeping the entries themselves (filePath, title) so the shelf keeps its
    // books. Backs 设置 → 清除阅读记录.
    static void clearReadingHistory();

private:
    // Absolute path of the JSON metadata file.
    static QString filePath();
};

} // namespace Genesis

#endif // METADATASTORE_H
