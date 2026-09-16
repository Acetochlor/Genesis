#ifndef STATTILE_H
#define STATTILE_H

#include <QString>
#include <QWidget>

namespace Genesis {

// One figure in the stats panel's top row: a large value over a small caption.
// Hand-painted so it can sit transparently on top of the app's background
// image, matching how the rest of the shelf paints.
class StatTile : public QWidget {
    Q_OBJECT

public:
    explicit StatTile(const QString& caption, QWidget* parent = nullptr);

    void setValue(const QString& value);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_caption;
    QString m_value;
};

} // namespace Genesis

#endif // STATTILE_H
