#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QObject>

namespace Genesis {

// The shelf page's TEXT theme. Deliberately narrow: it controls the colour of
// text drawn over the three shelf panels (part1 placeholder, part2 statistics,
// part3 bookshelf), so a user who sets a dark background image can switch the
// labels to white. Surfaces, borders and the accent are not affected - this is
// not a full dark mode.
//
// Widgets paint from these tokens and connect to changed() to repaint, so a
// switch reaches both stylesheet-built labels and custom paintEvent code.
class Theme : public QObject {
    Q_OBJECT

public:
    enum class TextColor { Black, White };

    static Theme& instance();

    TextColor textColor() const { return m_textColor; }
    void setTextColor(TextColor c);

    // --- colour tokens, for painting ---
    // Primary text: titles, values, body labels.
    QColor text() const;
    // Secondary text: subtitles, counts, axis labels.
    QColor mutedText() const;
    // Hint drawn behind a widget's own translucent hover fill.
    QColor hoverFill() const;

    // --- the same tokens as CSS, for setStyleSheet() ---
    QString textCss() const { return text().name(); }
    QString mutedTextCss() const { return mutedText().name(); }

signals:
    // Emitted after the colour changed; every themed widget repaints.
    void changed();

private:
    explicit Theme(QObject* parent = nullptr);

    TextColor m_textColor;
};

} // namespace Genesis

#endif // THEME_H
