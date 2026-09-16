#include "MetadataStore.h"
#include "../library/Library.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>

namespace Genesis {

QString MetadataStore::filePath()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("metadata.json"));
}

std::optional<BookInfo> MetadataStore::load(const QString& bookPath)
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();
    for (const QJsonValue& v : arr) {
        BookInfo info = BookInfo::fromJson(v.toObject());
        if (info.filePath == bookPath)
            return info;
    }
    return std::nullopt;
}

void MetadataStore::save(const BookInfo& info)
{
    // Load all, overwrite the matching entry (or append), write back.
    QHash<QString, BookInfo> all;
    {
        QFile f(filePath());
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
            for (const QJsonValue& v : arr) {
                BookInfo entry = BookInfo::fromJson(v.toObject());
                all.insert(entry.filePath, entry);
            }
            f.close();
        }
    }
    all.insert(info.filePath, info);

    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QJsonArray arr;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        arr.append(it.value().toJson());
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
}

QHash<QString, BookInfo> MetadataStore::loadAll()
{
    QHash<QString, BookInfo> all;
    QFile f(filePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        for (const QJsonValue& v : arr) {
            BookInfo entry = BookInfo::fromJson(v.toObject());
            all.insert(entry.filePath, entry);
        }
    }
    return all;
}

void MetadataStore::clearReadingHistory()
{
    // Keep each entry (the shelf still lists the book) but drop what was
    // recorded about reading it, so the card falls back to "未读".
    QHash<QString, BookInfo> all = loadAll();
    for (auto it = all.begin(); it != all.end(); ++it) {
        it->lastOpened = QDateTime();
        it->readSeconds = 0;
    }

    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QJsonArray arr;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        arr.append(it.value().toJson());
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
}

} // namespace Genesis
