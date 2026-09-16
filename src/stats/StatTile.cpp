#include "StatTile.h"

#include "../settings/Theme.h"

#include <QPainter>

namespace Genesis {

StatTile::StatTile(const QString& caption, QWidget* parent)
    : QWidget(parent)
    , m_caption(caption)
{
    setMinimumHeight(48);
    // Repaint when the shelf's text colour is switched.
    connect(&Theme::instance(), &Theme::changed, this,
            [this]() { update(); });
}

void StatTile::setValue(const QString& value)
{
    if (value == m_value)
        return;
    m_value = value;
    update();
}

void StatTile::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Value on top, caption under it; both centred. The value gets the larger
    // font by deriving it from the widget font rather than hard-coding, so DPI
    // scaling stays sane.
    QFont valueFont = font();
    valueFont.setPointSizeF(font().pointSizeF() * 1.5);
    valueFont.setBold(true);

    const QFontMetrics vm(valueFont);
    const QFontMetrics cm(font());
    const int gap = 2;
    const int totalH = vm.height() + gap + cm.height();
    int y = (height() - totalH) / 2;

    p.setFont(valueFont);
    p.setPen(Theme::instance().text());
    p.drawText(QRect(0, y, width(), vm.height()), Qt::AlignCenter,
               m_value.isEmpty() ? QStringLiteral("—") : m_value);

    y += vm.height() + gap;
    p.setFont(font());
    p.setPen(Theme::instance().mutedText());
    p.drawText(QRect(0, y, width(), cm.height()), Qt::AlignCenter, m_caption);
}

} // namespace Genesis
