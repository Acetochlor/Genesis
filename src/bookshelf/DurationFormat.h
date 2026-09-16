#ifndef DURATIONFORMAT_H
#define DURATIONFORMAT_H

#include <QString>

namespace Genesis {

// Human-readable reading duration: "未读" for nothing, "H 时 M 分" past an
// hour, otherwise "M 分". Shared by the bookshelf card and the stats panel so
// both describe a total the same way.
QString formatDuration(int seconds);

} // namespace Genesis

#endif // DURATIONFORMAT_H
