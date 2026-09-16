#include "CardGrid.h"
#include "BookCardWidget.h"

#include <QGridLayout>
#include <QResizeEvent>

namespace Genesis {

namespace {
const int kCardPadding = 10;
} // namespace

CardGrid::CardGrid(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: transparent;");
}

void CardGrid::setCards(const QVector<BookCardWidget*>& cards)
{
    m_cards = cards;
    relayout();
}

void CardGrid::relayout()
{
    QLayout* old = layout();
    if (old) {
        QLayoutItem* item;
        while ((item = old->takeAt(0)))
            delete item;
        delete old;
    }

    if (m_cards.isEmpty())
        return;

    const int availW = width() - 2 * kCardPadding;
    const int cardW = 140;
    const int gap = 8;
    const int cols = qMax(1, (availW + gap) / (cardW + gap));

    QGridLayout* grid = new QGridLayout;
    grid->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
    grid->setSpacing(gap);

    int lastRow = 0;
    for (int i = 0; i < m_cards.size(); ++i) {
        const int row = i / cols;
        const int col = i % cols;
        grid->addWidget(m_cards[i], row, col, Qt::AlignLeft | Qt::AlignTop);
        lastRow = row;
    }
    // Absorb all leftover space to the right of the last column and below the
    // last row, so the cards pack into the top-left corner instead of being
    // spread evenly across the container.
    grid->setColumnStretch(cols, 1);
    grid->setRowStretch(lastRow + 1, 1);
    setLayout(grid);
}

void CardGrid::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayout();
}

} // namespace Genesis
