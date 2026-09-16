#ifndef READINGLOGSTORE_H
#define READINGLOGSTORE_H

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

namespace Genesis {

// One continuous stretch of reading. Written as a RAW record: a session that
// runs over midnight or over an hour boundary is NOT split here - the splits
// happen when querying, so the file holds one truth and every projection
// (per-day, per-hour) is derived from it and cannot disagree with the others.
struct ReadingSession {
    QDateTime start;      // local time the session began
    int seconds = 0;      // duration; sessions under a minute are ignored by
                          // the charts (see kMinChartSeconds)
    QString bookPath;     // absolute path of the PDF being read
};

// One bucket of a histogram: total time and how many session-chunks landed in
// it. Both metrics feed the pie chart's 阅读时长 / 阅读次数 switch.
struct ReadingBucket {
    int seconds = 0;
    int count = 0;
};

// Persistent log of reading sessions, saved as one JSON file in the app data
// directory (sibling of metadata.json / bookmarks.json). This is the ONLY
// source of day-by-day and hour-by-hour history: BookInfo keeps a cumulative
// total per book and nothing else.
class ReadingLogStore {
public:
    // Sessions shorter than this are excluded from the charts. The raw log
    // still keeps them, so the threshold lives in exactly one place and can be
    // changed without rewriting history.
    static constexpr int kMinChartSeconds = 60;

    // Append one session. Ignored when it has no duration, no start or no book.
    static void add(const ReadingSession& session);

    // Discard every recorded session. The trend and hour charts go empty;
    // reading time starts accumulating again from zero.
    static void clearAll();

    // Every session, in stored (append) order.
    static QList<ReadingSession> loadAll();

    // --- Aggregates over [from, to] inclusive ---
    // A session straddling midnight contributes to BOTH days; one straddling
    // an hour boundary contributes to both hours.

    // date -> bucket. Days with no reading are simply absent.
    static QHash<QDate, ReadingBucket> dailyBuckets(const QDateTime& from,
                                                    const QDateTime& to);
    // hour of day (0..23) -> bucket, summed across all days in the range.
    static QHash<int, ReadingBucket> hourlyBuckets(const QDateTime& from,
                                                   const QDateTime& to);
    // Distinct days with reading (chart-qualifying sessions only).
    static int dayCount(const QDateTime& from, const QDateTime& to);
    // Distinct books read.
    static int bookCount(const QDateTime& from, const QDateTime& to);
    // Total reading time in seconds.
    static int totalSeconds(const QDateTime& from, const QDateTime& to);

    // The whole supported range: the earliest session's day .. now. Used by the
    // 总 tab.
    static QDateTime earliestStart();

private:
    static QString filePath();
    static QList<ReadingSession> readAll();
    static void writeAll(const QList<ReadingSession>& sessions);
};

} // namespace Genesis

#endif // READINGLOGSTORE_H
