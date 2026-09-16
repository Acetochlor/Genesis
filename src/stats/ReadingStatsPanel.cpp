#include "ReadingStatsPanel.h"

#include "HourPieWidget.h"
#include "SegmentedToggle.h"
#include "StatTile.h"
#include "TrendChartWidget.h"
#include "../bookshelf/DurationFormat.h"
#include "../library/ReadingLogStore.h"
#include "../settings/Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace Genesis {

ReadingStatsPanel::ReadingStatsPanel(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 6);
    root->setSpacing(4);

    // ---- area 1: range switch, separator, stat tiles ----
    m_rangeToggle = new SegmentedToggle(
        {QString::fromUtf8("月"), QString::fromUtf8("年"),
         QString::fromUtf8("总")},
        this);
    connect(m_rangeToggle, &SegmentedToggle::changed, this,
            [this](int) { reload(); });

    QHBoxLayout* rangeRow = new QHBoxLayout;
    rangeRow->setContentsMargins(0, 0, 0, 0);
    rangeRow->addWidget(m_rangeToggle);
    rangeRow->addStretch();
    root->addLayout(rangeRow);

    QWidget* sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet("background: rgba(204, 238, 255, 180);");
    root->addWidget(sep);

    QHBoxLayout* tiles = new QHBoxLayout;
    tiles->setContentsMargins(0, 0, 0, 0);
    tiles->setSpacing(0);
    m_tileDays = new StatTile(QString::fromUtf8("阅读天数（天）"), this);
    m_tileBooks = new StatTile(QString::fromUtf8("阅读书籍（本）"), this);
    m_tileTime = new StatTile(QString::fromUtf8("累计时长"), this);
    tiles->addWidget(m_tileDays, 1);
    tiles->addWidget(m_tileBooks, 1);
    tiles->addWidget(m_tileTime, 1);
    root->addLayout(tiles);
    root->addSpacing(4);

    // ---- area 2: trend curve ----
    m_trendTitle = new QLabel(QString::fromUtf8("阅读趋势"), this);
    root->addWidget(m_trendTitle);
    m_trend = new TrendChartWidget(this);
    root->addWidget(m_trend, 3);

    // ---- area 3: hour-of-day pie ----
    QHBoxLayout* pieHeader = new QHBoxLayout;
    pieHeader->setContentsMargins(0, 0, 0, 0);
    m_pieTitle = new QLabel(QString::fromUtf8("阅读时刻"), this);
    pieHeader->addWidget(m_pieTitle);
    pieHeader->addStretch();
    m_metricToggle = new SegmentedToggle(
        {QString::fromUtf8("阅读时长"), QString::fromUtf8("阅读次数")}, this);
    connect(m_metricToggle, &SegmentedToggle::changed, this, [this](int i) {
        m_pie->setMetric(i == 0 ? HourPieWidget::Metric::Seconds
                                : HourPieWidget::Metric::Count);
    });
    pieHeader->addWidget(m_metricToggle);
    root->addLayout(pieHeader);
    m_pie = new HourPieWidget(this);
    root->addWidget(m_pie, 3);

    connect(&Theme::instance(), &Theme::changed, this,
            [this]() { applyTextTheme(); });

    applyTextTheme();
    reload();
}

void ReadingStatsPanel::applyTextTheme()
{
    const QString css =
        QStringLiteral("color:%1;").arg(Theme::instance().textCss());
    if (m_trendTitle)
        m_trendTitle->setStyleSheet(css);
    if (m_pieTitle)
        m_pieTitle->setStyleSheet(css);
}

void ReadingStatsPanel::currentRange(QDateTime* from, QDateTime* to) const
{
    const QDateTime now = QDateTime::currentDateTime();
    switch (m_rangeToggle->currentIndex()) {
    case 0: {   // 月 - the last half month, ending today
        // The chart shows a fortnight of daily points; the tiles below use the
        // same window, so the panel describes one consistent period.
        *from = QDateTime(now.date().addDays(-14), QTime(0, 0));
        *to = QDateTime(now.date(), QTime(23, 59, 59));
        break;
    }
    case 1: {   // 年 - this calendar year
        const QDate first(now.date().year(), 1, 1);
        *from = QDateTime(first, QTime(0, 0));
        *to = QDateTime(QDate(now.date().year(), 12, 31), QTime(23, 59, 59));
        break;
    }
    default: {  // 总 - this year's data (still month by month on the chart)
        const QDate first(now.date().year(), 1, 1);
        *from = QDateTime(first, QTime(0, 0));
        *to = now;
        break;
    }
    }
}

void ReadingStatsPanel::reload()
{
    QDateTime from, to;
    currentRange(&from, &to);

    m_tileDays->setValue(
        QString::number(ReadingLogStore::dayCount(from, to)));
    m_tileBooks->setValue(
        QString::number(ReadingLogStore::bookCount(from, to)));
    m_tileTime->setValue(
        formatDuration(ReadingLogStore::totalSeconds(from, to)));

    // Granularity follows the range: a month is read day by day, a year or the
    // all-time view month by month (a multi-year daily series is unreadable).
    // The x label is the full date for its bucket - "9月5日" for a day,
    // "2026年9月" for a month - so the axis is readable without a legend.
    const bool byMonth = m_rangeToggle->currentIndex() != 0;
    QList<TrendChartWidget::Point> points;
    if (byMonth) {
        // Aggregate the daily buckets up into months.
        const QHash<QDate, ReadingBucket> daily =
            ReadingLogStore::dailyBuckets(from, to);
        QHash<QDate, int> perMonth;   // first-of-month -> seconds
        for (auto it = daily.constBegin(); it != daily.constEnd(); ++it) {
            const QDate m(it.key().year(), it.key().month(), 1);
            perMonth[m] += it.value().seconds;
        }
        QList<QDate> keys = perMonth.keys();
        std::sort(keys.begin(), keys.end());
        for (const QDate& m : keys) {
            TrendChartWidget::Point p;
            p.date = m;
            p.label = QString::fromUtf8("%1年%2月")
                          .arg(m.year())
                          .arg(m.month());
            p.seconds = perMonth.value(m);
            points << p;
        }
    } else {
        const QHash<QDate, ReadingBucket> daily =
            ReadingLogStore::dailyBuckets(from, to);
        QList<QDate> keys = daily.keys();
        std::sort(keys.begin(), keys.end());
        for (const QDate& d : keys) {
            TrendChartWidget::Point p;
            p.date = d;
            p.label = QString::fromUtf8("%1月%2日")
                          .arg(d.month())
                          .arg(d.day());
            p.seconds = daily.value(d).seconds;
            points << p;
        }
    }
    m_trend->setPoints(points);

    // The ring follows the selected 月/年/总 range, but the dial's centre
    // readout is always TODAY, so it needs its own day-scoped buckets.
    m_pie->setBuckets(ReadingLogStore::hourlyBuckets(from, to));
    const QDate today = QDate::currentDate();
    m_pie->setTodayBuckets(ReadingLogStore::hourlyBuckets(
        QDateTime(today, QTime(0, 0)),
        QDateTime(today, QTime(23, 59, 59))));
}

} // namespace Genesis
