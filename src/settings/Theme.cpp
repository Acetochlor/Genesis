#include "Theme.h"

#include <QSettings>

namespace Genesis {

namespace {
const char* const kKey = "theme/textColor";

// Text colours for the two modes. Both are the pure extremes: the switch
// exists to make text readable over an arbitrary background image, and any
// softening (a #333 dark grey, an off-white) costs contrast against a
// background the app cannot know. Secondary text keeps the SAME pure colour
// and is distinguished by size/position instead.
const QColor kBlackText("#000000");
const QColor kBlackMuted("#000000");
const QColor kWhiteText("#FFFFFF");
const QColor kWhiteMuted("#FFFFFF");

// Hover fill drawn behind text. The light blue used on white would flash
// against a dark image, so the white-text mode gets a translucent dark fill.
const QColor kBlackHover("#E6F7FF");
const QColor kWhiteHover(255, 255, 255, 40);
} // namespace

Theme& Theme::instance()
{
    static Theme inst;
    return inst;
}

Theme::Theme(QObject* parent)
    : QObject(parent)
{
    const QString saved =
        QSettings(QStringLiteral("Genesis"), QStringLiteral("Genesis"))
            .value(QString::fromLatin1(kKey), QStringLiteral("black"))
            .toString();
    m_textColor = saved == QLatin1String("white") ? TextColor::White
                                                  : TextColor::Black;
}

void Theme::setTextColor(TextColor c)
{
    if (c == m_textColor)
        return;
    m_textColor = c;
    QSettings(QStringLiteral("Genesis"), QStringLiteral("Genesis"))
        .setValue(QString::fromLatin1(kKey),
                  c == TextColor::White ? QStringLiteral("white")
                                        : QStringLiteral("black"));
    emit changed();
}

QColor Theme::text() const
{
    return m_textColor == TextColor::White ? kWhiteText : kBlackText;
}

QColor Theme::mutedText() const
{
    return m_textColor == TextColor::White ? kWhiteMuted : kBlackMuted;
}

QColor Theme::hoverFill() const
{
    return m_textColor == TextColor::White ? kWhiteHover : kBlackHover;
}

} // namespace Genesis
