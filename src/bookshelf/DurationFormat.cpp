#include "DurationFormat.h"

namespace Genesis {

QString formatDuration(int seconds)
{
    if (seconds <= 0)
        return QString::fromUtf8("未读");
    const int h = seconds / 3600;
    const int m = (seconds % 3600) / 60;
    if (h > 0)
        return QString::fromUtf8("%1 时 %2 分").arg(h).arg(m);
    return QString::fromUtf8("%1 分").arg(m);
}

} // namespace Genesis
