#include "Library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>

namespace Genesis {

QString Library::directory()
{
    // e.g. C:/Users/<user>/AppData/Roaming/Genesis/library
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!dir.exists("library"))
        dir.mkpath("library");
    return dir.filePath("library");
}

Library::ImportResult Library::importFiles(const QStringList& paths)
{
    ImportResult result;
    const QDir libDir(directory());

    for (const QString& path : paths) {
        const QFileInfo src(path);
        const QString dest = libDir.filePath(src.fileName());

        if (QFileInfo::exists(dest)) {
            result.duplicates << src.fileName();
            continue;
        }
        if (QFile::copy(path, dest))
            result.imported << src.fileName();
        else
            result.failed << src.fileName();
    }
    return result;
}

QStringList Library::books(SortMode mode)
{
    QDir libDir(directory());
    QStringList books;
    // ByTime = newest first (most recently modified at the top of the shelf).
    const QDir::SortFlags sort = (mode == SortMode::ByName)
        ? QDir::Name
        : (QDir::Time);
    const QFileInfoList entries =
        libDir.entryInfoList({QStringLiteral("*.pdf")}, QDir::Files, sort);
    for (const QFileInfo& info : entries)
        books << info.absoluteFilePath();
    return books;
}

QStringList Library::removeFiles(const QStringList& paths)
{
    QStringList failed;
    const QDir libDir(directory());
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (!removeForcibly(path)) {
            // Gone is as good as deleted: treat a vanished file as success so
            // a second attempt on an already-removed book is not an error.
            if (info.exists())
                failed << info.fileName();
            continue;
        }
        // Also drop this book's cached thumbnails (any DPR variant).
        const QString base = info.completeBaseName().toLower();
        const QStringList thumbs = libDir.entryList(
            {base + QStringLiteral(".thumb-*.png"),
             base + QStringLiteral(".png")},
            QDir::Files);
        for (const QString& t : thumbs) {
            const QString thumbPath = libDir.filePath(t);
            QFile(thumbPath).setPermissions(QFileDevice::ReadOwner
                                            | QFileDevice::WriteOwner);
            QFile::remove(thumbPath);
        }
    }
    return failed;
}

bool Library::removeForcibly(const QString& path)
{
    // Deleting a file out from under the app's own reader is the common case:
    // the PDF is still open, so the OS refuses and a plain remove() fails.
    // Retry a few times with a short pause, because on Windows a handle that
    // is being released (or held briefly by AV / the search indexer) is often
    // free a moment later, and a delete that failed once frequently succeeds
    // on the next attempt.
    constexpr int kAttempts = 5;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        // Read-only is the other common refusal: QFile::copy (import) carries
        // the source's attribute over, and Windows will not delete a
        // read-only file. Clear every write bit the OS tracks, not just the
        // user's, so a file owned by another account still goes.
        QFile file(path);
        file.setPermissions(QFileDevice::WriteOwner
                            | QFileDevice::WriteUser
                            | QFileDevice::WriteGroup
                            | QFileDevice::WriteOther
                            | QFileDevice::ReadOwner
                            | QFileDevice::ReadUser
                            | QFileDevice::ReadGroup
                            | QFileDevice::ReadOther);
        if (file.remove() || !QFileInfo::exists(path))
            return true;
        QThread::msleep(60);
    }
    return !QFileInfo::exists(path);
}

} // namespace Genesis
