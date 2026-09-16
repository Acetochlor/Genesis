#ifndef EXCERPTSTORE_H
#define EXCERPTSTORE_H

#include <QColor>
#include <QList>
#include <QString>

namespace Genesis {

// The app's single book excerpt (书摘): one short passage the user types in
// from 更多 → 书摘, shown on the shelf page's top-left panel.
//
// Global rather than per-book, so the excerpt reads as a standing note pinned
// to the app rather than as another field of a book's metadata.
// Where the excerpt sits inside its panel. Both default to the centre, which
// is how the panel looked before the user could move it.
enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

struct Excerpt {
    QString text;
    QColor color;   // one of kExcerptColors; invalid = use the default
    HAlign hAlign = HAlign::Center;
    VAlign vAlign = VAlign::Middle;
};

class ExcerptStore {
public:
    // The six colours offered by the editor, in display order. Both the dialog
    // (swatches) and the shelf panel (text colour) read this one list.
    static QList<QColor> palette();
    // The colour used when nothing has been chosen yet.
    static QColor defaultColor();

    static Excerpt load();
    static void save(const Excerpt& excerpt);
    // Drop the stored excerpt (the panel goes back to its placeholder).
    static void clear();
};

} // namespace Genesis

#endif // EXCERPTSTORE_H
