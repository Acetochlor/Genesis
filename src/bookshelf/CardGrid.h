#ifndef CARDGRID_H
#define CARDGRID_H

#include <QWidget>
#include <QVector>

class QGridLayout;

namespace Genesis {

class BookCardWidget;

// Helper widget that holds the book card grid and responds to resize by
// re-flowing the cards into the appropriate number of columns.
class CardGrid : public QWidget {
    Q_OBJECT

public:
    explicit CardGrid(QWidget* parent = nullptr);

    void setCards(const QVector<BookCardWidget*>& cards);
    QVector<BookCardWidget*>& cards() { return m_cards; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void relayout();
    QVector<BookCardWidget*> m_cards;
};

} // namespace Genesis

#endif // CARDGRID_H
