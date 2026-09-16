#include "MdEditorDialog.h"

#include <algorithm>
#include <functional>

#include <QVBoxLayout>
#include <QAbstractTextDocumentLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QCryptographicHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QVector>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextEdit>
#include <QWheelEvent>
#include <QTextList>
#include <QTextListFormat>
#include <QTextLine>
#include <QTimer>
#include <QTouchEvent>
#include <QUrl>
#include <QVariant>

namespace Genesis {

namespace {

// GitHub markdown-body light-theme tokens (markdown-tools preview).
const QColor kFgDefault("#1F2328");      // body text
const QColor kFgMuted("#656D76");        // blockquote, h6
const QColor kBorderDefault("#D0D7DE");  // blockquote bar, hr
const QColor kBorderMuted("#D8DEE4");    // h1/h2 underline
const QColor kAccentFg("#0969DA");       // links
const QColor kNeutralMuted(175, 184, 193, 51);   // inline code bg (~0.2 alpha)
const QColor kCanvasSubtle("#F6F8FA");   // code block bg

// Preview indentation. setMarkdown renders list items and blockquotes with
// a hard-coded 40px unit; this shrinks both to a tighter value.
constexpr int kListIndentWidth = 24;     // px per list level
constexpr int kQuoteLeftMargin = 24;     // px of the blockquote left margin
// Extra bottom space reserved on the last block of a fenced run when the
// overlay's real font metrics need more height than the in-document copy.
constexpr int kCodeFrameExtraBottomProperty = QTextFormat::UserProperty + 1;
// Horizontal/vertical chrome of an MdCodeFrame overlay, matching its
// stylesheet (1px border + 6px/4px padding). The preview document's left
// margin is set to kChromeH so the frame, which is placed kChromeH to the
// left of its code text, keeps its left border inside the viewport.
constexpr qreal kChromeH = 7;   // 1px border + 6px horizontal padding
constexpr qreal kChromeV = 5;   // 1px border + 4px vertical padding
// Extra left inset applied to the code frame beyond the document margin, so
// the box sits clearly inside the body column instead of hugging its edge.
constexpr int kCodeFrameLeftInset = 8;
// Code frames and blockquotes render one step below the 16px body text.
constexpr int kCodePixelSize = 14;
constexpr int kBlockquotePixelSize = 14;
// Space kept on EACH side of the note editor's line numbers. Equal on both
// sides so the digits read as centred in the gutter strip.
constexpr int kGutterPad = 6;
// Sentinel wrapping the heading level of a "N. # text" list item. md4c
// parses a heading inside a list item but Qt's importer DROPS the heading
// block's list membership — the item loses its "1." marker and the rest of
// the list renumbers from 1. refreshPreview() therefore replaces the "#"
// run with <sentinel>level<sentinel> so the importer sees a plain list
// item, and polishPreviewDocument() decodes it back into a heading block
// (level + font) that KEEPS its place in the list.
constexpr QChar kHeadingSentinel(0xE000);   // private-use, never typed

// Qt's markdown importer only turns a literal <img> tag into an embedded
// picture when the tag is SELF-CLOSING (<img ... />). An un-slash-terminated
// <img ...> — exactly what a paste from 语雀/网页 HTML produces — is parsed as
// a raw HTML block (line start) or mangled inline element and silently
// dropped, so the note's image never shows (markdown 渲染 panel stays blank).
// Forcing the trailing slash in makes the picture render (CommonMark/HTML
// compliant). Quoted attribute values are skipped so an ">" or "/" inside one
// is not mistaken for the tag terminator. The line is edited in place; a line
// with no <img> is left untouched.
void forceImgSelfClosing(QString& line)
{
    static const QRegularExpression imgTag(
        QStringLiteral("<img\\b"), QRegularExpression::CaseInsensitiveOption);
    QList<int> insertAt;
    QRegularExpressionMatchIterator it = imgTag.globalMatch(line);
    while (it.hasNext()) {
        const int open = it.next().capturedStart();
        bool inQuote = false;
        int i = open + 4;
        for (; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QLatin1Char('"'))
                inQuote = !inQuote;
            else if (!inQuote && ch == QLatin1Char('>'))
                break;   // i now indexes the tag's closing '>'
        }
        if (i >= line.size())
            break;   // tag's '>' is not on this line: nothing to close here
        if (i - 1 >= open && line.at(i - 1) == QLatin1Char('/'))
            continue;   // already self-closing (<img ... />)
        insertAt.append(i);
    }
    // Insert right-to-left so earlier recorded positions stay valid.
    std::sort(insertAt.begin(), insertAt.end(), std::greater<int>());
    for (const int at : insertAt)
        line.insert(at, QLatin1Char('/'));
}

// Qt's markdown importer treats ANY `name…>`-style token as an inline HTML
// element and, when it never finds that element's closing tag, swallows the
// REST of the document into the unwritten content — so a stray C++ `<vector>`
// or `std::make_shared<…>` in a normal paragraph makes everything after it not
// display at all. Escape any `<name>` whose name is not one of the genuine
// HTML tags we actually rely on (img is handled separately and stays intact),
// turning it into `&lt;name…` plain text so Qt renders it literally instead.
// Only the opening '<' need be escaped; trailing '>'s stay as ordinary text.
// Applies to plain (non-fenced) lines only.
void escapeStrayAngleTags(QString& line)
{
    static const QSet<QString> allowed = [] {
        QSet<QString> s;
        const char* tags[] = {
            "a", "abbr", "address", "article", "aside", "b", "bdi", "bdo",
            "blockquote", "br", "caption", "cite", "code", "col", "dd",
            "del", "details", "div", "dl", "dt", "em", "fieldset",
            "figcaption", "figure", "footer", "form", "h1", "h2", "h3",
            "h4", "h5", "h6", "header", "hr", "html", "i", "img", "input",
            "ins", "kbd", "label", "legend", "li", "main", "mark", "noscript",
            "ol", "optgroup", "option", "p", "pre", "q", "s", "samp",
            "section", "select", "small", "source", "span", "strong", "sub",
            "summary", "sup", "table", "tbody", "td", "textarea", "tfoot",
            "th", "thead", "time", "title", "tr", "track", "u", "ul", "var",
            "video", "wbr"};
        for (const char* t : tags)
            s.insert(QLatin1String(t));
        return s;
    }();

    QString out;
    out.reserve(line.size() + 16);
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch != QLatin1Char('<') || i + 1 >= line.size()) {
            out += ch;
            continue;
        }
        // Optional "/" for a closing tag </name>.
        int ni = i + 1;
        if (line.at(ni) == QLatin1Char('/'))
            ++ni;
        const bool candidate =
            ni < line.size()
            && ((line.at(ni) >= QLatin1Char('a') && line.at(ni) <= QLatin1Char('z'))
                || (line.at(ni) >= QLatin1Char('A') && line.at(ni) <= QLatin1Char('Z')));
        if (!candidate) {
            out += ch;
            continue;
        }
        // Skip the `](<…>)` link/image destination form so space-paths keep.
        if (i > 0 && line.at(i - 1) == QLatin1Char('(')) {
            out += ch;
            continue;
        }
        // Read the tag (or closing tag) name.
        int j = ni;
        while (j < line.size()
               && ((line.at(j) >= QLatin1Char('a') && line.at(j) <= QLatin1Char('z'))
                   || (line.at(j) >= QLatin1Char('A') && line.at(j) <= QLatin1Char('Z'))
                   || (line.at(j) >= QLatin1Char('0') && line.at(j) <= QLatin1Char('9'))
                   || line.at(j) == QLatin1Char('-')))
            ++j;
        if (allowed.contains(line.mid(ni, j - ni).toLower())) {
            out += ch;   // genuine HTML we want: keep for Qt / html blocks
        } else {
            out += QStringLiteral("&lt;");   // escape the '<' (open or close)
        }
    }
    line = out;
}
// True for a block that renders as a code block in the preview: fenced
// (BlockCodeLanguage/BlockCodeFence stamped by setMarkdown) or 4-space
// indented (plain block with indent + fixed-pitch font). Used by
// polishPreviewDocument() to find the first/last block of each contiguous
// code run so the context spacing lands on the run edges only.
bool isPreviewCodeBlock(const QTextBlock& b)
{
    const QTextBlockFormat bf = b.blockFormat();
    if (bf.hasProperty(QTextFormat::BlockCodeLanguage)
        || bf.hasProperty(QTextFormat::BlockCodeFence))
        return true;
    return bf.indent() > 0
        && b.length() > 0
        && b.begin().fragment().charFormat().fontFixedPitch();
}

void polishPreviewDocument(QTextDocument* doc)
{
    QTextCursor c(doc);

    // ---- decode heading-in-list sentinels FIRST: a block whose text
    //      starts with <sentinel>level<sentinel> is a "N. # text" item
    //      that was smuggled through setMarkdown as a plain list item
    //      (see kHeadingSentinel). Strip the marker, then promote the
    //      block to a real heading (level + Qt's FontSizeAdjustment) —
    //      while its QTextList membership stays intact, so the "N."
    //      marker and the numbering of the following items survive.
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        const QString text = b.text();
        if (text.size() < 2 || text.at(0) != kHeadingSentinel)
            continue;
        const int close = text.indexOf(kHeadingSentinel, 1);
        const int level = close > 1 ? text.mid(1, close - 1).toInt() : 0;
        if (level < 1 || level > 6)
            continue;
        c.setPosition(b.position());
        c.setPosition(b.position() + close + 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        QTextBlockFormat hbf = b.blockFormat();
        hbf.setHeadingLevel(level);
        c.setPosition(b.position());
        c.setBlockFormat(hbf);
        // Same size mechanism as Qt's own markdown importer: h1..h6 →
        // FontSizeAdjustment +3..-2 (weight is applied by the heading
        // pass below, exactly as for ordinary headings).
        QTextCharFormat cf;
        cf.setProperty(QTextFormat::FontSizeAdjustment, 4 - level);
        c.setPosition(b.position());
        c.setPosition(b.position() + b.length() - 1,
                      QTextCursor::KeepAnchor);
        c.mergeCharFormat(cf);
    }

    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();

        // ---- blockquotes: muted text + keep the (Qt-added) indent; the
        //      left bar itself is drawn in paintEvent via BlockQuoteLevel.
        //      Reduce the importer's default 40px left margin so quotes
        //      sit closer to the left bar.
        if (bf.hasProperty(QTextFormat::BlockQuoteLevel)) {
            bf.setLeftMargin(kQuoteLeftMargin);
            c.setPosition(b.position());
            c.setBlockFormat(bf);
            c.setPosition(b.position());
            c.setPosition(b.position() + b.length() - 1,
                          QTextCursor::KeepAnchor);
            QTextCharFormat cf;
            cf.setForeground(kFgMuted);
            // Quote text is one step down from the 16px body (see
            // kBlockquotePixelSize); an absolute pixel size is safe here
            // because a quote block has no FontSizeAdjustment of its own, so
            // there is no heading multiplier to fight with.
            cf.setProperty(QTextFormat::FontPixelSize, kBlockquotePixelSize);
            c.mergeCharFormat(cf);
        }

        // ---- unordered lists: switch the bullet glyph by nesting level
        //      (GitHub/Lake style: level 1 disc, level 2 hollow circle,
        //      level 3+ square). The markdown importer stamps every level
        //      with Disc, so rewrite the list format per level.
        if (QTextList* list = b.textList()) {
            const QTextListFormat lf = list->format();
            if (lf.style() == QTextListFormat::ListDisc
                || lf.style() == QTextListFormat::ListCircle
                || lf.style() == QTextListFormat::ListSquare) {
                const int lvl = qBound(1, lf.indent(), 6);
                static const QTextListFormat::Style kBullets[] = {
                    QTextListFormat::ListDisc,
                    QTextListFormat::ListCircle,
                    QTextListFormat::ListSquare};
                QTextListFormat nf = lf;
                nf.setStyle(kBullets[(lvl - 1) % 3]);
                c.setPosition(b.position());
                c.createList(nf);   // reassign the block to the new-style list
            }
        }

        // ---- headings: GitHub weight (600). The SIZE keeps Qt's own
        //      heading mechanism (FontSizeAdjustment set by the markdown
        //      importer), so the block height stays in sync with the font
        //      and text never overflows the block. Overriding to an
        //      absolute pixel size would inflate the glyphs beyond the
        //      layout-computed block rect, overlapping the list item that
        //      follows the heading.
        const int h = bf.headingLevel();
        if (h > 0) {
            c.setPosition(b.position());
            c.setPosition(b.position() + b.length() - 1,
                          QTextCursor::KeepAnchor);
            QTextCharFormat cf;
            cf.setFontWeight(QFont::DemiBold);   // 600
            c.mergeCharFormat(cf);

            // Heading text is not reinterpreted as list syntax here. For
            // example, "# 1. 一级标题测试" remains flush with the preview
            // column; genuine list-item headings are handled by the sentinel
            // path above.
        }

        // ---- code blocks: grey panel, monospace. Fenced blocks carry
        //      BlockCodeLanguage/Fence; 4-space indented blocks are a plain
        //      block with indent + fixed-pitch font.
        const bool fenced = bf.hasProperty(QTextFormat::BlockCodeLanguage)
            || bf.hasProperty(QTextFormat::BlockCodeFence);
        const bool indentedCode = bf.indent() > 0
            && b.length() > 0
            && b.begin().fragment().charFormat().fontFixedPitch();
        if (fenced || indentedCode) {
            // Fenced runs carry their own opaque rounded MdCodeFrame overlay;
            // an in-doc panel behind it would peek out around the frame as a
            // second, square slab. Indented code has no overlay, keep its panel.
            if (!fenced)
                bf.setBackground(kCanvasSubtle);
            // Spacing between the code box and the surrounding context:
            // margins only on the FIRST/LAST block of a contiguous code run,
            // so the lines inside one block stay tightly packed (12px margin
            // + the frame's 3px border pad ~ one 16px text line, GitHub's
            // 1em gap between text and code boxes).
            const bool firstOfRun = !b.previous().isValid()
                || !isPreviewCodeBlock(b.previous());
            const bool lastOfRun = !b.next().isValid()
                || !isPreviewCodeBlock(b.next());
            bf.setTopMargin(firstOfRun ? 12 : 0);
            bf.setBottomMargin(lastOfRun ? 12 : 0);
            // A fenced block inside a blockquote keeps the quote's own left
            // margin (set by the blockquote pass above). Overwriting it with
            // the top-level 12px inset would pull the code frame out to the
            // left of the quote's body text; and because sizeCodeFrame()
            // anchors the frame on textStart - a blockBoundingRect(), which
            // excludes margins entirely - the frame would then sit at the pane
            // edge while the quoted paragraphs sit at the quote margin. Only a
            // top-level fence gets the 12px inset.
            const bool inQuote = bf.hasProperty(QTextFormat::BlockQuoteLevel);
            bf.setLeftMargin(inQuote ? kQuoteLeftMargin : 12);
            bf.setRightMargin(12);
            // Pack code **lines** tightly: force SingleHeight so the block uses
            // the exact code-font line height with no added leading above each
            // text line (other line-height types add a leading sliver that reads
            // as extra spacing in a dense console/CMake listing). The first
            // argument is ignored for SingleHeight.
            if (b.length() > 0)
                bf.setLineHeight(0, QTextBlockFormat::SingleHeight);
            c.setPosition(b.position());
            c.setBlockFormat(bf);
            if (fenced) {
                // The opaque MdCodeFrame overlay re-renders fenced code on
                // top of these blocks, but Qt lays fenced lines out with a
                // per-block NoWrap text option: an over-long line (e.g. the
                // 1904px-wide `<?xml-model ...?>` row) stretches PAST the
                // frame's right edge and its tail shows in the doc layer
                // underneath - the same line appears a second time at the
                // box edge. Blank the in-doc copy; the frame is the only
                // visible renderer. Block rects (frame geometry) are
                // untouched - only the glyphs disappear.
                c.setPosition(b.position());
                c.setPosition(b.position() + b.length() - 1,
                              QTextCursor::KeepAnchor);
                QTextCharFormat cf;
                cf.setForeground(Qt::transparent);
                // The MdCodeFrame overlay re-renders this run in Consolas at
                // kCodePixelSize; pin the in-doc copy to the SAME family AND
                // size so both layers produce identical line heights and
                // identical wrap points (a size or family mismatch changes the
                // line height and makes code taller or shorter than its frame).
                cf.setFontFamilies({QStringLiteral("Consolas"),
                                    QStringLiteral("Courier New")});
                cf.setProperty(QTextFormat::FontPixelSize, kCodePixelSize);
                c.mergeCharFormat(cf);
            }
        }
    }

    // ---- character-level: inline code chips, links, strike-through.
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        // Fenced code is painted by its MdCodeFrame overlay only; the
        // in-doc copy was blanked above. Skip it here: where the
        // importer's "monospace" family resolves to a fixed-pitch font,
        // the chip branch below would re-merge a visible foreground over
        // the transparent one and the sliver would come back.
        const QTextBlockFormat bfmt = b.blockFormat();
        if (bfmt.hasProperty(QTextFormat::BlockCodeLanguage)
            || bfmt.hasProperty(QTextFormat::BlockCodeFence))
            continue;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            QTextCharFormat cf = frag.charFormat();
            if (cf.isAnchor() && !cf.anchorHref().isEmpty()) {
                cf.setForeground(kAccentFg);
            } else if (cf.fontFixedPitch()
                       || cf.fontFamilies().toStringList().contains(
                           QStringLiteral("Courier New"))) {
                // Inline code: subtle grey chip + monospace.
                cf.setBackground(kNeutralMuted);
                cf.setFontFamilies({QStringLiteral("Consolas"),
                                    QStringLiteral("Courier New")});
            } else {
                continue;
            }
            c.setPosition(frag.position());
            c.setPosition(frag.position() + frag.length(),
                          QTextCursor::KeepAnchor);
            c.mergeCharFormat(cf);
        }
    }

    // ---- horizontal rule colour: Qt paints it from the block's
    //      background brush.
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();
        if (bf.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
            bf.setBackground(kBorderDefault);
            bf.setTopMargin(24);
            bf.setBottomMargin(24);
            c.setPosition(b.position());
            c.setBlockFormat(bf);
        }
    }
}

// Rounded, in-preformatted-box code frames (see MdCodeFrame below) track these.
class MdCodeFrame;

// QTextBrowser subclass that draws the GitHub-style decorations that Qt's
// rich text cannot express: the h1/h2 underline rule and the blockquote
// left bar (both keyed off block properties set by setMarkdown).
class MdPreviewBrowser : public QTextBrowser {
public:
    explicit MdPreviewBrowser(QWidget* parent = nullptr)
        : QTextBrowser(parent)
    {
    }

    // Re-fit every embedded picture to half this pane's current content width,
    // preserving each picture's aspect ratio. Cheap when nothing changed, and
    // cheap enough to call on every render/resize; also invoked once a remote
    // image finally arrives (it could not be sized while its size was unknown).
    void fitImagesToHalfWidth();

    // Rebuild the rounded, no-wrap preview box for every fenced
    // code range. Called at the end of each render; geometry is stored in doc
    // coordinates and only (re)lied-out by relayoutFrames().
    void syncCodeFrames();
    // Cheap pass: move/hide each code frame to its block's on-screen rect using
    // the live scrollbar values. Runs on every vertical scroll/resize.
    void relayoutFrames();

    // Re-derive every code slot's doc-space rect from the live layout after a
    // re-layout (resize, late polish); full syncCodeFrames() when the fence-run
    // structure itself changed.
    void refreshCodeGeom();

protected:
    // The rendered page never slides sideways: the horizontal component of
    // any wheel gesture (trackpad x-swipe, shift+wheel) is dropped; only the
    // vertical delta may scroll the page. QWheelEvent deltas are read-only,
    // so sideways gestures are handled here instead of the base class.
    void wheelEvent(QWheelEvent* event) override
    {
        const QPoint d = event->angleDelta();
        if (d.x() == 0) {
            QTextBrowser::wheelEvent(event);   // pure vertical: standard path
            return;
        }
        if (d.y() == 0) {
            event->accept();   // sideways-only gesture: nothing to scroll
            return;
        }
        // Diagonal gesture: scroll vertically only (same math as the base).
        if (QScrollBar* vb = verticalScrollBar())
            vb->setValue(vb->value() - d.y());
        event->accept();
    }

    void paintEvent(QPaintEvent* event) override
    {
        QTextBrowser::paintEvent(event);

        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing, true);
        const int xOff = horizontalScrollBar()->value();
        const int yOff = verticalScrollBar()->value();
        const QRect view = viewport()->rect();
        QAbstractTextDocumentLayout* layout = document()->documentLayout();

        // Gather, per visible block, its h1/h2 underline and blockquote bars.
        // Blockquote bars are collected so ADJACENT quote blocks merge into one
        // continuous vertical bar (GitHub renders a run of > lines as a single
        // pipe, not a dashed one) instead of leaving a bleed seam between them.
        struct Bar { qreal x, top, bottom; };
        QVector<Bar> bars;
        qreal prevQuoteBottom = -1e9;
        qreal prevQuoteX = -1e9;
        bool prevQuote = false;

        for (QTextBlock b = document()->begin(); b != document()->end();
             b = b.next()) {
            if (!b.isValid() || !b.isVisible())
                continue;
            const QTextBlockFormat bf = b.blockFormat();
            const QRectF r = layout->blockBoundingRect(b);
            const qreal top = r.top() - yOff;
            const qreal bottom = r.bottom() - yOff;
            if (bottom < 0 || top > view.height())
                continue;   // off-screen

            // h1/h2 underline rule below the heading (drawn immediately).
            if (bf.headingLevel() == 1 || bf.headingLevel() == 2) {
                const qreal y = bottom - 2;
                p.fillRect(QRectF(0, y, view.width(), 1.0),
                           QColor("#D8DEE4"));
            }

            // blockquote 4px left bar — record (optionally extending the run of
            // the previous bar if vertically contiguous and on the same x).
            if (bf.hasProperty(QTextFormat::BlockQuoteLevel)) {
                const qreal x = r.left() - xOff;
                // The gap to bridge is measured between TEXT spans, not block
                // rects: blockBoundingRect() excludes block margins, and a
                // fenced code run carries a 12px run-edge margin above its
                // first line and below its last (see polishPreviewDocument).
                // Measuring rect-to-rect left those 12px gaps unbridged, so the
                // quote bar visibly broke on both sides of a quoted code block.
                // Grow each block's span by its own margins, then allow the
                // small slack the old code had for fractional layout.
                const qreal spanTop = top - bf.topMargin();
                const qreal spanBottom = bottom + bf.bottomMargin();
                if (prevQuote && qAbs(x - prevQuoteX) < 0.5
                    && spanTop <= prevQuoteBottom + 1.0) {
                    // Contiguous with the previous quote: just extend it.
                    if (!bars.isEmpty())
                        bars.last().bottom =
                            qMax(bars.last().bottom, spanBottom);
                    prevQuoteBottom = qMax(prevQuoteBottom, spanBottom);
                    prevQuoteX = x;
                    continue;
                }
                bars.append({x, spanTop, spanBottom});
                prevQuote = true;
                prevQuoteX = x;
                prevQuoteBottom = spanBottom;
            } else {
                prevQuote = false;
            }
        }

        // Draw the merged bars — a run is one rectangle, top edge overlaps the
        // preceding bar's occupied area so no hairline seam shows.
        for (const auto& bar : std::as_const(bars)) {
            const qreal h = bar.bottom - bar.top;
            p.fillRect(QRectF(bar.x, bar.top + 1, 4.0,
                              h > 2 ? h - 2 : h),
                       QColor("#D0D7DE"));
        }
    }

    // Re-size pictures when the pane's width changes, so they stay at half the
    // current width (defined here because fitImagesToHalfWidth() is declared
    // above; see its definition after MdPreviewDocument).
    void resizeEvent(QResizeEvent* event) override;

private:
    struct FrameGeom {
        qreal docTop = 0, docBottom = 0;    // frame top/bottom edge, doc space
        qreal contentX = 0, slotWidth = 0;  // frame left edge / width, doc space
    };
    QVector<MdCodeFrame*> m_codeFrames;   // live code boxes, parented to viewport
    QVector<FrameGeom> m_codeGeom;         // doc-space geometry per frame (paired)
    bool m_barHooked = false;              // vertical bar->relayout connected once
    // Build one frame from a contiguous fence run [from .. endEx exclusive).
    void finalizeFence(const QTextBlock& from, const QTextBlock& endEx,
                       QAbstractTextDocumentLayout* lay);
    // True for a block inside a fenced code run (language or fence marker).
    static bool isFencedBlock(const QTextBlock& b);
    // Re-derive the doc-space rect of the fence run [from .. endEx) from
    // the live in-doc layout. The frame's chrome is the constant declared
    // by MdCodeFrame's own stylesheet (measuring it off the frame's live
    // viewport is useless here - the frame is hidden, and a hidden widget's
    // viewport is not resized by setGeometry), and the frame's height is
    // the in-doc run's own line span, which the frame reproduces exactly
    // by comparing it with the code frame document's own laid-out height.
    void sizeCodeFrame(MdCodeFrame* f, const QTextBlock& from,
                       const QTextBlock& endEx,
                       QAbstractTextDocumentLayout* lay, FrameGeom& g);
};

// A rounded, per-fence code display. NOT used for flow text: it draws a single
// fenced code region already positioned in MdPreviewBrowser (doc grey slot).
// Because Qt gives no per-block rounding, the code is a real child QTextEdit
// NEAT on top of that slot. Long lines do NOT wrap: a line wider than the
// box slides sideways by gesture (shift+wheel / trackpad x-swipe / touch
// swipe) - no bar is ever shown - and the content is pinned to exactly the
// slot's height, so nothing scrolls vertically either.
// Rounded chrome comes from the stylesheet.
class MdCodeFrame : public QTextEdit {
public:
    MdCodeFrame(QTextBrowser* host, QWidget* parent)
        : QTextEdit(parent)
    {
        m_host = host;
        setReadOnly(true);
        setFrameShape(QFrame::NoFrame);
        // No scrollbars, ever: the content is pinned to the slot's height
        // (sizeCodeFrame), so nothing overflows downwards. Long lines do NOT
        // wrap - a line wider than the box still slides sideways, but only
        // by gesture (shift+wheel, trackpad x-swipe, touch swipe): the
        // scrollbar range is live even while the bar stays hidden.
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QTextOption to = document()->defaultTextOption();
        to.setWrapMode(QTextOption::NoWrap);
        document()->setDefaultTextOption(to);
        QFont f(QStringLiteral("Consolas"));
        f.setPixelSize(kCodePixelSize);
        setFont(f);
        // The default 4px document margin silently eats 8px of the frame's
        // height (on top of the stylesheet border + padding) and clipped
        // the last line of every block; the visible padding is the
        // stylesheet one, so keep the document margin at zero.
        document()->setDocumentMargin(0);
        // Single background layer only; the rounded corners show because what is
        // behind this widget is transparent, not a second grey slab.
        setStyleSheet(
            "QTextEdit { background:#F6F8FA;"
            " border:1px solid #D0D7DE; border-radius:6px;"
            " padding:4px 6px; }");
    }

    QString text() const { return QTextEdit::toPlainText(); }

    // Widen keystrokes handle no edit.
    void keyPressEvent(QKeyEvent* e) override { (void)e; /* read-only */ }

    // Wheel: a horizontal component (trackpad x-swipe, shift+wheel) slides
    // the frame sideways while a line overflows the box. A vertical wheel
    // reaches the OUTER page: the content is exactly as tall as the slot, so
    // the frame never overflows vertically (the vb branch below stays as a
    // safety net in case the pinning math ever drifts).
    void wheelEvent(QWheelEvent* e) override
    {
        const QPoint d = e->angleDelta();
        if (d.x() != 0) {
            if (QScrollBar* hb = horizontalScrollBar()) {
                if (hb->maximum() > 0) {
                    hb->setValue(hb->value() - d.x());
                    e->accept();
                    return;
                }
            }
        }
        if (d.y() != 0) {
            if (QScrollBar* vb = verticalScrollBar()) {
                if (vb->maximum() > 0) {
                    // content taller than the frame: scroll it internally
                    vb->setValue(vb->value() - d.y());
                    e->accept();
                    return;
                }
            }
        }
        e->ignore();   // bubble: let the outer QTextBrowser page scroll
    }

    // Touch: a single-finger sideways swipe slides the frame while a line
    // overflows the box; a mostly-vertical swipe hands off to the outer page
    // (the frame itself never overflows vertically). Multi-touch falls
    // through to the native handler. QWidget::touchEvent() is not virtual, so
    // touch types arrive
    // via event(); anything not consumed here drops to the base class.
    bool event(QEvent* e) override
    {
        switch (e->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            if (handleTouch(static_cast<QTouchEvent*>(e)))
                return true;
            break;
        default:
            break;
        }
        return QTextEdit::event(e);
    }

    void setText(const QString& s) { setPlainText(s); update(); }

private:
    QTextBrowser* m_host = nullptr;
    QPointF m_touchPos;         // previous single-touch position (slide delta)

    // True when this frame consumed the touch gesture.
    bool handleTouch(QTouchEvent* e)
    {
        if (e->type() != QEvent::TouchBegin
            && e->type() != QEvent::TouchUpdate) {
            m_touchPos = QPointF();   // gesture over: forget the last point
            return false;
        }
        const auto pts = e->points();
        if (pts.size() != 1) {        // multi-touch: let the base class cope
            m_touchPos = QPointF();
            return false;
        }
        const QPointF pos = pts.first().position();
        if (m_touchPos.isNull()) {    // first sample of this gesture
            m_touchPos = pos;
            return true;
        }
        const QPointF delta = pos - m_touchPos;
        m_touchPos = pos;

        // Sideways swipe: slide the frame while its content overflows the
        // box; otherwise eat the sample (nothing to slide).
        if (qAbs(delta.x()) > qAbs(delta.y())) {
            if (QScrollBar* hb = horizontalScrollBar()) {
                if (hb->maximum() > 0)
                    hb->setValue(hb->value() - int(delta.x()));
            }
            return true;
        }

        if (QScrollBar* vb = verticalScrollBar()) {
            if (vb->maximum() > 0) {
                vb->setValue(vb->value() - int(delta.y()));
                return true;
            }
        }
        if (m_host) {
            // No in-frame overflow: hand the vertical slide to the page.
            if (QScrollBar* pv = m_host->verticalScrollBar())
                pv->setValue(pv->value() - int(delta.y()));
        }
        return true;
    }
};

// Preview document that resolves the image files the note references.
// setMarkdown() stores an image's src verbatim and defers to
// loadResource(); the default implementation only understands resource
// and data: URLs, so pasted or hand-typed local paths (backslashes,
// spaces, percent-encoding, relative refs) and remote http(s) URLs
// (e.g. Yuque CDN links pasted as <img> or ![](...)) would render as
// blank placeholders. This override works local paths out against disk
// and fetches remote pictures off the network.
//
// Local files resolve synchronously off disk. Remote pictures are fetched
// ASYNCHRONOUSLY: loadResource() runs on the GUI thread during layout and
// must never block, so it returns whatever is cached now and asks the shared
// net access manager to grab the bytes in the background. When the reply
// lands, the document(s) that asked for it are relaid out (scheduleRefresh)
// so the picture pops in on its own — no more freezing the editor while a
// remote URL loads, and a slow/failed CDN no longer blanks the note until
// the next keystroke.
class MdPreviewDocument : public QTextDocument {
public:
    using QTextDocument::QTextDocument;

    // Optional hook invoked when a remote picture this document asked for has
    // finished loading (success or settled failure) and a relayout will now
    // pick it up from the shared cache. The owning QTextBrowser sets this to
    // mark the document dirty and repaint its viewport.
    std::function<void()> scheduleRefresh;

    // The decoded pixels currently known for a note image address: a local file
    // is decoded off disk here, a remote URL is served from its cache (or null
    // while the async download is still in flight). Used to keep aspect ratios
    // when picture boxes are resized to fit the preview width.
    static QImage imageFor(const QUrl& url);

protected:
    QVariant loadResource(int type, const QUrl& name) override
    {
        QVariant value = QTextDocument::loadResource(type, name);
        if (type == QTextDocument::ImageResource && value.isNull()) {
            const bool remote =
                name.scheme() == QLatin1String("http")
                || name.scheme() == QLatin1String("https");
            const QImage img = remote ? fetchRemoteImage(this, name)
                                      : resolveLocalImage(name);
            if (!img.isNull())
                value = img;
        }
        return value;
    }

private:
    // ---- shared, per-URL async download state (module-wide) ----
    struct RemoteBrew {
        QImage  image;    // fully decoded, or null while absent / failed
        qint64  failedAt = 0;   // ms window of the last failure (retry cooldown)
        bool    loading  = false;
    };
    // Short failure cooldown: long enough to stop re-hammering a dead URL on
    // every render, short enough that a transient outage recovers quickly.
    static constexpr qint64 kFailCooldownMs = 5 * 1000;

    static QHash<QString, RemoteBrew> s_remote;
    // Documents waiting on each URL so they can relayout once it resolves.
    static QHash<QString, QVector<QPointer<MdPreviewDocument>>> s_waiters;
    static QNetworkAccessManager s_nam;

    // ---- on-disk cache so a note's remote pictures load instantly on later
    //      launches instead of re-downloading over the (slow) network each time.
    static void saveRemoteDisk(const QString& key, const QByteArray& data);
    // Decoded (and long-edge-clamped) pixels already saved for `key`, or null.
    static QImage diskRemote(const QString& key);
    // Directory the cached raw bytes live in (content-addressed by SHA-1).
    static QString remoteDiskFile(const QString& key);

    // Clamp the longest edge so we never hold / paint an enormous bitmap.
    static QImage cappedRemote(QImage image);

    // Fire a remote request for `user` (async). Each document that needs the
    // result registers itself first so it is repainted on arrival.
    static void ensureRemoteLoading(MdPreviewDocument* self,
                                    const QString& key, const QUrl& url)
    {
        RemoteBrew& r = s_remote[key];

        // Register this viewer so it is woken when the picture arrives (or a
        // retry finally settles), for URLs it has not happily cached yet.
        if (!r.image.isNull())   // already there: nothing to wait for
            return;
        QVector<QPointer<MdPreviewDocument>>& w = s_waiters[key];
        if (!w.contains(self))
            w.append(self);

        // Don't start a second download while one is in flight, and respect
        // the failure cooldown so a hard-down host isn't re-requested on every
        // render.
        if (r.loading)
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (r.failedAt != 0 && now - r.failedAt < kFailCooldownMs)
            return;   // retried later by the next natural render

        r.loading = true;
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("Mozilla/5.0 (Windows NT 10.0;"
                                         " Win64; x64) Genesis/0.1"));
        request.setTransferTimeout(20 * 1000);
        QNetworkReply* reply = s_nam.get(request);
        QObject::connect(reply, &QNetworkReply::finished, reply, [key, reply]() {
            const bool ok = reply->error() == QNetworkReply::NoError;
            const QByteArray data = ok ? reply->readAll() : QByteArray();
            QImage image = ok && !data.isEmpty() ? QImage::fromData(data)
                                                 : QImage();
            if (!image.isNull())
                image = cappedRemote(image);

            RemoteBrew& r = s_remote[key];
            r.loading = false;
            if (!image.isNull()) {
                r.image = image;
                r.failedAt = 0;
                // Keep it on disk so the next launch skips the network trip.
                if (!data.isEmpty())
                    saveRemoteDisk(key, data);
            } else {
                r.failedAt = QDateTime::currentMSecsSinceEpoch();
            }
            reply->deleteLater();

            // Wake every viewer that asked for this URL so a relayout now
            // serves the freshly cached bytes (or shows a settled blank).
            const QVector<QPointer<MdPreviewDocument>> waiters =
                s_waiters.take(key);
            for (const auto& d : waiters)
                if (d && d->scheduleRefresh)
                    d->scheduleRefresh();
        });
    }

    static QImage fetchRemoteImage(MdPreviewDocument* self, const QUrl& url)
    {
        const QString key = url.toString();
        RemoteBrew& r = s_remote[key];
        if (!r.image.isNull())          // hot (this session)
            return r.image;

        // Already downloaded in a previous session: read straight from disk —
        // no network wait, so returning the picture is near-instant.
        const QImage disk = diskRemote(key);
        if (!disk.isNull()) {
            r.image = disk;
            r.failedAt = 0;
            return disk;
        }
        ensureRemoteLoading(self, key, url);
        return r.image;   // empty until the async reply lands
    }

    static QImage resolveLocalImage(const QUrl& name)
    {
        // Spellings — every form the path may have reached us in: the
        // local-file form, the raw string, and percent-decoded twins
        // ("My%20Documents" → "My Documents").
        QStringList spellings;
        const auto add = [&spellings](QString s) {
            s.replace(QLatin1Char('\\'), QLatin1Char('/'));
            if (s.isEmpty() || s.startsWith(QLatin1String(":/"))
                || s.contains(QLatin1String("://")))
                return;
            if (!spellings.contains(s))
                spellings << s;
            const QString decoded = QUrl::fromPercentEncoding(s.toUtf8());
            if (decoded != s && !spellings.contains(decoded))
                spellings << decoded;
        };
        if (name.isLocalFile())
            add(name.toLocalFile());
        add(name.toString());

        // Resolve to one existing absolute path (cheap), then hand it to the
        // decoding cache. Stop at the first candidate that decodes.
        const auto tryAbs = [](const QString& p) -> QImage {
            const QFileInfo fi(p);
            if (!fi.isFile())
                return QImage();
            return decodeLocalImage(p);
        };
        for (const QString& sp : spellings) {
            const QImage img = tryAbs(sp);
            if (!img.isNull())
                return img;
            // Relative refs: the working directory, the notes folder, and its
            // images subfolder (where pasted pictures live).
            if (QFileInfo(sp).isRelative()) {
                const QString notes =
                    QStandardPaths::writableLocation(
                        QStandardPaths::AppDataLocation)
                    + QStringLiteral("/notes");
                const QStringList bases = {QDir::currentPath(), notes,
                                           notes + QStringLiteral("/images")};
                for (const QString& base : bases) {
                    const QImage r =
                        tryAbs(QDir(base).filePath(sp));
                    if (!r.isNull())
                        return r;
                }
            }
        }
        return QImage();
    }

    // ---- local-file picture cache + decoding cap (keeps big screenshots from
    //      being re-decoded on every render, and Qt from software-scaling a
    //      4K bitmap on every scroll/paint frame). Decoded once per file,
    //      down-scaled so a scroll never repaints a huge original. ----
    static constexpr int kLocalCapLongEdge = 1600;
    struct LocalEntry {
        QImage  im;
        qint64  fileTime = 0;
        qint64  fileSize = 0;
    };
    static QHash<QString, LocalEntry> s_local;
    static QImage decodeLocalImage(const QString& path);
};

QHash<QString, MdPreviewDocument::RemoteBrew> MdPreviewDocument::s_remote;
QHash<QString, QVector<QPointer<MdPreviewDocument>>>
    MdPreviewDocument::s_waiters;
QNetworkAccessManager MdPreviewDocument::s_nam;
QHash<QString, MdPreviewDocument::LocalEntry> MdPreviewDocument::s_local;

QImage MdPreviewDocument::decodeLocalImage(const QString& path)
{
    LocalEntry& e = s_local[path];
    const QFileInfo fi(path);
    const qint64 ft = fi.lastModified().toMSecsSinceEpoch();
    const qint64 fz = fi.size();
    // Serve the cached decode while the file is unchanged on disk.
    if (!e.im.isNull() && e.fileTime == ft && e.fileSize == fz)
        return e.im;
    e.fileTime = ft;
    e.fileSize = fz;
    e.im = QImage(path);
    if (e.im.isNull())
        return e.im;   // (leave the empty entry cached; file is not a picture)
    // Cap the longest edge so the document never holds (nor re-scales on
    // paint) an enormous bitmap — a 4K screenshot otherwise makes every
    // scroll/paint rescale it in software and stutter.
    const qreal maxDim = qreal(kLocalCapLongEdge);
    const qreal w = e.im.width();
    const qreal h = e.im.height();
    const qreal longest = qMax(w, h);
    if (longest > maxDim) {
        const qreal k = maxDim / longest;
        e.im = e.im.scaled(qMax(1, int(w * k)), qMax(1, int(h * k)),
                           Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return e.im;
}

QString MdPreviewDocument::remoteDiskFile(const QString& key)
{
    // <AppData>/Genesis/notes/imgcache/<sha1-of-url>.bin
    const QByteArray hash =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1)
            .toHex();
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/notes/imgcache/")
        + QString::fromLatin1(hash) + QStringLiteral(".bin");
}

void MdPreviewDocument::saveRemoteDisk(const QString& key,
                                       const QByteArray& data)
{
    if (data.isEmpty())
        return;
    const QString fp = remoteDiskFile(key);
    QDir().mkpath(QFileInfo(fp).absolutePath());
    QFile f(fp);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(data);
}

QImage MdPreviewDocument::cappedRemote(QImage image)
{
    if (image.isNull())
        return image;
    const qreal longest = qMax(qreal(image.width()), qreal(image.height()));
    const qreal maxDim = qreal(kLocalCapLongEdge);
    if (longest > maxDim) {
        const qreal k = maxDim / longest;
        image = image.scaled(qMax(1, int(image.width() * k)),
                             qMax(1, int(image.height() * k)),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

QImage MdPreviewDocument::diskRemote(const QString& key)
{
    QFile f(remoteDiskFile(key));
    if (!f.open(QIODevice::ReadOnly))
        return QImage();
    const QByteArray data = f.readAll();
    if (data.isEmpty())
        return QImage();
    return cappedRemote(QImage::fromData(data));
}

QImage MdPreviewDocument::imageFor(const QUrl& url)
{
    if (url.scheme() == QLatin1String("http")
        || url.scheme() == QLatin1String("https"))
        return s_remote.value(url.toString()).image;   // null until the reply lands
    return resolveLocalImage(url);
}

// Redraw every embedded picture in the preview document so it is exactly
// `colWidth` pixels wide (the pane already supplies half its width) and its
// height follows the source's aspect ratio. Images whose source is still
// unknown (a remote picture not yet downloaded) are left at their natural
// size; they are fitted again the moment the bytes arrive.
void fitPreviewImagesToWidth(QTextDocument* doc, int colWidth)
{
    if (!doc || doc->isEmpty() || colWidth <= 0)
        return;
    QTextCursor c(doc);
    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || !f.charFormat().isImageFormat())
                continue;
            QTextImageFormat imgFmt = f.charFormat().toImageFormat();
            const QImage src =
                MdPreviewDocument::imageFor(QUrl(imgFmt.name()));
            const int srcW = src.width();
            const int srcH = src.height();
            const int want = srcW > 0 && srcH > 0 ? colWidth : 0;
            if (want <= 0)
                continue;   // dimension unknown yet — retry when it arrives
            const int boxH = qMax(1, (int)(srcH * (qreal)colWidth / srcW));
            if (qAbs((int)imgFmt.width() - colWidth) < 1
                && qAbs((int)imgFmt.height() - boxH) < 1)
                continue;   // already right-sized this layout
            imgFmt.setWidth(colWidth);
            imgFmt.setHeight(boxH);
            c.setPosition(f.position());
            c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
            c.mergeCharFormat(imgFmt);
        }
    }
}

void MdPreviewBrowser::fitImagesToHalfWidth()
{
    // Refresh for the case the text width is not yet final (first layout).
    QAbstractTextDocumentLayout* lay = document()->documentLayout();
    if (lay)
        lay->documentSize();
    const int avail = viewport() ? viewport()->width() : width();
    if (avail <= 10)
        return;
    fitPreviewImagesToWidth(document(), avail / 2);
    // Force a layout pass so the new box sizes take effect, then repaint.
    document()->markContentsDirty(0, document()->characterCount());
    if (QAbstractTextDocumentLayout* lay = document()->documentLayout())
        lay->documentSize();
    if (QWidget* vp = viewport())
        vp->update();
}

void MdPreviewBrowser::relayoutFrames()
{
    if (m_codeFrames.isEmpty())
        return;
    QAbstractTextDocumentLayout* lay = document()->documentLayout();
    if (!lay)
        return;
    const int yBar = verticalScrollBar() ? verticalScrollBar()->value() : 0;
    const int xBar = horizontalScrollBar() ? horizontalScrollBar()->value() : 0;
    const int vpHeight = viewport() ? viewport()->height() : 0;
    // Doc-space -> viewport uses the same transform paintEvent() applies to
    // its decorations: point minus the live scrollbar values.
    for (int i = 0; i < m_codeFrames.size() && i < m_codeGeom.size(); ++i) {
        MdCodeFrame* f = m_codeFrames[i];
        const FrameGeom& g = m_codeGeom[i];
        const int yTop = int(g.docTop) - yBar;
        const int xLeft = int(g.contentX) - xBar;
        const bool show = (int(g.docBottom) - yBar) > 0
                          && yTop < vpHeight;
        if (show) {
            f->setGeometry(xLeft, yTop, int(g.slotWidth),
                           int(g.docBottom - g.docTop));
            f->raise();
            f->show();
        } else {
            f->hide();
        }
    }
}

void MdPreviewBrowser::syncCodeFrames()
{
    // Free any previous overlay frames.
    for (MdCodeFrame* f : std::as_const(m_codeFrames))
        delete f;
    m_codeFrames.clear();
    m_codeGeom.clear();

    QAbstractTextDocumentLayout* lay = document()->documentLayout();
    if (!lay)
        return;
    // Walk contiguous fenced runs; one rounded overlay per run.
    QTextBlock run;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        const bool fenc = isFencedBlock(b);
        if (fenc && !run.isValid())
            run = b;
        if (!fenc && run.isValid()) {
            finalizeFence(run, b, lay);   // run .. b-1
            run = QTextBlock();
        }
    }
    if (run.isValid())
        finalizeFence(run, QTextBlock(), lay);
    relayoutFrames();
}

bool MdPreviewBrowser::isFencedBlock(const QTextBlock& b)
{
    const QTextBlockFormat bf = b.blockFormat();
    return bf.hasProperty(QTextFormat::BlockCodeLanguage)
        || bf.hasProperty(QTextFormat::BlockCodeFence);
}

void MdPreviewBrowser::sizeCodeFrame(MdCodeFrame* f, const QTextBlock& from, const QTextBlock& endEx, QAbstractTextDocumentLayout* lay, FrameGeom& g)
{
    g = FrameGeom();
    if (!lay || !from.isValid())
        return;
    QTextDocument* doc = document();
    const QTextBlockFormat bf = from.blockFormat();
    const qreal indent = bf.indent() * doc->indentWidth();

    // The chrome (kChromeH/kChromeV, declared at file scope next to the
    // property they pair with) is exactly what MdCodeFrame's stylesheet
    // declares (1px border + 4px/6px padding, applied identically by
    // QStyleSheetStyle on every platform). It must NOT be measured off the
    // frame's live viewport: the frame is hidden while this runs (a fresh
    // overlay, or a reused one being re-sized), and a hidden widget does not
    // process resize events - its viewport keeps its stale (0x0 on first
    // build) geometry - so the measured chrome collapses to garbage and the
    // frame comes out the wrong size (the "box far taller than its code" bug).

    // Anchor on the code text's doc-space left edge. blockBoundingRect()
    // reports the block's CONTENT box, whose left is the document margin and
    // whose RIGHT is what the left margin shrinks - the left margin never
    // appears in rect.left(). Add it back explicitly, so a fenced block inside
    // a blockquote (leftMargin = kQuoteLeftMargin) anchors level with the
    // quote's body text instead of out at the pane edge. `indent` is the same
    // kind of offset for an indented (list-nested) block.
    const QRectF firstRect = lay->blockBoundingRect(from);
    const qreal textStart = firstRect.left() + bf.leftMargin() + indent;
    // blockBoundingRect() returns the block's TEXT-LINE rect: block margins
    // live in the gaps BETWEEN rects, never inside them (a block whose only
    // line is 13px tall reports top=0/bottom=13 even with a 12px top margin
    // and a 31px bottom margin). The code span is therefore just the rect
    // span, first line top to last line bottom. Do NOT add the first block's
    // top margin or subtract the last block's bottom margin: the earlier code
    // did both and they cancel only for a run of TWO OR MORE lines. On a
    // SINGLE-line fence from == last, so lineTop gained the 12px run-edge
    // margin while lineBottom lost that same margin's worth - the span
    // collapsed to <= 0, requiredExtra came out as the whole content height,
    // and the box was sized ~5px shorter than its code.
    const qreal lineTop = firstRect.top();
    // The frame's OUTER box must fit inside the pane's text column, so its
    // right edge is simply the column's right edge (text width less the block's
    // right gutter) - the same boundary the body text wraps at. Nothing is
    // added for the right-hand chrome: the chrome lives INSIDE this box, and
    // adding another kChromeH here overhung the viewport by kChromeH -
    // rightMargin and clipped the frame's right border and rounded corner.
    // In-doc fenced lines are laid out NoWrap (the importer's per-block text
    // option), so the code text uses the whole column and lines wider than the
    // box slide sideways inside the frame instead of wrapping.
    const qreal frameRight = firstRect.left() + doc->textWidth()
        - bf.rightMargin();

    // The slot height is the in-doc run's own line span: first line top to
    // last line bottom, run-edge margins excluded. It is read off the
    // ALREADY Laid-out browser document (the same source the frame position
    // comes from), not off the frame's own document, which is not laid out
    // yet while the frame is hidden. The frame document is laid out separately
    // below and can enlarge this span when its real font metrics need more room.
    QTextBlock last = from;
    for (QTextBlock b = from.next(); b.isValid() && b != endEx;
         b = b.next())
        last = b;
    const QTextBlockFormat lbf = last.blockFormat();
    const QRectF lastRect = lay->blockBoundingRect(last);
    const qreal previousExtra = lbf.property(
        kCodeFrameExtraBottomProperty).toReal();
    // lastRect.bottom() is the last text line's bottom; the reserved extra
    // from the previous pass sits BELOW it, in the gap before the next block,
    // and so is already excluded. Nothing to subtract.
    const qreal lineBottom = lastRect.bottom();
    const qreal hostContentHeight = qMax(0.0, lineBottom - lineTop);

    f->hide();   // relayoutFrames() places and re-shows it
    // The frame must keep its LEFT rounded border on screen. Its left edge is
    // placed at textStart - kChromeH, so the chrome (1px border + 6px padding)
    // sits to the left of the code text and that text stays aligned with the
    // surrounding paragraphs. That only works if the code text starts at least
    // kChromeH from the viewport's left edge; with Qt's default 4px document
    // margin it starts at 4 and the frame landed at x = -3 - its left border
    // and corner were clipped off (see createMarkdownPreview, which now sets
    // the document margin to kChromeH so this can never go negative).
    // kCodeFrameLeftInset then pushes the whole box further right so it reads
    // as inset inside the body column rather than flush against its edge. The
    // slot is narrowed by the same amount so the frame's RIGHT border stays
    // inside the viewport: shifting right without shrinking would push the
    // right edge (and its rounded corner) off-screen instead.
    // Inside a blockquote the inset is NOT applied: there the requirement is
    // for the code to line up with the quote's own body text, which the left
    // margin (folded into textStart above) already places correctly - adding
    // the inset on top would indent the code past the quoted paragraphs.
    const bool inQuote = bf.hasProperty(QTextFormat::BlockQuoteLevel);
    const qreal inset = inQuote ? 0.0 : qreal(kCodeFrameLeftInset);
    g.contentX = textStart - kChromeH + inset;
    // Width is whatever is left between the (inset) left edge and the column's
    // right edge computed above - never more, so the right border cannot
    // overhang the viewport.
    g.slotWidth = qMax(1.0, frameRight - g.contentX);
    // Round the outer edges outward so the int() casts in relayoutFrames()
    // can never shave the first/last code line by a sub-pixel.
    // When relayoutFrames() shows the frame at its slot it resizes the viewport
    // to slotWidth - 2*chrome. Set the document to that width before measuring
    // it; this works while the widget is hidden and does not depend on the
    // hidden viewport's stale geometry. The frame lays out NoWrap, so this
    // changes the sideways scroll range rather than the line count, but the
    // measurement must still describe the width the frame really gets.
    f->document()->setTextWidth(
        qMax(1, int(g.slotWidth) - int(2.0 * kChromeH)));
    f->document()->markContentsDirty(0, f->document()->characterCount());
    qreal frameContentHeight = 0;
    if (QAbstractTextDocumentLayout* frameLayout =
            f->document()->documentLayout()) {
        frameContentHeight = frameLayout->documentSize().height();
    }

    // Never compress the overlay to the host block span. FixedHeight line
    // formats can be a fraction shorter than the glyphs after DPI rounding or
    // font fallback, which clips the last code row. Keep the host span when it
    // is larger, but let the frame's actual laid-out content determine the
    // lower bound.
    const qreal requiredExtra = qMax(
        0.0, qreal(qCeil(frameContentHeight - hostContentHeight)));
    if (qAbs(requiredExtra - previousExtra) > 0.01) {
        QTextBlockFormat adjusted = lbf;
        adjusted.setBottomMargin(
            qMax(0.0, lbf.bottomMargin() - previousExtra) + requiredExtra);
        adjusted.setProperty(kCodeFrameExtraBottomProperty, requiredExtra);
        QTextCursor c(doc);
        c.setPosition(last.position());
        c.setBlockFormat(adjusted);
        lay->documentSize();
    }
    const qreal contentBottom = lineBottom + requiredExtra;
    g.docTop = qFloor(lineTop - kChromeV);
    g.docBottom = qCeil(contentBottom + kChromeV);
}

void MdPreviewBrowser::finalizeFence(const QTextBlock& from, const QTextBlock& endEx, QAbstractTextDocumentLayout* lay)
{
    // One frame line per in-doc code block (b.text() drops the block's
    // terminator). An EMPTY block - a blank code line - has no fragments but
    // still takes a row in the doc layout; the old fragment loop dropped it
    // and the frame's content ended up shorter than its in-doc run.
    QStringList lines;
    for (QTextBlock b = from; b.isValid() && b != endEx; b = b.next())
        lines << b.text();
    if (!from.isValid())
        return;
    MdCodeFrame* f = new MdCodeFrame(this, viewport());
    f->setText(lines.join(QLatin1Char('\n')));
    // frame stores geometry in doc coordinates; relayoutFrames applies scroll.
    FrameGeom g;
    sizeCodeFrame(f, from, endEx, lay, g);
    m_codeFrames.append(f);
    m_codeGeom.append(g);
    // parent==viewport: scrollbar policies follow relayout; add once
    if (!m_barHooked && verticalScrollBar()) {
        m_barHooked = true;
        connect(verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this]() { relayoutFrames(); });
    }
}

void MdPreviewBrowser::resizeEvent(QResizeEvent* event)
{
    QTextBrowser::resizeEvent(event);
    fitImagesToHalfWidth();
    // Base handler has already grown the viewport and re-textWidthed the
    // document: re-derive the slots from the fresh layout instead of just
    // re-placing stale geometry.
    refreshCodeGeom();
}

void MdPreviewBrowser::refreshCodeGeom()
{
    if (m_codeFrames.isEmpty())
        return;
    QAbstractTextDocumentLayout* lay = document()->documentLayout();
    if (!lay)
        return;
    lay->documentSize();
    // Collect the current fence runs; if the structure changed since the last
    // sync the frame list itself is stale, so do a full rebuild.
    QVector<QTextBlock> starts, ends;
    QTextBlock run;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        const bool fenc = isFencedBlock(b);
        if (fenc && !run.isValid())
            run = b;
        if (!fenc && run.isValid()) {
            starts.append(run);
            ends.append(b);
            run = QTextBlock();
        }
    }
    if (run.isValid()) {
        starts.append(run);
        ends.append(QTextBlock());
    }
    if (starts.size() != m_codeGeom.size()) {
        syncCodeFrames();
        return;
    }
    for (int i = 0; i < m_codeGeom.size(); ++i)
        sizeCodeFrame(m_codeFrames[i], starts[i], ends[i], lay, m_codeGeom[i]);
    relayoutFrames();
}

// Line-number gutter painted down the left edge of an MdSourceEditor (see
// setLineNumbersVisible). A bare paint surface: the editor owns all the state
// and does the drawing, which keeps this free of Q_OBJECT/moc (it lives in the
// file's anonymous namespace) and of any layout participation - the editor
// positions it by hand in resizeEvent, and the viewport is inset with
// setViewportMargins so the text never runs under the numbers.
class MdSourceEditor;

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(MdSourceEditor* editor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    MdSourceEditor* m_editor = nullptr;
};

// QPlainTextEdit subclass that shows spaces/tabs and auto-continues
// ordered-list numbering on Enter.
class MdSourceEditor : public QPlainTextEdit {
public:
    explicit MdSourceEditor(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
    {
        // Visible whitespace — spaces as faint middle-dots, tabs as arrows.
        QTextOption opt = document()->defaultTextOption();
        opt.setFlags(opt.flags() | QTextOption::ShowTabsAndSpaces);
        document()->setDefaultTextOption(opt);

        // Line-number gutter, hidden until setLineNumbersVisible(true).
        m_lineArea = new LineNumberArea(this);
        m_lineArea->hide();
        // QPlainTextEdit exposes these as signals, not virtuals (the Qt code
        // editor example overrides a subclass; here we connect instead).
        // A block-count change can cross a power of ten, so re-derive the
        // gutter width; the update request drives the scroll-sync repaint.
        connect(this, &QPlainTextEdit::blockCountChanged, this,
                [this](int) { updateLineNumberAreaWidth(); });
        connect(this, &QPlainTextEdit::updateRequest, this,
                [this](const QRect& rect, int dy) {
                    updateLineNumberArea(rect, dy);
                });
    }

    // Show/hide the left line-number gutter. Turning it off collapses the
    // viewport margin back to zero so no blank strip is left behind.
    void setLineNumbersVisible(bool visible)
    {
        if (visible == m_lineNumbersVisible)
            return;
        m_lineNumbersVisible = visible;
        // Place the gutter now rather than waiting for the next resizeEvent -
        // the editor may already be laid out (it usually is, by the time the
        // menu is opened).
        positionLineNumberArea();
        m_lineArea->setVisible(visible);
        updateLineNumberAreaWidth();
        if (visible) {
            m_lineArea->raise();
            m_lineArea->update();
        }
    }
    bool lineNumbersVisible() const { return m_lineNumbersVisible; }

    // Width of the gutter: one digit's worth plus EQUAL padding each side
    // (plus the 1px separator), growing with the line count so 9->10 and
    // 99->100 widen the strip instead of clipping. Symmetric padding is what
    // lets the number sit centred in the strip.
    int lineNumberAreaWidth() const
    {
        int digits = 1;
        for (int n = qMax(1, blockCount()); n >= 10; n /= 10)
            ++digits;
        return 2 * kGutterPad + 1
            + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    // Draws the numbers for the visible blocks. Called by LineNumberArea.
    void lineNumberAreaPaintEvent(QPaintEvent* event)
    {
        QPainter p(m_lineArea);
        p.fillRect(event->rect(), QColor("#F6F8FA"));
        // 1px separator so the gutter reads as a distinct strip.
        p.setPen(QColor("#E0E6ED"));
        p.drawLine(m_lineArea->width() - 1, event->rect().top(),
                   m_lineArea->width() - 1, event->rect().bottom());

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block)
                             .translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        const int rowHeight = fontMetrics().height();
        // The editor is disabled until a book is loaded; mute the digits then
        // (an explicit pen colour would otherwise stay full strength).
        const QColor fg = isEnabled() ? QColor("#8A94A0")
                                      : palette().color(QPalette::Disabled,
                                                        QPalette::Text);

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                p.setPen(fg);
                // Centre horizontally, but derive the VERTICAL position from the
                // block's own first text line rather than AlignVCenter: Qt
                // centres text on the font's full line box, which sits a few
                // pixels below where the document actually puts the glyphs, so
                // AlignVCenter left every number riding above its line. Placing
                // the number's baseline on the block's baseline keeps them in
                // step at any font or DPI.
                const QTextLine line = block.layout()
                                           ? block.layout()->lineAt(0)
                                           : QTextLine();
                const int ascent = line.isValid()
                                       ? qRound(line.ascent())
                                       : fontMetrics().ascent();
                const int baselineY = top + ascent;
                p.drawText(QRect(0, baselineY - ascent,
                                 m_lineArea->width() - 1, rowHeight),
                           Qt::AlignHCenter | Qt::AlignVCenter,
                           QString::number(blockNumber + 1));
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

    // Receives pasted images (Ctrl+V or the context menu). The handler
    // returns the Markdown to insert at the cursor; an empty return
    // cancels the paste.
    void setImagePasteHandler(std::function<QString(const QImage&)> handler)
    {
        m_imageHandler = std::move(handler);
    }

    // Receives pasted/dropped IMAGE FILES (copying a file in the file manager
    // or a browser, then Ctrl+V/drag) as one block of local paths. Those arrive
    // as text/uri-list and would otherwise collapse to a bare "file:///…" text
    // that never renders as a picture.
    void setImageFilePasteHandler(
        std::function<QString(const QStringList&)> handler)
    {
        m_fileHandler = std::move(handler);
    }

    // 0-based index of the first line visible at the top of the viewport.
    // firstVisibleBlock() is protected, but QPlainTextEdit's vertical
    // scrollbar is measured in BLOCKS, not pixels: its value IS the index of
    // the top block (verified - setValue(50) puts block 50 at the top with a
    // maximum equal to the last block index). Dividing by the line height, as
    // if it were a pixel offset, under-reports badly.
    int topVisibleLine() const
    {
        return verticalScrollBar() ? verticalScrollBar()->value() : 0;
    }

    // A single place for the gutter's geometry. It must line up with the
    // VIEWPORT, not the editor frame: the editor's stylesheet gives it 8px of
    // padding, so the text area starts at viewport().y() and the document's
    // y=0 is there - not at the frame's y=0. The paint loop draws in document
    // coordinates, so a gutter pinned to the frame would sit those 8px high and
    // every number would ride above its line.
    void positionLineNumberArea()
    {
        m_lineArea->setGeometry(QRect(0, viewport()->y(),
                                      lineNumberAreaWidth(),
                                      viewport()->height()));
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QPlainTextEdit::resizeEvent(event);
        if (m_lineNumbersVisible)
            positionLineNumberArea();
    }

    // The scroll-sync hook (QPlainTextEdit::updateRequest signal). While
    // scrolling (dy != 0) the gutter content just slides with the text -
    // numbers stay glued to their lines without any re-layout. Anything else
    // (a cursor blink, a reflow) repaints only the changed band, and only
    // while the gutter is actually shown.
    void updateLineNumberArea(const QRect& rect, int dy)
    {
        if (!m_lineNumbersVisible)
            return;
        if (dy != 0)
            m_lineArea->scroll(0, dy);
        else
            m_lineArea->update(0, rect.y(), m_lineArea->width(),
                               rect.height());
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter) {
            // Auto-continue ordered list: "N. text" Enter → "N+1. "
            QTextCursor c = textCursor();
            const QString line = c.block().text();
            static const QRegularExpression re(
                QStringLiteral("^(\\d{1,9})\\.\\s"));
            const auto m = re.match(line);
            if (m.hasMatch()) {
                const int next = m.captured(1).toInt() + 1;
                c.beginEditBlock();
                c.movePosition(QTextCursor::EndOfBlock);
                c.insertText(QStringLiteral("\n%1. ").arg(next));
                c.endEditBlock();
                setTextCursor(c);
                event->accept();
                return;
            }
        }
        QPlainTextEdit::keyPressEvent(event);
    }

    // The default admits TEXT pastes only; allow images and picture files too,
    // so the context menu's 粘贴 stays enabled for such a clipboard.
    bool canInsertFromMimeData(const QMimeData* source) const override
    {
        if (source && (source->hasImage()
                       || !localImageFileUrls(source).isEmpty()))
            return true;
        return QPlainTextEdit::canInsertFromMimeData(source);
    }

    // Turn a pasted/dropped picture into its ![..](..) reference: real pixels
    // (screenshot Ctrl+V) are handed to the image handler; copied image FILEs
    // (text/uri-list) are grouped and handed to the file handler. Otherwise the
    // default behaviour (text etc.) applies. QPlainTextEdit routes drops here
    // too, so drag-and-drop of an image file is covered as well.
    void insertFromMimeData(const QMimeData* source) override
    {
        if (source && m_imageHandler && source->hasImage()) {
            QImage img;
            const QVariant data = source->imageData();
            if (data.canConvert<QImage>())
                img = data.value<QImage>();
            else if (data.canConvert<QPixmap>())
                img = data.value<QPixmap>().toImage();
            if (!img.isNull()) {
                const QString md = m_imageHandler(img);
                if (!md.isEmpty()) {
                    insertPlainText(md);
                    return;
                }
            }
        }
        if (source && m_fileHandler) {
            const QStringList paths = localImageFileUrls(source);
            if (!paths.isEmpty()) {
                const QString md = m_fileHandler(paths);
                if (!md.isEmpty()) {
                    insertPlainText(md);
                    return;
                }
            }
        }
        QPlainTextEdit::insertFromMimeData(source);
    }

private:
    // Local (file:) urls in the mime data that point at existing picture files.
    static QStringList localImageFileUrls(const QMimeData* source)
    {
        QStringList out;
        for (const QUrl& u : source->urls()) {
            if (!u.isLocalFile())
                continue;
            const QString p = u.toLocalFile();
            if (!isPictureFile(p))
                continue;
            out << p;
        }
        return out;
    }

    // A conservative set of image suffixes (anything QImage/ico easily loads).
    static bool isPictureFile(const QString& p)
    {
        const QFileInfo f(p);
        if (!f.exists() || !f.isFile())
            return false;
        const QString s = f.suffix().toLower();
        static const char* const kImg[] = {"png", "jpg", "jpeg", "bmp", "gif",
                                           "webp", "tif", "tiff", "ico",
                                           "avif", "svg"};
        for (const char* e : kImg)
            if (s == QLatin1String(e))
                return true;
        return false;
    }

    // Reserve (or release) the strip the gutter occupies so text never runs
    // under the numbers. Zero when hidden - no leftover blank strip.
    void updateLineNumberAreaWidth()
    {
        setViewportMargins(m_lineNumbersVisible ? lineNumberAreaWidth() : 0,
                           0, 0, 0);
    }

    std::function<QString(const QImage&)> m_imageHandler;
    std::function<QString(const QStringList&)> m_fileHandler;
    LineNumberArea* m_lineArea = nullptr;
    bool m_lineNumbersVisible = false;
};

LineNumberArea::LineNumberArea(MdSourceEditor* editor)
    : QWidget(editor)
    , m_editor(editor)
{
    setObjectName(QStringLiteral("genesisLineNumberArea"));
    setCursor(Qt::ArrowCursor);
}

QSize LineNumberArea::sizeHint() const
{
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event)
{
    m_editor->lineNumberAreaPaintEvent(event);
}

} // namespace

QPlainTextEdit* createMarkdownSourceEditor(
    QWidget* parent,
    std::function<QString(const QImage&)> imagePasteHandler,
    std::function<QString(const QStringList&)> imageFilePasteHandler)
{
    auto* editor = new MdSourceEditor(parent);
    if (imagePasteHandler)
        editor->setImagePasteHandler(std::move(imagePasteHandler));
    if (imageFilePasteHandler)
        editor->setImageFilePasteHandler(std::move(imageFilePasteHandler));
    // Marker so setMarkdownEditorLineNumbers() can recognise an editor made
    // here without the internal MdSourceEditor type (and its Q_OBJECT-less
    // anonymous-namespace status) ever reaching a public header.
    editor->setProperty("genesisMdSource", true);
    // Top padding is tightened (8px sides, 2px top): the 8px top simply pushed
    // the first line down as dead space above it. The bottom keeps its 8px so
    // the caret on the last line is not flush with the edge.
    editor->setStyleSheet(
        "QPlainTextEdit { background: #FFFFFF; color: #333333;"
        "  border: none; font-size: 14px; padding: 2px 8px 8px 8px;"
        "  font-family: Consolas, 'Courier New', monospace; }");
    return editor;
}

void setMarkdownEditorLineNumbers(QPlainTextEdit* editor, bool visible)
{
    if (editor && editor->property("genesisMdSource").toBool())
        static_cast<MdSourceEditor*>(editor)->setLineNumbersVisible(visible);
}

int markdownEditorTopLine(QPlainTextEdit* editor)
{
    if (editor && editor->property("genesisMdSource").toBool())
        return static_cast<MdSourceEditor*>(editor)->topVisibleLine();
    return 0;
}

bool scrollMarkdownPreviewToSourceLine(QTextBrowser* preview, int sourceLine)
{
    if (!preview || sourceLine < 0)
        return false;
    QTextDocument* doc = preview->document();
    if (!doc)
        return false;
    // The rendered document does not correspond line-for-line with the source
    // (paragraphs reflow, fences become overlays, HTML blocks are extracted),
    // so a raw index is meaningless. Anchor on TEXT instead: take the source
    // line's own words and find the first rendered block that starts with
    // them. Walking forward from the source line covers blank/short lines
    // that produce no block of their own.
    const QStringList lines = preview->property("genesisSourceLines")
                                  .toStringList();
    if (sourceLine >= lines.size())
        return false;

    auto normalize = [](const QString& s) {
        QString t = s.simplified();
        // Strip the leading Markdown syntax that never survives rendering.
        static const QRegularExpression lead(
            QStringLiteral("^[\\s>#*+\\-`~\\d.)\\[\\]!|]*"));
        t.remove(lead);
        return t.trimmed();
    };

    // blockBoundingRect() must describe the FINAL layout or the scroll lands
    // a block short (the same staleness the render function guards against).
    // Settle the layout before reading any geometry.
    QAbstractTextDocumentLayout* lay = doc->documentLayout();
    if (!lay)
        return false;
    lay->documentSize();
    doc->markContentsDirty(0, doc->characterCount());
    lay->documentSize();

    for (int i = sourceLine; i < lines.size(); ++i) {
        QString key = normalize(lines.at(i));
        if (key.size() < 2)
            continue;
        // A fenced-code marker or pure punctuation carries no anchor.
        if (key.startsWith(QLatin1String("```"))
            || key.startsWith(QLatin1String("~~~")))
            continue;
        // Search the rendered blocks for one that begins with this text.
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            const QString bt = b.text().trimmed();
            if (bt.isEmpty())
                continue;
            if (bt.startsWith(key.left(40))
                || (key.size() >= 8 && bt.contains(key.left(24)))) {
                // Scroll so this block is the top-most line. Setting the
                // scrollbar to the block's layout top is off by a pixel or so
                // (Qt's scroll area and the document layout round differently),
                // which leaves the block ABOVE - whose bottom coincides with
                // this top - as the top-most line. Nudge forward until this
                // block really is the one at the top of the viewport.
                const QRectF r = lay->blockBoundingRect(b);
                QScrollBar* sb = preview->verticalScrollBar();
                sb->setValue(int(r.top()));
                const int want = b.blockNumber();
                for (int tries = 0; tries < 3; ++tries) {
                    if (preview->cursorForPosition(QPoint(2, 1)).blockNumber()
                        >= want)
                        break;
                    sb->setValue(sb->value() + 1);
                }
                return true;
            }
        }
    }
    return false;
}

QTextBrowser* createMarkdownPreview(QWidget* parent)
{
    auto* preview = new MdPreviewBrowser(parent);
    // Local images referenced by the note resolve through this document's
    // loadResource() override (the default one leaves them blank).
    auto* doc = new MdPreviewDocument(preview);
    QPointer<MdPreviewDocument> docGuard = doc;
    // When a remote picture finishes downloading it lives in the shared cache
    // but the block's image was laid out empty; force a re-layout + repaint so
    // loadResource() runs again and the fresh bytes are drawn (see the async
    // remote fetch). scheduleRefresh is invoked from the finished handler on
    // the GUI thread.
    doc->scheduleRefresh = [docGuard, preview]() {
        if (!docGuard)
            return;   // preview (and thus the owned doc) is gone
        // A remote picture is now cached and sizeable: fit it (like every
        // other image) to half the pane width, which also re-lays out and
        // repaints so the freshly-downloaded bytes are drawn.
        if (preview) {
            if (auto* mb = static_cast<MdPreviewBrowser*>(preview)) {
                mb->fitImagesToHalfWidth();
                mb->refreshCodeGeom();
            }
        } else {
            docGuard->markContentsDirty(0, qMax(0, docGuard->characterCount()));
            if (auto* lay = docGuard->documentLayout())
                lay->documentSize();
        }
    };
    preview->setDocument(doc);
    // Left margin wide enough for the fenced-code frames' chrome. Each
    // MdCodeFrame overlay is placed at (code-text x - kChromeH) so its 1px
    // border + 6px padding sit left of the code and the code text lines up with
    // the surrounding paragraphs. With Qt's default 4px document margin that
    // put the frame at x = -3 and the viewport clipped its left border and
    // rounded corner. kChromeH here makes the code text start exactly far
    // enough in for the frame to fit, and lines the frame's outer edge up with
    // the paragraph text block edge.
    doc->setDocumentMargin(kChromeH);
    preview->setOpenExternalLinks(true);
    // No scrollbar strips on the render page: wheel/keyboard still scroll
    // (same treatment as the notes editor and the Markdown guide window).
    preview->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setStyleSheet(
        "QTextBrowser { background: #FFFFFF; color: #1F2328;"
        "  border: none; padding: 8px; font-size: 16px; }");
    // Apply the stylesheet font before the first (possibly hidden) render:
    // an unpolished browser lays out with the default font and the document
    // never resyncs after show().
    preview->ensurePolished();
    return preview;
}

void renderMarkdownPreview(QTextBrowser* preview, const QString& markdown)
{
    if (!preview)
        return;

    // Preserve scroll relative to top so the preview doesn't jump.
    QScrollBar* bar = preview->verticalScrollBar();
    const int saved = bar ? bar->value() : 0;

    // Keep the ORIGINAL source lines so scrollMarkdownPreviewToSourceLine()
    // can map a source line to its rendered block by text (the rendered
    // document is not line-for-line with the source).
    preview->setProperty("genesisSourceLines",
                         markdown.split(QChar('\n')));

    QString src = markdown;
    {
        // A standalone "<br>" line at the start of a paragraph makes md4c
        // enter "HTML block" mode (it treats <br> as an opening block tag)
        // and swallow ALL following content until a "</br>" it never finds.
        // Replace such a line with a Markdown hard-break line ("  "), which
        // renders the same line break without triggering the block state.
        QStringList lines = src.split(QChar('\n'));
        static const QRegularExpression brLine(
            QStringLiteral("^\\s*<br\\s*/?\\s*>\\s*$"));
        for (QString& ln : lines)
            if (brLine.match(ln).hasMatch())
                ln = QStringLiteral("  ");

        // Pad EMPTY list items ("1. " / "- " with nothing after) with a
        // non-breaking space: md4c treats an empty item's marker line as a
        // lazy continuation and swallows the PREVIOUS line into the list —
        // a heading right above would render as "1. 一级标题" on one line.
        static const QRegularExpression emptyItem(
            QStringLiteral("^([ \t]*)([0-9]{1,9}[.)]|[-*+])[ \t]*$"));
        for (QString& ln : lines) {
            const auto m = emptyItem.match(ln);
            if (m.hasMatch())
                ln = m.captured(1) + m.captured(2) + QChar(' ')
                     + QChar(0x00A0);
        }

        // Heading inside a list item ("1. # 标题"): md4c parses it, but
        // Qt's importer drops the heading block's list membership — the
        // item loses its number and the NEXT items renumber from 1.
        // Smuggle the heading level through as <sentinel>level<sentinel>
        // so the importer keeps a plain list item; the polish pass decodes
        // it back into a heading block that stays in the list. Lines inside
        // fenced code blocks are skipped — there the text is literal and a
        // sentinel would leak into the rendered code.
        static const QRegularExpression listHeadingItem(QStringLiteral(
            "^([ \t]*)([0-9]{1,9}[.)]|[-*+])([ \t]+)(#{1,6})[ \t]+(.*)$"));
        static const QRegularExpression fenceLine(
            QStringLiteral("^[ \t]*(```|~~~)"));
        bool inFence = false;
        for (QString& ln : lines) {
            if (fenceLine.match(ln).hasMatch()) {
                inFence = !inFence;
                continue;
            }
            if (inFence)
                continue;
            const auto m = listHeadingItem.match(ln);
            if (m.hasMatch())
                ln = m.captured(1) + m.captured(2) + m.captured(3)
                     + kHeadingSentinel
                     + QString::number(m.captured(4).size())
                     + kHeadingSentinel + m.captured(5);
            // Standalone or inline <img ...> without a trailing "/" would be
            // dropped by the importer (see forceImgSelfClosing) — self-close it
            // here, outside fenced code, so pasted HTML images render.
            forceImgSelfClosing(ln);
            // A stray C++-style `<vector>` / `<Node>` would make Qt's importer
            // swallow everything after it (see escapeStrayAngleTags); escape any
            // such unknown tag so notes keep rendering past it.
            escapeStrayAngleTags(ln);
        }

        // Image line-breaking. Qt sticks a lone <img>"paragraph" onto the text
        // paragraph right before it, even when a BLANK line separates them, so
        // the picture stays glued on the source text's last row. The only
        // reliable way to give a lone image its OWN visual row inside that
        // merged paragraph is a Markdown HARD line break ("  "+newline) placed
        // so the image starts line two. So for every image-only source line we
        // pull it up onto the nearest real text above (over any blank paragraph
        // lines) and hard-break onto it — covering both "two carriage returns"
        // and "two spaces + carriage return" before the image.
        const auto isFigureLine = [](const QString& raw) {
            const QString t = raw.trimmed();
            if (t.isEmpty())
                return false;
            static const QRegularExpression figureTok(QStringLiteral(
                "^(?:<img(?:\\s[^>]*)?/?>|!\\[[^\\]]*\\]\\([^)]*\\))$"));
            return figureTok.match(t).hasMatch();
        };
        QStringList fig;
        bool figFence = false;
        for (const QString& ln0 : std::as_const(lines)) {
            if (fenceLine.match(ln0).hasMatch()) {
                figFence = !figFence;
                fig << ln0;
                continue;
            }
            if (figFence) {
                fig << ln0;
                continue;
            }
            if (isFigureLine(ln0)) {
                // Step back over any blank lines to the nearest real text.
                int k = fig.size();
                bool skippedBlank = false;
                while (k > 0 && fig[k - 1].trimmed().isEmpty()) {
                    skippedBlank = true;
                    --k;
                }
                // Back up over any *prior* just-joined figure too, so multiple
                // consecutive figures each keep their own slot.
                while (k > 0 && isFigureLine(fig[k - 1]))
                    --k;
                if (k > 0) {
                    // Hard-break the nearest text (if the boundary already is a
                    // two-space line this stays true; else we add it) then drop
                    // the blank line(s) we skipped so the image really sits on
                    // the next row of that merged block.
                    if (!fig[k - 1].endsWith(QStringLiteral("  ")))
                        fig[k - 1] += QStringLiteral("  ");
                    fig.resize(k);                  // drop blank separator line(s)
                    fig << ln0;                  // image, own row next to that text
                    if (skippedBlank)
                        fig << QString();        // keep one gap before next writer block
                    continue;
                }
                fig << ln0;   // at document start with nothing above
                continue;
            }
            fig << ln0;
        }
        // Collapse any runs of blank lines left by the pass.
        lines.clear();
        for (const QString& ln : std::as_const(fig)) {
            if (ln.isEmpty() && !lines.isEmpty() && lines.last().isEmpty())
                continue;
            lines << ln;
        }
        src = lines.join(QChar('\n'));
    }

    // Split the source into a markdown part and standalone HTML blocks.
    // Qt's setMarkdown handles inline HTML tags natively (span/cite/del/a/
    // img anywhere inside paragraphs, lists, headings), but BLOCK-level
    // elements (div/table/pre/p/section/...) must be inserted via
    // insertHtml — setMarkdown would otherwise merge them into neighbour
    // paragraphs. CommonMark semantics: a block element starts on its own
    // line, spans until its matching close tag, and Markdown inside it is
    // NOT processed (kept literal).
    QString md = src;
    QString htmlBlocks;
    {
        // Block-level tag names per CommonMark. "script/style/pre" handled
        // via their content; everything else via <tag>...</tag> scanning.
        static const QStringList kBlockTags = {
            QStringLiteral("address"),  QStringLiteral("article"),
            QStringLiteral("aside"),    QStringLiteral("base"),
            QStringLiteral("basefont"), QStringLiteral("blockquote"),
            QStringLiteral("body"),     QStringLiteral("caption"),
            QStringLiteral("center"),   QStringLiteral("col"),
            QStringLiteral("colgroup"), QStringLiteral("dd"),
            QStringLiteral("details"),  QStringLiteral("dialog"),
            QStringLiteral("dir"),      QStringLiteral("div"),
            QStringLiteral("dl"),       QStringLiteral("dt"),
            QStringLiteral("fieldset"), QStringLiteral("figcaption"),
            QStringLiteral("figure"),   QStringLiteral("footer"),
            QStringLiteral("form"),     QStringLiteral("frame"),
            QStringLiteral("frameset"), QStringLiteral("h1"),
            QStringLiteral("h2"),       QStringLiteral("h3"),
            QStringLiteral("h4"),       QStringLiteral("h5"),
            QStringLiteral("h6"),       QStringLiteral("head"),
            QStringLiteral("header"),   QStringLiteral("hr"),
            QStringLiteral("html"),     QStringLiteral("iframe"),
            QStringLiteral("legend"),   QStringLiteral("li"),
            QStringLiteral("link"),     QStringLiteral("main"),
            QStringLiteral("menu"),     QStringLiteral("menuitem"),
            QStringLiteral("nav"),      QStringLiteral("noframes"),
            QStringLiteral("ol"),       QStringLiteral("optgroup"),
            QStringLiteral("option"),   QStringLiteral("p"),
            QStringLiteral("param"),    QStringLiteral("search"),
            QStringLiteral("section"),  QStringLiteral("summary"),
            QStringLiteral("table"),    QStringLiteral("tbody"),
            QStringLiteral("td"),       QStringLiteral("tfoot"),
            QStringLiteral("th"),       QStringLiteral("thead"),
            QStringLiteral("title"),    QStringLiteral("tr"),
            QStringLiteral("track"),    QStringLiteral("ul"),
        };

        QStringList lines = src.split(QChar('\n'));
        QStringList out;
        QStringList block;      // current HTML block lines
        bool inBlock = false;
        QString blockTag;

        const auto pushBlock = [&]() {
            if (block.isEmpty())
                return;
            // Keep the raw HTML verbatim so the block renders standalone
            // and inner Markdown stays literal.
            if (!htmlBlocks.isEmpty())
                htmlBlocks += QChar('\n');
            htmlBlocks += block.join(QChar('\n'));
            block.clear();
            inBlock = false;
        };

        for (int i = 0; i < lines.size(); ++i) {
            const QString ln = lines[i];
            if (!inBlock) {
                // Detect an opening block tag at the start of a line
                // (CommonMark: block element begins its own line).
                const auto m = QRegularExpression(
                    QStringLiteral("^\\s*<(/?)(%1)(\\s[^>]*)?>\\s*$")
                        .arg(kBlockTags.join(QLatin1Char('|'))))
                        .match(ln);
                if (m.hasMatch()) {
                    const bool isClose = !m.captured(1).isEmpty();
                    const QString tag = m.captured(2).toLower();
                    // Only START a block on an OPENING tag that isn't
                    // immediately closed on the same line.
                    if (!isClose
                        && !ln.contains(QLatin1String("</"))) {
                        inBlock = true;
                        blockTag = tag;
                        block << ln;
                        continue;
                    }
                    // self-closing / single-line block: emit directly
                    if (!isClose && ln.contains(QLatin1String("</"))) {
                        htmlBlocks += (htmlBlocks.isEmpty()
                                           ? QString() : QStringLiteral("\n"))
                                      + ln;
                        continue;
                    }
                    // stray close tag or hr: treat as its own block line
                    htmlBlocks += (htmlBlocks.isEmpty()
                                       ? QString() : QStringLiteral("\n"))
                                  + ln;
                    continue;
                }
                out << ln;   // normal markdown line
            } else {
                // Inside a block: look for the matching close tag.
                const auto m = QRegularExpression(
                    QStringLiteral("^\\s*</%1\\s*>\\s*$")
                        .arg(blockTag))
                        .match(ln);
                if (m.hasMatch()) {
                    block << ln;
                    pushBlock();
                    continue;
                }
                block << ln;
            }
        }
        pushBlock();   // flush trailing block

        md = out.join(QChar('\n'));
    }

    // Build the preview document: markdown part via setMarkdown (handles
    // inline HTML natively), then append the block-level HTML via
    // insertHtml so it renders standalone with inner Markdown untouched.

    // Qt lays out fenced code WITHOUT horizontal wrap, so an over-long code line
    // would widen the whole page. Reflow such lines at the pane width by breaking
    // each too-long code source line into several shorter ones (a newline inside
    // one fence stays inside the same code block). Char widths are measured with
    // the same monospace at 16px the preview uses. Reflow ONLY when a credible
    // pane width is available — during first construction the widget may still be
    // un-sized (width 0), which must not trigger a tiny over-split.
    {
        // Widget width can be 0 when the pane has not been laid out yet (first
        // build). If we cannot get a real width, use a generous fixed content
        // width instead of falling back to a few pixels — otherwise long lines
        // would be shredded into ~5-char fragments.
        int contentW = preview->width();
        if (preview->viewport() && preview->viewport()->width() > contentW)
            contentW = preview->viewport()->width();
        if (contentW < 90)
            contentW = 640;   // safe default until the real width is known
        const int boxPx = qMax(220, contentW - 56);
        QFont f(QStringLiteral("Consolas")); f.setPixelSize(kCodePixelSize);
        QFontMetricsF fm(f);
        // Code line frames do not wrap (see the CodeFrame overlay added to
        // MdPreviewBrowser): long lines slide sideways inside the box, so do
        // NOT pre-split long code lines here.
        const qreal budgetPx = 1e7;   // effectively disable this reflow
            const auto wrapAt = [&](QString run) {
                QString wrapped;
                while (!run.isEmpty()) {
                    qreal w = 0; int take = 0;
                    for (; take < run.size(); ++take) {
                        const qreal cw = fm.horizontalAdvance(run.at(take));
                        if (take > 0 && w + cw > budgetPx) break;
                        w += cw;
                    }
                    if (take <= 0) take = 1;
                    if (!wrapped.isEmpty()) wrapped += QLatin1Char('\n');
                    wrapped += run.left(take);
                    run = run.mid(take);
                }
                return wrapped;
            };
            QStringList ml = md.split(QLatin1Char('\n'));
            bool inCode = false;
            for (QString& line : ml) {
                static const QRegularExpression fenceOpen(
                    QStringLiteral("^[ \\t]*(```|~~~)"));
                // A code fence line also ends a run only for real inner markers;
                // lines that only contain "```" open/close toggle.
                const QString t = line.trimmed();
                if (t.size() >= 3 && t.startsWith(QLatin1String("```"))
                    || t.startsWith(QLatin1String("~~~"))) {
                    inCode = !inCode;
                    continue;
                }
                if (!inCode) continue;
                qreal w = 0; for (const QChar& c : line) w += fm.horizontalAdvance(c);
                if (w > budgetPx + 4) line = wrapAt(line);
            }
            md = ml.join(QLatin1Char('\n'));
    }

    QTextDocument* doc = preview->document();
    doc->setMarkdown(md, QTextDocument::MarkdownDialectGitHub);
    // setMarkdown resets the document's indent width to its default, so
    // re-apply the tighter list indentation here (after every re-parse).
    doc->setIndentWidth(kListIndentWidth);
    if (!htmlBlocks.isEmpty()) {
        QTextCursor c(doc);
        c.movePosition(QTextCursor::End);
        // A separator so the HTML block doesn't merge with the last
        // markdown paragraph.
        c.insertText(QStringLiteral("\n"));
        c.insertHtml(htmlBlocks);
    }
    // setMarkdown produces plain QTextFormat properties; apply the
    // GitHub markdown-body styling on top.
    polishPreviewDocument(doc);
    // Renderer of setMarkdown is done; then uniformly size every embedded
    // picture to half the pane's current content width (aspect preserved),
    // so notes look consistent and re-flow when the window is rezised.
    const int avail = preview->viewport() ? preview->viewport()->width()
                                          : preview->width();
    if (avail > 10)
        fitPreviewImagesToWidth(doc, avail / 2);
    // Long lines (console output, XML <depend>… etc.) must never escape the box
    // sideways: enable wrap-anywhere on the BROWSER so the widest line breaks
    // inside its code block and the pane never needs to scroll the page
    // horizontally.
    preview->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    {   // Also reflect it on the document itself (and a fresh default).
        QTextOption to = doc->defaultTextOption();
        to.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        doc->setDefaultTextOption(to);
    }
    // Force the layout to reflow after the programmatic format changes —
    // otherwise blockBoundingRect() (used by MdPreviewBrowser::paintEvent
    // to draw the h1 underline) can report stale geometry.
    doc->documentLayout()->documentSize();
    doc->markContentsDirty(0, doc->characterCount());
    // Fenced code is shown as its own rounded, no-wrap frame
    // (see MdPreviewBrowser::syncCodeFrames); rebuild overlays from the fresh
    // doc, then restore the saved scroll position which re-positions them.
    if (auto* mb = static_cast<MdPreviewBrowser*>(preview)) {
        mb->syncCodeFrames();
        mb->relayoutFrames();
    }
    if (bar)
        bar->setValue(qMin(saved, bar->maximum()));
}

} // namespace Genesis
