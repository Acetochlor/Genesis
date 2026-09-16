#include "ReadingLogStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

namespace Genesis {

namespace {

// Walk one session and hand each hour-sized chunk to `sink`, so a session that
// crosses an hour boundary (or midnight) is attributed to every hour it really
// covered. EVERY aggregate below funnels through here, which is what keeps the
// day view and the hour view consistent with each other.
template <typename Sink>
void splitIntoHours(const ReadingSession& s, Sink sink)
{
    // Truncate the start to its hour and step forward one hour at a time.
    QDateTime cursor(s.start.date(), QTime(s.start.time().hour(), 0));
    const QDateTime end = s.start.addSecs(s.seconds);
    while (cursor < end) {
        const QDateTime next = cursor.addSecs(3600);
        const QDateTime segStart = qMax(cursor, s.start);
        const QDateTime segEnd = qMin(next, end);
        const int secs = segStart.secsTo(segEnd);
        if (secs > 0)
            sink(cursor, secs);
        cursor = next;
    }
}

// Sessions that qualify for the charts (long enough to count as reading).
QList<ReadingSession> chartSessions(const QDateTime& from, const QDateTime& to)
{
    QList<ReadingSession> out;
    for (const ReadingSession& s : ReadingLogStore::loadAll()) {
        if (!s.start.isValid() || s.seconds < ReadingLogStore::kMinChartSeconds)
            continue;
        // Overlap test against [from, to] - a session counts when any part of
        // it falls inside the range.
        const QDateTime end = s.start.addSecs(s.seconds);
        if (end < from || s.start > to)
            continue;
        out << s;
    }
    return out;
}

} // namespace

QString ReadingLogStore::filePath()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("reading_log.json"));
}

QList<ReadingSession> ReadingLogStore::readAll()
{
    QList<ReadingSession> all;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return all;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        ReadingSession s;
        s.start = QDateTime::fromString(
            obj.value(QStringLiteral("start")).toString(), Qt::ISODate);
        s.seconds = obj.value(QStringLiteral("seconds")).toInt();
        s.bookPath = obj.value(QStringLiteral("book")).toString();
        if (s.start.isValid() && s.seconds > 0)
            all << s;
    }
    return all;
}

void ReadingLogStore::writeAll(const QList<ReadingSession>& sessions)
{
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QJsonArray arr;
    for (const ReadingSession& s : sessions) {
        QJsonObject obj;
        obj.insert(QStringLiteral("start"), s.start.toString(Qt::ISODate));
        obj.insert(QStringLiteral("seconds"), s.seconds);
        obj.insert(QStringLiteral("book"), s.bookPath);
        arr.append(obj);
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

void ReadingLogStore::add(const ReadingSession& session)
{
    if (!session.start.isValid() || session.seconds <= 0
        || session.bookPath.isEmpty())
        return;
    QList<ReadingSession> all = readAll();
    all << session;
    writeAll(all);
}

void ReadingLogStore::clearAll()
{
    // Write an empty array rather than deleting the file: keeps the store's
    // location stable and any reader sees a valid, empty log.
    writeAll({});
}

QList<ReadingSession> ReadingLogStore::loadAll()
{
    return readAll();
}

QHash<QDate, ReadingBucket> ReadingLogStore::dailyBuckets(const QDateTime& from,
                                                          const QDateTime& to)
{
    QHash<QDate, ReadingBucket> out;
    for (const ReadingSession& s : chartSessions(from, to)) {
        splitIntoHours(s, [&](const QDateTime& hourStart, int secs) {
            ReadingBucket& b = out[hourStart.date()];
            b.seconds += secs;
            ++b.count;
        });
    }
    return out;
}

QHash<int, ReadingBucket> ReadingLogStore::hourlyBuckets(const QDateTime& from,
                                                         const QDateTime& to)
{
    QHash<int, ReadingBucket> out;
    for (const ReadingSession& s : chartSessions(from, to)) {
        splitIntoHours(s, [&](const QDateTime& hourStart, int secs) {
            ReadingBucket& b = out[hourStart.time().hour()];
            b.seconds += secs;
            ++b.count;
        });
    }
    return out;
}

int ReadingLogStore::dayCount(const QDateTime& from, const QDateTime& to)
{
    return dailyBuckets(from, to).size();
}

int ReadingLogStore::bookCount(const QDateTime& from, const QDateTime& to)
{
    QSet<QString> books;
    for (const ReadingSession& s : chartSessions(from, to))
        books.insert(s.bookPath);
    return books.size();
}

int ReadingLogStore::totalSeconds(const QDateTime& from, const QDateTime& to)
{
    int total = 0;
    for (const ReadingSession& s : chartSessions(from, to)) {
        const QDateTime end = s.start.addSecs(s.seconds);
        // Count only the part inside the range, so a range that starts midway
        // through a session does not claim the whole of it.
        const QDateTime lo = qMax(s.start, from);
        const QDateTime hi = qMin(end, to);
        const int secs = lo.secsTo(hi);
        if (secs > 0)
            total += secs;
    }
    return total;
}

QDateTime ReadingLogStore::earliestStart()
{
    QDateTime earliest;
    for (const ReadingSession& s : readAll())
        if (!earliest.isValid() || s.start < earliest)
            earliest = s.start;
    return earliest;
}

} // namespace Genesis
