#ifndef DOUBLEPAGEVIEW_H
#define DOUBLEPAGEVIEW_H

#include <QAbstractScrollArea>
#include <QHash>
#include <QPixmap>

class QPdfDocument;
class QTimer;

namespace Genesis {

// Two-page spread view for the reader (QPdfView only offers single-column
// modes). Renders pages side by side — row k holds pages 2k and 2k+1 —
// with lazy per-page rendering of just the visible rows and a small pixmap
// cache. Supports the same zoom / fit modes as the single-page view.
class DoublePageView : public QAbstractScrollArea {
    Q_OBJECT

public:
    enum class FitMode { Custom, FitWidth, FitPage };

    explicit DoublePageView(QWidget* parent = nullptr);

    void setDocument(QPdfDocument* document);

    // Pages laid out side by side per row: 2 = the reading spread, 1 = the
    // single-page view. Everything else (zoom, fit, scrolling, highlighting)
    // is identical, so both reader modes share this one class.
    int pagesPerRow() const { return m_pagesPerRow; }
    void setPagesPerRow(int n);

    qreal zoomFactor() const { return m_zoom; }
    void setZoomFactor(qreal zoom);

    FitMode fitMode() const { return m_fitMode; }
    void setFitMode(FitMode mode);

    // First page of the top-most visible row (0-based).
    int currentPage() const { return m_currentPage; }
    void jumpToPage(int page);

    // Boxes to tint on top of the rendered pages, in PDF point coordinates
    // (origin top-left of the page, y down - the same space
    // QPdfSelection::bounds() reports). Set to an empty list to clear.
    void setHighlights(int page, const QList<QRectF>& boxesPdfPoints);

    // The page's rect in CONTENT space (no scroll offset). One definition
    // shared by the paint pass and hit-testing so they cannot drift.
    QRectF pageRectContent(int page) const;
    // Page under a viewport point, and the page's on-screen rect. Returns -1
    // when the point is not over any page.
    int pageAt(const QPoint& viewportPos, QRectF* pageRect = nullptr) const;
    // Viewport point -> PDF point on `page` (inverse of the mapping the paint
    // pass uses). False when the page has no rect yet.
    bool toPagePoint(int page, const QPoint& viewportPos, QPointF* out) const;
    // Index into getAllText(page).text() of the character nearest a PDF point.
    // Used to turn a drag into a text range, since getSelection() only handles
    // two points on one line while getSelectionAtIndex() spans lines.
    int charIndexAt(int page, const QPointF& pdfPoint) const;
    // Tight box per character for one page, built once and cached. The bounds
    // that getAllText() returns are merged glyph runs, not one box per
    // character, so they cannot be indexed positionally.
    const QList<QRectF>& charBoxes(int page) const;

    // Drag-select text on the pages. While dragging, the covered text is
    // highlighted; on release, selectionFinished() carries it out.
    void setSelectionEnabled(bool on);
    bool selectionEnabled() const { return m_selectEnabled; }
    void clearSelection();

signals:
    void currentPageChanged(int page);
    // Emitted on mouse-up after a drag selected something. `text` is the
    // selection; `boxesPdfPoints` are its rectangles in PDF point coordinates
    // for the page in `page`.
    void selectionFinished(int page, const QString& text,
                           const QList<QRectF>& boxesPdfPoints);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // Recompute the row layout (sizes/offsets) for the current zoom.
    void relayout();
    // Apply the fit mode: derive m_zoom from the viewport size.
    void applyFit();
    // Render (or fetch cached) pixmap of one page at the current zoom.
    QPixmap pagePixmap(int page, const QSize& size);
    void updateCurrentPage();
    // Off-screen rows near the viewport, rendered during idle time so
    // scrolling onto them never blocks the paint.
    void schedulePrerender();
    void prerenderStep();

    QPdfDocument* m_document;
    qreal m_zoom;
    FitMode m_fitMode;
    int m_currentPage;
    int m_pagesPerRow;   // 2 = spread, 1 = single page

    // Layout, all in px at the current zoom: per-row y offset/height and
    // per-page size.
    QList<qreal> m_rowY;
    QList<qreal> m_rowHeight;
    QList<QSizeF> m_pageSize;
    qreal m_contentWidth;
    qreal m_contentHeight;

    QHash<int, QPixmap> m_cache;   // page -> rendered pixmap at m_zoom
    QTimer* m_prerender;           // idle prerender of neighbouring rows
    // Highlights, keyed by page, each box in PDF point coordinates.
    QHash<int, QList<QRectF>> m_highlights;

    // Drag-selection state. m_selPage is the page the drag started on (-1 =
    // none); both endpoints are PDF points so a scroll mid-drag cannot skew
    // them.
    bool m_selectEnabled = false;
    bool m_dragging = false;
    int m_selPage = -1;
    QPointF m_selStart;
    QPointF m_selEnd;
    QString m_selText;
    QList<QRectF> m_selBoxes;
    // Per-character boxes, keyed by page. Built lazily on the first selection
    // on that page; see charBoxes().
    mutable QHash<int, QList<QRectF>> m_charBoxes;
};

} // namespace Genesis

#endif // DOUBLEPAGEVIEW_H
