#include "DoublePageView.h"

#include <QPdfDocument>
#include <QPdfSelection>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QTimer>

namespace Genesis {

namespace {
const int kPageGap = 12;      // px between the two pages of a spread and rows
const int kMargin  = 16;      // outer margin around the content
const int kMaxCachedPages = 48;
const int kPrerenderRows = 2; // rows to pre-render above/below the viewport
} // namespace

DoublePageView::DoublePageView(QWidget* parent)
    : QAbstractScrollArea(parent)
    , m_document(nullptr)
    , m_zoom(1.0)
    , m_fitMode(FitMode::FitWidth)
    , m_currentPage(0)
    , m_pagesPerRow(2)
    , m_contentWidth(0)
    , m_contentHeight(0)
    , m_prerender(new QTimer(this))
{
    // Transparent viewport: the app background shows through around pages.
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, false);
    viewport()->setAutoFillBackground(false);
    setStyleSheet("QAbstractScrollArea { background: transparent; }");
    setFrameShape(QFrame::NoFrame);
    verticalScrollBar()->setSingleStep(40);
    horizontalScrollBar()->setSingleStep(40);

    // Idle prerender: one page per tick, so it never blocks interaction.
    m_prerender->setInterval(0);
    m_prerender->setSingleShot(true);
    connect(m_prerender, &QTimer::timeout, this, &DoublePageView::prerenderStep);
}

void DoublePageView::setDocument(QPdfDocument* document)
{
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (m_document) {
        // Re-layout whenever a (new) document finishes loading.
        connect(m_document, &QPdfDocument::statusChanged, this,
                [this](QPdfDocument::Status status) {
                    if (status == QPdfDocument::Status::Ready) {
                        // Both caches describe the document that was loaded a
                        // moment ago. This view instance outlives any single
                        // book — opening another one reuses it — so they must
                        // be dropped here or the next book is served the
                        // previous book's page pixmaps and, worse, its
                        // per-character boxes, which would place every
                        // selection on the wrong glyphs.
                        m_cache.clear();
                        m_charBoxes.clear();
                        verticalScrollBar()->setValue(0);
                        applyFit();
                    }
                });
    }
    m_cache.clear();
    m_charBoxes.clear();
    applyFit();
}

void DoublePageView::setPagesPerRow(int n)
{
    n = qBound(1, n, 2);
    if (n == m_pagesPerRow)
        return;
    // Carry the reading position across the relayout, the same way the
    // reader's mode switch does.
    const int page = m_currentPage;
    m_pagesPerRow = n;
    m_cache.clear();
    applyFit();          // recomputes m_zoom for the new spread width
    jumpToPage(page);
}

void DoublePageView::setZoomFactor(qreal zoom)
{
    m_fitMode = FitMode::Custom;
    m_zoom = qBound(0.2, zoom, 5.0);
    m_cache.clear();
    relayout();
}

void DoublePageView::setFitMode(FitMode mode)
{
    m_fitMode = mode;
    applyFit();
}

void DoublePageView::applyFit()
{
    if (!m_document || m_document->pageCount() == 0) {
        relayout();
        return;
    }

    if (m_fitMode != FitMode::Custom) {
        // Base row size at zoom 1: the widest row and the tallest page in it.
        qreal maxRowW = 0, maxH = 0;
        const int n = m_document->pageCount();
        for (int i = 0; i < n; i += m_pagesPerRow) {
            qreal w = 0;
            qreal h = 0;
            for (int k = 0; k < m_pagesPerRow && i + k < n; ++k) {
                const QSizeF s = m_document->pagePointSize(i + k);
                w += s.width() + (k > 0 ? kPageGap : 0);
                h = qMax(h, s.height());
            }
            maxRowW = qMax(maxRowW, w);
            maxH = qMax(maxH, h);
        }
        const qreal availW = viewport()->width() - 2 * kMargin;
        const qreal availH = viewport()->height() - 2 * kMargin;
        if (maxRowW > 0 && availW > 0) {
            qreal z = availW / maxRowW;
            if (m_fitMode == FitMode::FitPage && maxH > 0 && availH > 0)
                z = availH / maxH;
            m_zoom = qBound(0.05, z, 5.0);
            m_cache.clear();
        }
    }
    relayout();
}

void DoublePageView::setHighlights(int page, const QList<QRectF>& boxesPdfPoints)
{
    if (boxesPdfPoints.isEmpty())
        m_highlights.remove(page);
    else
        m_highlights.insert(page, boxesPdfPoints);
    viewport()->update();
}

void DoublePageView::relayout()
{
    m_rowY.clear();
    m_rowHeight.clear();
    m_pageSize.clear();
    m_contentWidth = 0;
    m_contentHeight = 0;

    if (m_document && m_document->pageCount() > 0) {
        const int n = m_document->pageCount();
        m_pageSize.reserve(n);
        for (int i = 0; i < n; ++i)
            m_pageSize << m_document->pagePointSize(i) * m_zoom;

        qreal y = kMargin;
        for (int i = 0; i < n; i += m_pagesPerRow) {
            qreal rowW = 0;
            qreal rowH = 0;
            for (int k = 0; k < m_pagesPerRow && i + k < n; ++k) {
                rowW += m_pageSize[i + k].width() + (k > 0 ? kPageGap : 0);
                rowH = qMax(rowH, m_pageSize[i + k].height());
            }
            m_rowY << y;
            m_rowHeight << rowH;
            m_contentWidth = qMax(m_contentWidth, rowW);
            y += rowH + kPageGap;
        }
        m_contentHeight = y - kPageGap + kMargin;
    }

    // Scroll ranges: content minus viewport, floored at 0.
    verticalScrollBar()->setRange(
        0, qMax(0, int(m_contentHeight) - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setRange(
        0, qMax(0, int(m_contentWidth + 2 * kMargin) - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());

    updateCurrentPage();
    viewport()->update();
}

QPixmap DoublePageView::pagePixmap(int page, const QSize& size)
{
    // NOTE: compare logical (device-independent) sizes — QPixmap::size() is
    // in device pixels, which differs under a non-1 device pixel ratio.
    auto it = m_cache.constFind(page);
    if (it != m_cache.constEnd()
        && it->deviceIndependentSize().toSize() == size)
        return *it;

    // Render at device-pixel-ratio resolution so pages stay crisp on HiDPI.
    const qreal dpr = devicePixelRatioF();
    const QImage img = m_document->render(page, size * dpr);
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);

    if (m_cache.size() >= kMaxCachedPages) {
        // Drop the entry farthest from the requested page.
        int worst = page, dist = -1;
        for (auto k = m_cache.keyBegin(); k != m_cache.keyEnd(); ++k) {
            if (qAbs(*k - page) > dist) {
                dist = qAbs(*k - page);
                worst = *k;
            }
        }
        m_cache.remove(worst);
    }
    m_cache.insert(page, pm);
    return pm;
}

// The page's on-screen rect, WITHOUT the scroll offset (content space).
// paintEvent subtracts the scroll; hit-testing maps the other way. Keeping one
// definition means the two can never drift apart.
QRectF DoublePageView::pageRectContent(int page) const
{
    if (page < 0 || page >= m_pageSize.size() || m_rowY.isEmpty())
        return QRectF();
    const int row = page / m_pagesPerRow;
    if (row < 0 || row >= m_rowY.size())
        return QRectF();

    const int left = row * m_pagesPerRow;
    const int n = m_document ? m_document->pageCount() : 0;
    qreal rowW = 0;
    for (int k = 0; k < m_pagesPerRow && left + k < n; ++k)
        rowW += m_pageSize[left + k].width() + (k > 0 ? kPageGap : 0);
    qreal x = kMargin + (m_contentWidth - rowW) / 2.0
              + qMax(0.0, (viewport()->width() - m_contentWidth
                           - 2.0 * kMargin) / 2.0);
    for (int p = left; p < page; ++p)
        x += m_pageSize[p].width() + kPageGap;

    return QRectF(x, m_rowY[row], m_pageSize[page].width(),
                  m_pageSize[page].height());
}

int DoublePageView::pageAt(const QPoint& viewportPos, QRectF* pageRect) const
{
    const QPointF content(
        viewportPos.x() + horizontalScrollBar()->value(),
        viewportPos.y() + verticalScrollBar()->value());
    const int n = m_document ? m_document->pageCount() : 0;
    for (int page = 0; page < n; ++page) {
        const QRectF r = pageRectContent(page);
        if (r.contains(content)) {
            if (pageRect)
                *pageRect = r;
            return page;
        }
    }
    return -1;
}

bool DoublePageView::toPagePoint(int page, const QPoint& viewportPos,
                                 QPointF* out) const
{
    const QRectF r = pageRectContent(page);
    if (r.isEmpty() || m_zoom <= 0)
        return false;
    const QPointF content(
        viewportPos.x() + horizontalScrollBar()->value(),
        viewportPos.y() + verticalScrollBar()->value());
    // content = pageRect.topLeft() + pdfPoint * zoom  =>  invert.
    const QPointF pt((content.x() - r.left()) / m_zoom,
                     (content.y() - r.top()) / m_zoom);
    if (out)
        *out = pt;
    return true;
}

const QList<QRectF>& DoublePageView::charBoxes(int page) const
{
    auto it = m_charBoxes.constFind(page);
    if (it != m_charBoxes.constEnd())
        return *it;

    QList<QRectF> out;
    if (m_document && page >= 0 && page < m_document->pageCount()) {
        const QString text = m_document->getAllText(page).text();
        out.reserve(text.size());
        // One query per character. getAllText()'s own bounds() are merged
        // glyph runs (820 boxes for 1180 non-space characters on the sample),
        // so indexing them positionally desynchronises and every later pick
        // lands on the wrong glyph - the "selection is off to the lower right
        // and some text cannot be selected" symptom. getSelectionAtIndex(i,1)
        // returns that character's own tight box.
        for (int i = 0; i < text.size(); ++i) {
            if (text.at(i).isSpace()) {
                out.append(QRectF());   // keep indices aligned with `text`
                continue;
            }
            const QPdfSelection s = m_document->getSelectionAtIndex(page, i, 1);
            QRectF r;
            if (s.isValid())
                for (const QPolygonF& poly : s.bounds())
                    r = r.isNull() ? poly.boundingRect()
                                   : r.united(poly.boundingRect());
            out.append(r);
        }
    }
    return *m_charBoxes.insert(page, out);
}

int DoublePageView::charIndexAt(int page, const QPointF& pdfPoint) const
{
    if (!m_document || page < 0)
        return -1;
    const QList<QRectF> boxes = charBoxes(page);
    if (boxes.isEmpty())
        return -1;

    // Prefer the character whose own box contains the point.
    for (int i = 0; i < boxes.size(); ++i)
        if (!boxes.at(i).isNull() && boxes.at(i).contains(pdfPoint))
            return i;

    // Otherwise snap to the nearest character ON THE NEAREST LINE: a point in
    // the leading between two lines must not jump sideways to a glyph on the
    // line above or below. Weight the vertical distance far above the
    // horizontal one so the line choice always wins.
    int best = -1;
    qreal bestScore = 1e30;
    for (int i = 0; i < boxes.size(); ++i) {
        const QRectF r = boxes.at(i);
        if (r.isNull())
            continue;
        const qreal dx = qMax(0.0, qMax(r.left() - pdfPoint.x(),
                                        pdfPoint.x() - r.right()));
        const qreal dy = qMax(0.0, qMax(r.top() - pdfPoint.y(),
                                        pdfPoint.y() - r.bottom()));
        const qreal score = dy * 10000.0 + dx;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

void DoublePageView::setSelectionEnabled(bool on)
{
    m_selectEnabled = on;
    if (!on)
        clearSelection();
    viewport()->setCursor(on ? Qt::IBeamCursor : Qt::ArrowCursor);
}

void DoublePageView::clearSelection()
{
    m_dragging = false;
    m_selPage = -1;
    m_selText.clear();
    m_selBoxes.clear();
    viewport()->update();
}

void DoublePageView::mousePressEvent(QMouseEvent* event)
{
    if (!m_selectEnabled || event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    QRectF r;
    const int page = pageAt(event->pos(), &r);
    if (page < 0) {
        clearSelection();
        return;
    }
    m_dragging = true;
    m_selPage = page;
    toPagePoint(page, event->pos(), &m_selStart);
    m_selEnd = m_selStart;
    m_selText.clear();
    m_selBoxes.clear();
    viewport()->update();
    event->accept();
}

void DoublePageView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    // Stay on the page the drag began on: text selection does not span pages.
    toPagePoint(m_selPage, event->pos(), &m_selEnd);

    // getSelection() only accepts two points on ONE line (a diagonal span
    // returns an empty selection), so a multi-line drag cannot use it. Turn
    // both ends into character indices and select that RANGE instead:
    // getSelectionAtIndex() spans lines freely.
    const int i0 = charIndexAt(m_selPage, m_selStart);
    const int i1 = charIndexAt(m_selPage, m_selEnd);
    if (i0 < 0 || i1 < 0) {
        m_selText.clear();
        m_selBoxes.clear();
        viewport()->update();
        event->accept();
        return;
    }
    // Include the character the drag ended on, whichever direction it went.
    const int from = qMin(i0, i1);
    const int to = qMax(i0, i1) + 1;

    const QPdfSelection sel =
        m_document->getSelectionAtIndex(m_selPage, from, to - from);
    m_selText = sel.isValid() ? sel.text() : QString();
    m_selBoxes.clear();
    if (sel.isValid())
        for (const QPolygonF& poly : sel.bounds()) {
            const QRectF b = poly.boundingRect();
            if (b.isValid() && b.width() > 0 && b.height() > 0)
                m_selBoxes.append(b);
        }
    viewport()->update();
    event->accept();
}

void DoublePageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    event->accept();
    if (!m_selText.trimmed().isEmpty()) {
        // Hand the result out; ReaderView shows the copy/cancel prompt. The
        // selection stays tinted until that is answered.
        emit selectionFinished(m_selPage, m_selText, m_selBoxes);
    } else {
        clearSelection();
    }
}

void DoublePageView::paintEvent(QPaintEvent* event)
{
    QPainter p(viewport());
    Q_UNUSED(event);   // transparent backdrop — only pages are painted

    if (!m_document || m_document->pageCount() == 0 || m_rowY.isEmpty())
        return;

    const int yOff = verticalScrollBar()->value();
    const int xOff = horizontalScrollBar()->value();
    const qreal viewTop = yOff;
    const qreal viewBottom = yOff + viewport()->height();
    const int n = m_document->pageCount();

    for (int row = 0; row < m_rowY.size(); ++row) {
        const qreal top = m_rowY[row];
        if (top + m_rowHeight[row] < viewTop || top > viewBottom)
            continue;   // row not visible

        // Center the row horizontally within the content width.
        const int left = row * m_pagesPerRow;
        qreal rowW = 0;
        for (int k = 0; k < m_pagesPerRow && left + k < n; ++k)
            rowW += m_pageSize[left + k].width() + (k > 0 ? kPageGap : 0);
        qreal x = kMargin + (m_contentWidth - rowW) / 2.0
                  + qMax(0.0, (viewport()->width() - m_contentWidth
                               - 2.0 * kMargin) / 2.0);

        for (int page = left;
             page <= qMin(left + m_pagesPerRow - 1, n - 1); ++page) {
            const QSize sz = m_pageSize[page].toSize();
            const QRectF target(x - xOff, top - yOff, sz.width(), sz.height());
            if (target.right() >= 0 && target.left() <= viewport()->width()) {
                // Page shadow + white sheet under the rendered image.
                p.fillRect(target.translated(2, 2), QColor(0, 0, 0, 28));
                p.fillRect(target, Qt::white);
                p.drawPixmap(target.topLeft(), pagePixmap(page, sz));

                // Tint boxes; the live drag selection wins over the stored
                // extraction highlight when both cover the same page.
                QList<QRectF> boxes;
                const auto hl = m_highlights.constFind(page);
                if (hl != m_highlights.constEnd())
                    boxes = *hl;
                if (m_selPage == page && !m_selBoxes.isEmpty())
                    boxes = m_selBoxes;
                if (!boxes.isEmpty()) {
                    // Boxes arrive in PDF POINTS: scaling by m_zoom and
                    // offsetting to the page's screen rect puts them exactly
                    // where the glyphs are - the same transform m_pageSize
                    // (= pointSize * zoom) was built from.
                    const bool live = (m_selPage == page && m_dragging);
                    // The drag tint is stronger so the user sees it form.
                    p.setPen(Qt::NoPen);
                    p.setBrush(live ? QColor(120, 180, 255, 130)
                                    : QColor(255, 214, 102, 110));
                    for (const QRectF& box : std::as_const(boxes))
                        p.drawRect(QRectF(target.left() + box.x() * m_zoom,
                                          target.top() + box.y() * m_zoom,
                                          box.width() * m_zoom,
                                          box.height() * m_zoom));
                }
            }
            x += sz.width() + kPageGap;
        }
    }

    // Warm the cache for nearby rows once this paint is done.
    schedulePrerender();
}

void DoublePageView::schedulePrerender()
{
    if (m_document && m_document->pageCount() > 0 && !m_rowY.isEmpty())
        m_prerender->start();
}

void DoublePageView::prerenderStep()
{
    if (!m_document || m_rowY.isEmpty())
        return;

    // Find the visible row range, then look outward up to kPrerenderRows on
    // each side for the first uncached page; render just ONE page per tick.
    const qreal viewTop = verticalScrollBar()->value();
    const qreal viewBottom = viewTop + viewport()->height();
    int firstRow = 0;
    while (firstRow < m_rowY.size() - 1
           && m_rowY[firstRow] + m_rowHeight[firstRow] < viewTop)
        ++firstRow;
    int lastRow = firstRow;
    while (lastRow < m_rowY.size() - 1 && m_rowY[lastRow + 1] < viewBottom)
        ++lastRow;

    const int n = m_document->pageCount();
    const int from = qMax(0, firstRow - kPrerenderRows);
    const int to = qMin(int(m_rowY.size()) - 1, lastRow + kPrerenderRows);
    for (int row = from; row <= to; ++row) {
        for (int page = row * m_pagesPerRow;
             page <= qMin(row * m_pagesPerRow + m_pagesPerRow - 1, n - 1);
             ++page) {
            const QSize sz = m_pageSize[page].toSize();
            auto it = m_cache.constFind(page);
            if (it == m_cache.constEnd()
                || it->deviceIndependentSize().toSize() != sz) {
                pagePixmap(page, sz);   // render + cache
                m_prerender->start();   // continue with the next one, later
                return;
            }
        }
    }
    // All neighbours cached: nothing to do until the next scroll/zoom.
}

void DoublePageView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    if (m_fitMode != FitMode::Custom)
        applyFit();      // re-derive zoom from the new viewport size
    else
        relayout();      // just refresh scroll ranges
}

void DoublePageView::scrollContentsBy(int, int)
{
    updateCurrentPage();
    viewport()->update();
}

void DoublePageView::jumpToPage(int page)
{
    if (!m_document || m_rowY.isEmpty())
        return;
    const int row = qBound(0, page / m_pagesPerRow, int(m_rowY.size()) - 1);
    // Land half a gap above the row so updateCurrentPage resolves to THIS
    // row — landing a full margin higher keeps the previous row's bottom
    // edge in view and the current page stuck on it (prev/next then no-op).
    const int y = (row == 0) ? 0 : int(m_rowY[row] - kPageGap / 2.0);
    verticalScrollBar()->setValue(y);
}

void DoublePageView::updateCurrentPage()
{
    if (m_rowY.isEmpty())
        return;
    // Top-most row whose bottom edge is below the viewport top.
    const qreal viewTop = verticalScrollBar()->value();
    int row = 0;
    while (row < m_rowY.size() - 1
           && m_rowY[row] + m_rowHeight[row] < viewTop + 1)
        ++row;
    const int page = row * m_pagesPerRow;
    if (page != m_currentPage) {
        m_currentPage = page;
        emit currentPageChanged(page);
    }
}

} // namespace Genesis
