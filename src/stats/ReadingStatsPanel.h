#ifndef READINGSTATSPANEL_H
#define READINGSTATSPANEL_H

#include <QWidget>

class QLabel;

namespace Genesis {

class SegmentedToggle;
class StatTile;
class TrendChartWidget;
class HourPieWidget;

// The reading-statistics panel that fills the shelf page's lower-left cell
// (m_part2). Three stacked areas:
//
//   1. a 月/年/总 range switch, a separator, then three stat tiles
//   2. a trend curve of minutes read per bucket (day or month)
//   3. a pie of which hours of the day get read, switchable between
//      阅读时长 and 阅读次数
//
// Stays transparent so the shelf's background image shows through, like the
// rest of that page.
class ReadingStatsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ReadingStatsPanel(QWidget* parent = nullptr);

    // Re-read the log and refresh all three areas against the CURRENT range and
    // metric selection. Safe to call often; it only refreshes values.
    void reload();

private:
    // Apply the current text colour to the stylesheet-built labels. The chart
    // widgets read the theme themselves in paintEvent.
    void applyTextTheme();

    // The selected range as [from, to]. 月 = this calendar month, 年 = this
    // calendar year, 总 = the earliest record .. now.
    void currentRange(QDateTime* from, QDateTime* to) const;
    void refreshRangeDependent();

    SegmentedToggle* m_rangeToggle = nullptr;
    StatTile* m_tileDays = nullptr;
    StatTile* m_tileBooks = nullptr;
    StatTile* m_tileTime = nullptr;
    QLabel* m_trendTitle = nullptr;
    TrendChartWidget* m_trend = nullptr;
    QLabel* m_pieTitle = nullptr;
    HourPieWidget* m_pie = nullptr;
    SegmentedToggle* m_metricToggle = nullptr;
};

} // namespace Genesis

#endif // READINGSTATSPANEL_H
