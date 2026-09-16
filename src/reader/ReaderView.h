#ifndef READERVIEW_H
#define READERVIEW_H

#include <QWidget>
#include <QDateTime>

class QLabel;
class QLineEdit;
class QPdfDocument;
class QPdfView;
class QStackedWidget;
class QTextBrowser;
class QTreeView;
class QPropertyAnimation;
class QResizeEvent;
class QTimer;

namespace Genesis {

class DoublePageView;
class IconButton;
class NotesPanel;

// Reading view shown when a book is opened from the shelf: the PDF fills the
// left 60%; the right 40% is the note-taking panel.
//
// A control bar tops the PDF area — hamburger (TOC drawer) and book title on
// the left; prev-page / page box "current/total" / next-page dead-centered;
// zoom -/+ and the fit-width/page toggle on the right, then the hide chevron
// and kebab (double-page view; add bookmark). The bar collapses via the
// chevron to give the page the full height; a small floating chevron brings
// it back. The PDF area is transparent so the app background shows through.
class ReaderView : public QWidget {
    Q_OBJECT

public:
    explicit ReaderView(QWidget* parent = nullptr);

    // Load and display the given PDF (absolute path).
    void openBook(const QString& path);

    // openBook + jump to a specific 0-based page (bookmark navigation).
    void openBookAt(const QString& path, int page);

    // Close the current document and release its file handle (so the file
    // can be deleted from the shelf on Windows).
    void closeBook();

    // Return the current file path (for metadata/tracking).
    QString currentBookPath() const { return m_bookPath; }

    // Total reading seconds for the current book (in-memory accumulator).
    int readSeconds() const { return m_readSeconds + m_sessionSeconds; }

    // Drain the reading time accumulated since the last call, and (optionally)
    // report when that stretch began. This is the READING LOG's watermark and
    // is deliberately independent of readSeconds(), which is a cumulative
    // total: the log needs deltas or every save would re-record the session.
    // Returns 0 when nothing new has accumulated.
    int takeUnloggedSeconds(QDateTime* startOut = nullptr);

    // Append the pending stretch to ReadingLogStore. No-op when no book is open
    // or nothing has accumulated since the last call, so it is safe to call
    // from several places (leaving the reader, the periodic tick, app exit).
    void logPendingReading();

signals:
    void backRequested();   // user clicked the back-to-shelf button

protected:
    // Keeps the floating center cluster of the control bar dead-centered.
    bool eventFilter(QObject* watched, QEvent* event) override;
    // Keeps the Markdown preview matched to the responsive PDF/notes split.
    void resizeEvent(QResizeEvent* event) override;

private:
    QWidget* buildControlBar();
    void setBarVisible(bool visible);
    void toggleToc();
    void showKebabMenu();
    void addBookmark();                 // name + save the current page
    // Copy the current page's text layer to the clipboard and tint where that
    // text sits on the page (both view modes).
    void extractPageText();
    // Store + show the tint boxes (PDF point coordinates) for one page. Both
    // views are DoublePageView, so both draw them the same way.
    void showTextHighlights(int page, const QList<QRectF>& boxesPdfPoints);
    void clearTextHighlights();
    // The user finished a drag-selection on a page: ask whether to copy it.
    void onPageSelectionFinished(int page, const QString& text,
                                 const QList<QRectF>& boxesPdfPoints);
    // Leave the text-extraction session: clear the tint and stop intercepting
    // drags, so normal reading gestures work again.
    void endTextExtraction();
    void setDoublePage(bool on);
    int currentPage() const;            // 0-based, of the active view
    void jumpToPage(int page);          // 0-based
    void syncPageBox(int page);         // reflect current page in the box
    void zoomBy(qreal factor);
    void toggleFitMode();
    void applyFitMode();
    void showMarkdownPreview();
    void hideMarkdownPreview();
    // Re-render the open Markdown preview from the latest note text (debounced
    // so typing a long note does not re-render the whole document per key).
    void refreshMarkdownPreview();

    // --- control bar ---
    QWidget* m_bar;
    IconButton* m_tocButton;
    QLabel* m_title;
    QLineEdit* m_pageBox;
    QLabel* m_pageTotal;
    IconButton* m_fitButton;
    IconButton* m_kebabButton;
    QWidget* m_revealStrip;      // floating chevron shown when the bar is hidden
    QWidget* m_centerBox;        // prev/page-box/next cluster, floated centered

    // --- PDF area ---
    QWidget* m_pdfColumn;        // left 60% column, also previews' geometry
    QStackedWidget* m_viewStack; // page 0: single page, page 1: two-page spread
    QPdfDocument* m_document;
    // Both views are DoublePageView - the same class laid out with one page
    // per row (single) or two (spread). Using our own view for both keeps the
    // page<->viewport transform under our control (see m_pagesPerRow), which
    // QPdfView did not expose.
    DoublePageView* m_singleView;
    DoublePageView* m_doubleView;
    QTreeView* m_tocView;        // TOC drawer over the PDF's left edge
    QPropertyAnimation* m_tocAnim;
    NotesPanel* m_notes;         // right column: Markdown source editor
    QWidget* m_markdownPreviewPanel; // slides in over the left 60% PDF area
    QTextBrowser* m_markdownPreview;
    QPropertyAnimation* m_markdownPreviewAnim;

    bool m_fitToWidth;           // false = fit page (default), true = fit width
    bool m_doublePage;
    // Reading-log watermark: the moment up to which reading time has already
    // been written to ReadingLogStore. takeUnloggedSeconds() advances it.
    QDateTime m_loggedTick;
    // Text-extraction tint: page number (-1 = none) and its boxes in PDF
    // point coordinates, kept so the double-page view can redraw them.
    int m_highlightPage;
    QList<QRectF> m_highlightBoxes;
    bool m_tocOpen;
    QTimer* m_readTimer;         // heartbeats every 5s while the book is open
    QTimer* m_previewDebounce;   // defers live Markdown preview to typing idle
    QString m_pendingMarkdown;   // latest note text, rendered when idle fires
    QString m_bookPath;          // currently loaded PDF path
    int m_readSeconds;           // persisted read seconds (from MetadataStore)
    int m_sessionSeconds;        // seconds accumulated this session
    QDateTime m_lastTick;        // wall clock snapshot used to bill time
};

} // namespace Genesis

#endif // READERVIEW_H
