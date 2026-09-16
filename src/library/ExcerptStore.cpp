#include "ExcerptStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace Genesis {

namespace {
// Six common text colours. Black leads (the default) and white is kept so the
// excerpt stays readable over a dark background image.
const char* const kColors[] = {
    "#000000",   // 黑
    "#FFFFFF",   // 白
    "#C0392B",   // 红
    "#1F6FEB",   // 蓝
    "#2EA043",   // 绿
    "#6E7781",   // 灰
};

QString filePath()
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("excerpt.json"));
}
} // namespace

QList<QColor> ExcerptStore::palette()
{
    QList<QColor> out;
    for (const char* c : kColors)
        out << QColor(QString::fromLatin1(c));
    return out;
}

QColor ExcerptStore::defaultColor()
{
    return QColor(QString::fromLatin1(kColors[0]));
}

Excerpt ExcerptStore::load()
{
    Excerpt e;
    e.color = defaultColor();
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return e;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    e.text = obj.value(QStringLiteral("text")).toString();
    const QString col = obj.value(QStringLiteral("color")).toString();
    if (!col.isEmpty() && QColor::isValidColorName(col))
        e.color = QColor(col);
    // Absent in files written before the alignment feature: qBound then leaves
    // the centred default, which is what those excerpts were already drawn as.
    e.hAlign = static_cast<HAlign>(qBound(0, obj.value(QStringLiteral("hAlign")).toInt(1), 2));
    e.vAlign = static_cast<VAlign>(qBound(0, obj.value(QStringLiteral("vAlign")).toInt(1), 2));
    return e;
}

void ExcerptStore::save(const Excerpt& excerpt)
{
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QJsonObject obj;
    obj.insert(QStringLiteral("text"), excerpt.text);
    obj.insert(QStringLiteral("color"), excerpt.color.name());
    obj.insert(QStringLiteral("hAlign"), static_cast<int>(excerpt.hAlign));
    obj.insert(QStringLiteral("vAlign"), static_cast<int>(excerpt.vAlign));
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    f.close();
}

void ExcerptStore::clear()
{
    QFile::remove(filePath());
}

} // namespace Genesis
