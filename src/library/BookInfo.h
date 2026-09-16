#ifndef BOOKINFO_H
#define BOOKINFO_H

#include <QString>
#include <QDateTime>
#include <QJsonObject>

namespace Genesis {

// Per-book persistent metadata stored alongside the library.
struct BookInfo {
    QString filePath;           // absolute path of the PDF on the shelf
    QString title;              // display name (derived from file name)
    QDateTime lastOpened;       // last time the book was opened, or null
    int readSeconds;            // total time spent reading, in seconds

    // Load / save from a JSON object.
    static BookInfo fromJson(const QJsonObject& obj);
    QJsonObject toJson() const;
};

} // namespace Genesis

#endif // BOOKINFO_H
