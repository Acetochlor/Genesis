#include "BookmarkStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace Genesis {

QString BookmarkStore::filePath()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("bookmarks.json"));
}

// Custom group names live in a sibling file so the bookmark list keeps its
// plain-array format.
static QString groupNamesFilePath()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("bookmark_groups.json"));
}

QHash<QString, QString> BookmarkStore::readGroupNames()
{
    QHash<QString, QString> names;
    QFile f(groupNamesFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return names;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        names.insert(it.key(), it.value().toString());
    return names;
}

void BookmarkStore::writeGroupNames(const QHash<QString, QString>& names)
{
    QFile f(groupNamesFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QJsonObject obj;
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        obj.insert(it.key(), it.value());
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

QString BookmarkStore::groupName(const QString& bookPath)
{
    const QString custom = readGroupNames().value(bookPath);
    return custom.isEmpty() ? QFileInfo(bookPath).completeBaseName() : custom;
}

void BookmarkStore::setGroupName(const QString& bookPath, const QString& name)
{
    QHash<QString, QString> names = readGroupNames();
    const QString trimmed = name.trimmed();
    // Renaming back to the default (or empty) drops the override.
    if (trimmed.isEmpty()
        || trimmed == QFileInfo(bookPath).completeBaseName())
        names.remove(bookPath);
    else
        names.insert(bookPath, trimmed);
    writeGroupNames(names);
}

QList<Bookmark> BookmarkStore::readAll()
{
    QList<Bookmark> all;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return all;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        Bookmark bm;
        bm.bookPath = obj.value(QStringLiteral("bookPath")).toString();
        bm.name = obj.value(QStringLiteral("name")).toString();
        bm.page = obj.value(QStringLiteral("page")).toInt();
        bm.createdAt = QDateTime::fromString(
            obj.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
        if (!bm.bookPath.isEmpty())
            all << bm;
    }
    return all;
}

void BookmarkStore::writeAll(const QList<Bookmark>& bookmarks)
{
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QJsonArray arr;
    for (const Bookmark& bm : bookmarks) {
        QJsonObject obj;
        obj.insert(QStringLiteral("bookPath"), bm.bookPath);
        obj.insert(QStringLiteral("name"), bm.name);
        obj.insert(QStringLiteral("page"), bm.page);
        obj.insert(QStringLiteral("createdAt"),
                   bm.createdAt.toString(Qt::ISODate));
        arr.append(obj);
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

QList<Bookmark> BookmarkStore::loadAll()
{
    return readAll();
}

QList<Bookmark> BookmarkStore::forBook(const QString& bookPath)
{
    QList<Bookmark> result;
    for (const Bookmark& bm : readAll()) {
        if (bm.bookPath == bookPath)
            result << bm;
    }
    return result;
}

void BookmarkStore::add(const Bookmark& bookmark)
{
    QList<Bookmark> all = readAll();
    all << bookmark;
    writeAll(all);
}

void BookmarkStore::remove(const Bookmark& bookmark)
{
    QList<Bookmark> all = readAll();
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].bookPath == bookmark.bookPath
            && all[i].page == bookmark.page
            && all[i].name == bookmark.name) {
            all.removeAt(i);
            break;
        }
    }
    writeAll(all);
}

void BookmarkStore::setForBook(const QString& bookPath,
                               const QList<Bookmark>& bookmarks)
{
    // Keep other books' entries in place; swap this book's block for the
    // new ordered list (appended at the original first position).
    QList<Bookmark> all = readAll();
    int insertAt = all.size();
    for (int i = all.size() - 1; i >= 0; --i) {
        if (all[i].bookPath == bookPath) {
            insertAt = i;
            all.removeAt(i);
        }
    }
    for (int i = 0; i < bookmarks.size(); ++i)
        all.insert(insertAt + i, bookmarks[i]);
    writeAll(all);
}

void BookmarkStore::purgeMissingBooks()
{
    QList<Bookmark> all = readAll();
    QList<Bookmark> kept;
    for (const Bookmark& bm : all) {
        if (QFileInfo::exists(bm.bookPath))
            kept << bm;
    }
    if (kept.size() != all.size())
        writeAll(kept);

    // Drop custom group names of vanished books too.
    QHash<QString, QString> names = readGroupNames();
    bool changed = false;
    for (auto it = names.begin(); it != names.end();) {
        if (!QFileInfo::exists(it.key())) {
            it = names.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        writeGroupNames(names);
}

} // namespace Genesis
