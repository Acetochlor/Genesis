#include "BookInfo.h"

#include <QJsonObject>

namespace Genesis {

BookInfo BookInfo::fromJson(const QJsonObject& obj)
{
    BookInfo info;
    info.filePath   = obj.value(QStringLiteral("filePath")).toString();
    info.title      = obj.value(QStringLiteral("title")).toString();
    if (obj.contains(QStringLiteral("lastOpened")))
        info.lastOpened = QDateTime::fromString(
            obj.value(QStringLiteral("lastOpened")).toString(), Qt::ISODate);
    info.readSeconds = obj.value(QStringLiteral("readSeconds")).toInt(0);
    return info;
}

QJsonObject BookInfo::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("filePath")]   = filePath;
    obj[QStringLiteral("title")]      = title;
    if (lastOpened.isValid())
        obj[QStringLiteral("lastOpened")] = lastOpened.toString(Qt::ISODate);
    obj[QStringLiteral("readSeconds")] = readSeconds;
    return obj;
}

} // namespace Genesis
