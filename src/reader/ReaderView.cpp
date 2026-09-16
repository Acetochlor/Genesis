#include "ReaderView.h"
#include "DoublePageView.h"
#include "NotesPanel.h"
#include "../dialogs/MdEditorDialog.h"
#include "../bookshelf/IconButton.h"
#include "../widgets/MenuOption.h"
#include "../dialogs/FramelessDialog.h"
#include "../library/BookmarkStore.h"
#include "../library/MetadataStore.h"
#include "../library/ReadingLogStore.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeView>
#include <QMenu>
#include <QWidgetAction>
#include <QEvent>
#include <QFileInfo>
#include <QIntValidator>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QResizeEvent>
#include <QTextBrowser>
#include <QTimer>
#include <QPdfDocument>
#include <QPdfBookmarkModel>
#include <QPdfPageNavigator>
#include <QPdfSelection>
#include <QClipboard>
#include <QGuiApplication>
#include <QToolTip>
#include <QCursor>
#include <QPainter>

namespace Genesis {

namespace {
const int kBarHeight = 32;   // matches the notes panel's control bar
const int kTocWidth = 260;
const qreal kZoomStep = 1.2;
} // namespace

ReaderView::ReaderView(QWidget* parent)
    : QWidget(parent)
    , m_bar(nullptr)
    , m_tocButton(nullptr)
    , m_title(nullptr)
    , m_pageBox(nullptr)
    , m_pageTotal(nullptr)
    , m_fitButton(nullptr)
    , m_kebabButton(nullptr)
    , m_revealStrip(nullptr)
    , m_centerBox(nullptr)
    , m_pdfColumn(nullptr)
    , m_viewStack(nullptr)
    , m_document(new QPdfDocument(this))
    , m_singleView(nullptr)
    , m_doubleView(nullptr)
    , m_tocView(nullptr)
    , m_tocAnim(nullptr)
    , m_notes(nullptr)
    , m_markdownPreviewPanel(nullptr)
    , m_markdownPreview(nullptr)
    , m_markdownPreviewAnim(nullptr)
    , m_fitToWidth(false)
    , m_doublePage(false)
    , m_highlightPage(-1)
    , m_tocOpen(false)
    , m_readTimer(new QTimer(this))
    , m_readSeconds(0)
    , m_sessionSeconds(0)
{
    // Every 5 seconds while the book is open, bill the elapsed wall-clock
    // period to the session accumulator.
    m_readTimer->setInterval(5000);
    connect(m_readTimer, &QTimer::timeout, this, [this]() {
        m_sessionSeconds += m_lastTick.secsTo(QDateTime::currentDateTime());
        m_lastTick = QDateTime::currentDateTime();
    });
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- body: PDF column 60% left, notes column 40% right ---
    QHBoxLayout* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    // PDF column: control bar over the page view.
    m_pdfColumn = new QWidget(this);
    QWidget* pdfColumn = m_pdfColumn;
    QVBoxLayout* pdfLayout = new QVBoxLayout(pdfColumn);
    pdfLayout->setContentsMargins(0, 0, 0, 0);
    pdfLayout->setSpacing(0);

    m_bar = buildControlBar();
    pdfLayout->addWidget(m_bar);

    // Single-page and double-page views, stacked. Both keep a transparent
    // viewport so the app background image shows through.
    m_viewStack = new QStackedWidget(pdfColumn);
    m_viewStack->setStyleSheet("QStackedWidget { background: transparent; }");

    // Both views are DoublePageView: one page per row for single-page mode,
    // two for the spread. Sharing the class means the page<->viewport
    // transform is ours (see m_pagesPerRow), which is what makes text
    // highlighting and mouse selection possible at all - QPdfView exposed
    // none of it.
    m_singleView = new DoublePageView(m_viewStack);
    m_singleView->setDocument(m_document);
    m_singleView->setPagesPerRow(1);
    m_viewStack->addWidget(m_singleView);

    m_doubleView = new DoublePageView(m_viewStack);
    m_doubleView->setDocument(m_document);
    m_viewStack->addWidget(m_doubleView);

    pdfLayout->addWidget(m_viewStack, 1);
    body->addWidget(pdfColumn, 3);   // 60%

    // Notes column: Markdown source editor with per-book persistence;
    // rendering is opened on demand from the notes kebab menu.
    QWidget* notes = new QWidget(this);
    notes->setObjectName("readerNotes");
    notes->setAttribute(Qt::WA_StyledBackground, true);
    notes->setStyleSheet(
        "#readerNotes { background: transparent;"
        "  border-left: 1px solid rgba(204, 238, 255, 180); }");
    QVBoxLayout* notesLayout = new QVBoxLayout(notes);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    m_notes = new NotesPanel(notes);
    notesLayout->addWidget(m_notes);
    body->addWidget(notes, 2);       // 40%

    layout->addLayout(body, 1);

    // --- on-demand Markdown preview: slides from outside the left edge and
    // covers exactly the 60% PDF column, while the source editor remains
    // visible in the right 40%. Its vertical extent matches the PDF column.
    m_markdownPreviewPanel = new QWidget(this);
    m_markdownPreviewPanel->setObjectName("markdownPreviewPanel");
    m_markdownPreviewPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_markdownPreviewPanel->setStyleSheet(
        "#markdownPreviewPanel { background: #FFFFFF; }");

    // Use real separator widgets instead of stylesheet borders. Child widgets
    // can paint over a parent's border in Qt; dedicated 1px/2px widgets keep
    // both rules visible regardless of label or preview repainting.
    QHBoxLayout* previewOuterLayout = new QHBoxLayout(m_markdownPreviewPanel);
    previewOuterLayout->setContentsMargins(0, 0, 0, 0);
    previewOuterLayout->setSpacing(0);

    QWidget* previewContent = new QWidget(m_markdownPreviewPanel);
    previewContent->setAttribute(Qt::WA_StyledBackground, true);
    previewContent->setStyleSheet("background: #FFFFFF;");
    QVBoxLayout* previewLayout = new QVBoxLayout(previewContent);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(0);

    QWidget* previewBar = new QWidget(previewContent);
    previewBar->setFixedHeight(kBarHeight - 1);
    previewBar->setAttribute(Qt::WA_StyledBackground, true);
    previewBar->setStyleSheet("background: #FFFFFF;");
    QGridLayout* previewBarLayout = new QGridLayout(previewBar);
    previewBarLayout->setContentsMargins(6, 0, 6, 0);
    previewBarLayout->setHorizontalSpacing(0);
    previewBarLayout->setColumnMinimumWidth(0, 30);
    previewBarLayout->setColumnStretch(1, 1);
    previewBarLayout->setColumnMinimumWidth(2, 30);

    QLabel* previewTitle = new QLabel(QString::fromUtf8("Markdown渲染"), previewBar);
    previewTitle->setAlignment(Qt::AlignCenter);
    previewTitle->setStyleSheet(
        "color: #333333; font-size: 13px; font-weight: bold; border: none;");
    previewBarLayout->addWidget(previewTitle, 0, 1);

    IconButton* closePreview =
        new IconButton(IconButton::Glyph::ChevronLeft, previewBar);
    closePreview->setToolTip(QString::fromUtf8("收起Markdown渲染"));
    connect(closePreview, &IconButton::clicked,
            this, &ReaderView::hideMarkdownPreview);
    previewBarLayout->addWidget(closePreview, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    previewLayout->addWidget(previewBar);

    QWidget* headerSeparator = new QWidget(previewContent);
    headerSeparator->setFixedHeight(1);
    headerSeparator->setStyleSheet("background: #E0E6ED;");
    previewLayout->addWidget(headerSeparator);

    m_markdownPreview = createMarkdownPreview(previewContent);
    previewLayout->addWidget(m_markdownPreview, 1);
    previewOuterLayout->addWidget(previewContent, 1);

    QWidget* rightSeparator = new QWidget(m_markdownPreviewPanel);
    rightSeparator->setFixedWidth(2);
    rightSeparator->setStyleSheet("background: #CCEEFF;");
    previewOuterLayout->addWidget(rightSeparator);
    m_markdownPreviewPanel->hide();

    m_markdownPreviewAnim = new QPropertyAnimation(
        m_markdownPreviewPanel, "pos", this);
    m_markdownPreviewAnim->setDuration(180);
    m_markdownPreviewAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_notes, &NotesPanel::markdownRenderRequested,
            this, &ReaderView::showMarkdownPreview);
    // Live preview on typing: defer to idle so typing a long note isn't met
    // with a full-document re-render on every keystroke.
    m_previewDebounce = new QTimer(this);
    m_previewDebounce->setSingleShot(true);
    m_previewDebounce->setInterval(250);
    connect(m_previewDebounce, &QTimer::timeout,
            this, &ReaderView::refreshMarkdownPreview);
    connect(m_notes, &NotesPanel::markdownTextChanged, this,
            [this](const QString& markdown) {
                m_pendingMarkdown = markdown;
                m_previewDebounce->start();
            });

    // --- floating reveal strip (chevron-down), shown when the bar is hidden ---
    m_revealStrip = new QWidget(pdfColumn);
    m_revealStrip->setFixedSize(44, 18);
    m_revealStrip->setAttribute(Qt::WA_StyledBackground, true);
    m_revealStrip->setStyleSheet(
        "background: rgba(255, 255, 255, 220);"
        "border: 1px solid #CCEEFF; border-top: none;"
        "border-bottom-left-radius: 6px; border-bottom-right-radius: 6px;");
    {
        QHBoxLayout* rl = new QHBoxLayout(m_revealStrip);
        rl->setContentsMargins(0, 0, 0, 0);
        IconButton* reveal =
            new IconButton(IconButton::Glyph::ChevronDown, m_revealStrip);
        reveal->setFixedSize(42, 16);
        reveal->setToolTip(QString::fromUtf8("显示控制栏"));
        connect(reveal, &IconButton::clicked,
                this, [this]() { setBarVisible(true); });
        rl->addWidget(reveal);
    }
    m_revealStrip->hide();

    // --- TOC drawer: slides over the PDF view's left edge ---
    m_tocView = new QTreeView(m_viewStack);
    m_tocView->setHeaderHidden(true);
    m_tocView->setFixedWidth(kTocWidth);
    m_tocView->setStyleSheet(
        "QTreeView { background: #FFFFFF; color: #333333;"
        "  border: none; border-right: 1px solid #CCEEFF; font-size: 12px; }"
        "QTreeView::item { padding: 4px 2px; }"
        "QTreeView::item:hover { background: #E6F7FF; }"
        "QTreeView::item:selected { background: #CCEEFF; color: #333333; }");
    QPdfBookmarkModel* bookmarks = new QPdfBookmarkModel(this);
    bookmarks->setDocument(m_document);
    m_tocView->setModel(bookmarks);
    // Clicking an entry jumps to its page.
    connect(m_tocView, &QTreeView::clicked, this, [this](const QModelIndex& ix) {
        const int page =
            ix.data(int(QPdfBookmarkModel::Role::Page)).toInt();
        if (page >= 0)
            jumpToPage(page);
    });
    m_tocView->hide();

    m_tocAnim = new QPropertyAnimation(m_tocView, "pos", this);
    m_tocAnim->setDuration(160);
    m_tocAnim->setEasingCurve(QEasingCurve::OutCubic);

    // Keep the page box in sync while scrolling (both views).
    connect(m_singleView, &DoublePageView::currentPageChanged,
            this, [this](int page) {
                if (!m_doublePage)
                    syncPageBox(page);
            });
    connect(m_doubleView, &DoublePageView::currentPageChanged,
            this, [this](int page) {
                if (m_doublePage)
                    syncPageBox(page);
            });
    // Drag-selection on the page, both modes. On release the view hands the
    // selected text over and ReaderView offers to copy it. Selection stays
    // DISARMED until 提取图文 turns it on, so the feature has a clear
    // on/off state rather than hijacking every drag forever.
    for (DoublePageView* v : {m_singleView, m_doubleView}) {
        v->setSelectionEnabled(false);
        connect(v, &DoublePageView::selectionFinished, this,
                [this](int page, const QString& text,
                       const QList<QRectF>& boxes) {
                    onPageSelectionFinished(page, text, boxes);
                });
        // Turning the page ends the extraction session: the boxes belong to
        // the page that was on screen when they were made, and the drag arming
        // should not outlive it either.
        connect(v, &DoublePageView::currentPageChanged, this,
                [this](int) { endTextExtraction(); });
    }

    connect(m_document, &QPdfDocument::statusChanged, this,
            [this](QPdfDocument::Status status) {
                if (status == QPdfDocument::Status::Ready) {
                    m_pageTotal->setText(
                        QStringLiteral("/ %1").arg(m_document->pageCount()));
                    syncPageBox(0);
                }
            });

    // App-wide filter: any click outside the open TOC drawer closes it
    // (clicks land on child widgets, so ReaderView never sees them itself).
    qApp->installEventFilter(this);
}

QWidget* ReaderView::buildControlBar()
{
    QWidget* bar = new QWidget(this);
    bar->setObjectName("readerBar");
    bar->setFixedHeight(kBarHeight);
    bar->setAttribute(Qt::WA_StyledBackground, true);
    // Scoped to the bar itself: an unscoped rule would cascade the
    // border-bottom onto every child (underlining the page cluster).
    bar->setStyleSheet(
        "#readerBar { background: #FFFFFF;"
        "  border-bottom: 1px solid #E0E6ED; }");

    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 0, 8, 0);
    barLayout->setSpacing(6);

    // ---- left: hamburger + title ----
    m_tocButton = new IconButton(IconButton::Glyph::Hamburger, bar);
    m_tocButton->setToolTip(QString::fromUtf8("目录"));
    connect(m_tocButton, &IconButton::clicked, this, &ReaderView::toggleToc);
    barLayout->addWidget(m_tocButton);

    m_title = new QLabel(bar);
    m_title->setStyleSheet(
        "color: #333333; font-size: 13px; font-weight: bold; border: none;");
    m_title->setMinimumWidth(0);
    barLayout->addWidget(m_title);

    barLayout->addStretch(1);

    // ---- right: zoom -/+, fit toggle, hide chevron, kebab ----
    IconButton* zoomOut = new IconButton(IconButton::Glyph::ZoomOut, bar);
    zoomOut->setToolTip(QString::fromUtf8("缩小"));
    connect(zoomOut, &IconButton::clicked,
            this, [this]() { zoomBy(1.0 / kZoomStep); });
    barLayout->addWidget(zoomOut);

    IconButton* zoomIn = new IconButton(IconButton::Glyph::ZoomIn, bar);
    zoomIn->setToolTip(QString::fromUtf8("放大"));
    connect(zoomIn, &IconButton::clicked,
            this, [this]() { zoomBy(kZoomStep); });
    barLayout->addWidget(zoomIn);

    m_fitButton = new IconButton(IconButton::Glyph::FitWidth, bar);
    m_fitButton->setToolTip(QString::fromUtf8("适应宽度"));
    connect(m_fitButton, &IconButton::clicked,
            this, &ReaderView::toggleFitMode);
    barLayout->addWidget(m_fitButton);

    barLayout->addSpacing(4);

    IconButton* hide = new IconButton(IconButton::Glyph::ChevronUp, bar);
    hide->setToolTip(QString::fromUtf8("隐藏控制栏"));
    connect(hide, &IconButton::clicked,
            this, [this]() { setBarVisible(false); });
    barLayout->addWidget(hide);

    m_kebabButton = new IconButton(IconButton::Glyph::Kebab, bar);
    m_kebabButton->setToolTip(QString::fromUtf8("更多"));
    connect(m_kebabButton, &IconButton::clicked,
            this, &ReaderView::showKebabMenu);
    barLayout->addWidget(m_kebabButton);

    // ---- center cluster: prev | page box "/ total" | next ----
    // Floated over the bar and re-centered on every bar resize, so it sits at
    // the exact horizontal center of the 60% PDF column regardless of how
    // wide the left/right clusters are.
    m_centerBox = new QWidget(bar);
    QHBoxLayout* centerLayout = new QHBoxLayout(m_centerBox);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(6);

    IconButton* prev = new IconButton(IconButton::Glyph::ChevronLeft, m_centerBox);
    prev->setToolTip(QString::fromUtf8("上一页"));
    connect(prev, &IconButton::clicked,
            this, [this]() { jumpToPage(currentPage() - (m_doublePage ? 2 : 1)); });
    centerLayout->addWidget(prev);

    m_pageBox = new QLineEdit(m_centerBox);
    m_pageBox->setFixedSize(52, 26);
    m_pageBox->setAlignment(Qt::AlignCenter);
    m_pageBox->setValidator(new QIntValidator(1, 999999, m_pageBox));
    // Click-to-type only: with tab focus, the stacked-widget page switch
    // hands this box the initial focus, and the hasFocus() guard in
    // syncPageBox then blocks every update — the page number sticks at 1.
    m_pageBox->setFocusPolicy(Qt::ClickFocus);
    m_pageBox->setStyleSheet(
        "QLineEdit { background: #FFFFFF; color: #333333;"
        "  border: 1px solid #CCEEFF; border-radius: 4px; font-size: 12px; }"
        "QLineEdit:focus { border-color: #33CCFF; }");
    connect(m_pageBox, &QLineEdit::returnPressed, this, [this]() {
        jumpToPage(m_pageBox->text().toInt() - 1);
        m_pageBox->clearFocus();
    });
    centerLayout->addWidget(m_pageBox);

    m_pageTotal = new QLabel(QStringLiteral("/ 0"), m_centerBox);
    m_pageTotal->setStyleSheet(
        "color: #888888; font-size: 12px; border: none;");
    centerLayout->addWidget(m_pageTotal);

    IconButton* next = new IconButton(IconButton::Glyph::ChevronRight, m_centerBox);
    next->setToolTip(QString::fromUtf8("下一页"));
    connect(next, &IconButton::clicked,
            this, [this]() { jumpToPage(currentPage() + (m_doublePage ? 2 : 1)); });
    centerLayout->addWidget(next);

    bar->installEventFilter(this);   // re-center m_centerBox on resize
    return bar;
}

void ReaderView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // The layout updates the PDF/notes split during the resize pass. Defer
    // reading the column geometry one turn so the open overlay follows the
    // final responsive size rather than the previous width.
    if (!m_markdownPreviewPanel || !m_markdownPreviewPanel->isVisible())
        return;
    QTimer::singleShot(0, this, [this]() {
        if (!m_markdownPreviewPanel || !m_markdownPreviewPanel->isVisible()
            || !m_pdfColumn)
            return;
        const int w = m_pdfColumn->width();
        const int h = height();
        m_markdownPreviewPanel->setFixedSize(w, h);
        m_markdownPreviewPanel->move(0, 0);
        m_markdownPreviewPanel->raise();
    });
}

bool ReaderView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_bar && event->type() == QEvent::Resize) {
        // Dead-center the page cluster within the whole control bar.
        const QSize s = m_centerBox->sizeHint();
        m_centerBox->resize(s);
        m_centerBox->move((m_bar->width() - s.width()) / 2,
                          (m_bar->height() - s.height()) / 2);
        m_centerBox->raise();
    }

    // Click-away closes the TOC drawer: any press outside the drawer AND
    // outside the hamburger button (whose own handler toggles the drawer —
    // closing here too would make that click reopen it).
    if (m_tocOpen && event->type() == QEvent::MouseButtonPress) {
        QWidget* w = qobject_cast<QWidget*>(watched);
        if (w && w->window() == window()
            && !m_tocView->isAncestorOf(w) && w != m_tocView
            && !m_tocButton->isAncestorOf(w) && w != m_tocButton)
            toggleToc();
    }

    return QWidget::eventFilter(watched, event);
}

void ReaderView::openBook(const QString& path)
{
    // Bill the stretch since the last timer tick before anything resets the
    // session. The timer only fires every few seconds, so the trailing partial
    // interval would otherwise be silently dropped on every book change.
    const QDateTime now = QDateTime::currentDateTime();
    if (m_lastTick.isValid())
        m_sessionSeconds += int(m_lastTick.secsTo(now));

    // Reopening the book that is already open (the reader jumps to a bookmark
    // of its own book without going through the shelf, which is the only place
    // metadata is persisted) must not reload the stored counters: the copy on
    // disk is stale, and overwriting the live ones with it discards everything
    // read since the last save.
    const bool sameBook = (path == m_bookPath);

    m_bookPath = path;
    // A new book starts outside any extraction session.
    endTextExtraction();
    hideMarkdownPreview();
    m_notes->setBook(path);   // load this book's Markdown notes

    if (!sameBook) {
        // Load persisted metadata and resume the read timer.
        const auto meta = MetadataStore::load(path);
        m_readSeconds = meta ? meta->readSeconds : 0;
        m_sessionSeconds = 0;
    }
    m_lastTick = now;
    // The reading-log watermark restarts with the book, because leaving the
    // reader flushes the outgoing book's stretch first. Reopening the book
    // already open skips that flush, so keep the watermark and let the pending
    // stretch keep accruing to this same book.
    if (!sameBook)
        m_loggedTick = now;
    m_readTimer->start();

    m_document->load(path);
    m_title->setText(QFileInfo(path).completeBaseName());
    // Reset view state: single page, fit to viewport height, from page 1.
    if (m_doublePage)
        setDoublePage(false);
    m_fitToWidth = false;
    m_fitButton->setGlyph(IconButton::Glyph::FitWidth);
    m_fitButton->setToolTip(QString::fromUtf8("适应宽度"));
    applyFitMode();
    if (QScrollBar* bar = m_singleView->verticalScrollBar())
        bar->setValue(0);
    if (m_tocOpen)
        toggleToc();
    setBarVisible(true);
    // Focus the view itself, so the page box never opens holding focus.
    m_viewStack->currentWidget()->setFocus();
}

void ReaderView::openBookAt(const QString& path, int page)
{
    openBook(path);
    jumpToPage(page);
}

void ReaderView::closeBook()
{
    m_readTimer->stop();
    m_notes->setBook(QString());   // save + clear the notes editor
    // Load an empty (invalid) document to make QPdfDocument release the
    // current file handle — Windows can't delete a file that's still open.
    m_document->close();
    m_bookPath.clear();
}

int ReaderView::takeUnloggedSeconds(QDateTime* startOut)
{
    if (!m_loggedTick.isValid())
        return 0;
    const QDateTime now = QDateTime::currentDateTime();
    const int delta = int(m_loggedTick.secsTo(now));
    // A backwards clock jump yields a negative delta; drop it rather than
    // writing nonsense, and re-anchor either way.
    m_loggedTick = now;
    if (delta <= 0)
        return 0;
    if (startOut)
        *startOut = now.addSecs(-delta);
    return delta;
}

void ReaderView::logPendingReading()
{
    if (m_bookPath.isEmpty())
        return;
    QDateTime start;
    const int secs = takeUnloggedSeconds(&start);
    if (secs <= 0 || !start.isValid())
        return;
    ReadingSession s;
    s.start = start;
    s.seconds = secs;
    s.bookPath = m_bookPath;
    ReadingLogStore::add(s);
}

void ReaderView::setBarVisible(bool visible)
{
    m_bar->setVisible(visible);
    m_revealStrip->setVisible(!visible);
    if (!visible) {
        // Hiding the bar: also close the TOC drawer so it doesn't linger with
        // no bar-anchor beneath it.
        if (m_tocOpen)
            toggleToc();
        // Top-center of the PDF column, floating over the view.
        QWidget* pdfColumn = m_bar->parentWidget();
        m_revealStrip->move((pdfColumn->width() - m_revealStrip->width()) / 2, 0);
        m_revealStrip->raise();
    }
}

void ReaderView::refreshMarkdownPreview()
{
    if (!m_markdownPreview || !m_markdownPreviewPanel->isVisible())
        return;
    renderMarkdownPreview(
        m_markdownPreview,
        m_pendingMarkdown.isNull() ? m_notes->markdownText()
                                   : m_pendingMarkdown);
}

void ReaderView::showMarkdownPreview()
{
    if (!m_markdownPreviewPanel || !m_pdfColumn)
        return;

    const int previewWidth = m_pdfColumn->width();
    const int previewHeight = height();
    m_markdownPreviewPanel->setFixedSize(previewWidth, previewHeight);
    // Remember where the source is scrolled before rendering, so the preview
    // can be opened at the same place instead of always at the top.
    const int sourceLine = m_notes->topVisibleLine();
    renderMarkdownPreview(m_markdownPreview, m_notes->markdownText());
    m_markdownPreviewPanel->setParent(this);
    m_markdownPreviewPanel->move(-previewWidth, 0);
    m_markdownPreviewPanel->show();
    m_markdownPreviewPanel->raise();
    // Scroll AFTER show(): a hidden QTextBrowser lays its document out at a
    // stale width, so blockBoundingRect() would report wrong geometry.
    if (sourceLine > 0) {
        m_markdownPreviewPanel->layout()->activate();
        scrollMarkdownPreviewToSourceLine(m_markdownPreview, sourceLine);
    }
    m_markdownPreviewAnim->stop();
    // A previous hide animation installs a finished handler that hides the
    // panel. Remove it before opening; otherwise that stale handler also runs
    // when the show animation finishes and the drawer immediately vanishes.
    disconnect(m_markdownPreviewAnim, &QPropertyAnimation::finished,
               this, nullptr);
    m_markdownPreviewAnim->setStartValue(QPoint(-previewWidth, 0));
    m_markdownPreviewAnim->setEndValue(QPoint(0, 0));
    m_markdownPreviewAnim->start();
}

void ReaderView::hideMarkdownPreview()
{
    if (!m_markdownPreviewPanel || !m_markdownPreviewPanel->isVisible())
        return;

    const int width = m_markdownPreviewPanel->width();
    m_markdownPreviewAnim->stop();
    m_markdownPreviewAnim->setStartValue(m_markdownPreviewPanel->pos());
    m_markdownPreviewAnim->setEndValue(QPoint(-width, 0));
    disconnect(m_markdownPreviewAnim, &QPropertyAnimation::finished,
               this, nullptr);
    connect(m_markdownPreviewAnim, &QPropertyAnimation::finished, this,
            [this]() {
                if (m_markdownPreviewAnim->endValue().toPoint().x() < 0)
                    m_markdownPreviewPanel->hide();
            });
    m_markdownPreviewAnim->start();
}

void ReaderView::toggleToc()
{
    m_tocOpen = !m_tocOpen;
    if (m_tocOpen) {
        m_tocView->setFixedHeight(m_viewStack->height());
        m_tocView->move(-kTocWidth, 0);
        m_tocView->show();
        m_tocView->raise();
        m_tocAnim->stop();
        m_tocAnim->setStartValue(QPoint(-kTocWidth, 0));
        m_tocAnim->setEndValue(QPoint(0, 0));
        m_tocAnim->start();
    } else {
        m_tocAnim->stop();
        m_tocAnim->setStartValue(m_tocView->pos());
        m_tocAnim->setEndValue(QPoint(-kTocWidth, 0));
        disconnect(m_tocAnim, &QPropertyAnimation::finished, this, nullptr);
        connect(m_tocAnim, &QPropertyAnimation::finished, this, [this]() {
            if (!m_tocOpen)
                m_tocView->hide();
        });
        m_tocAnim->start();
    }
}

void ReaderView::showKebabMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #FFFFFF; color: #333333;"
        "  border: 1px solid #CCEEFF; }"
        "QMenu::item { padding: 6px 26px; }"
        "QMenu::item:selected { background: #E6F7FF; color: #33CCFF; }");

    // App-wide toggle style, dot between the words: "双页 ● 视图".
    QWidgetAction* doublePage = new QWidgetAction(&menu);
    doublePage->setDefaultWidget(new MenuOption(
        QString::fromUtf8("双页"), QString::fromUtf8("视图"),
        m_doublePage, [this, &menu]() {
            menu.close();
            setDoublePage(!m_doublePage);
        }));
    menu.addAction(doublePage);

    QAction* addBm = menu.addAction(QString::fromUtf8("添加书签"));
    connect(addBm, &QAction::triggered, this, &ReaderView::addBookmark);

    // Text-layer extraction. Enabled only in single-page mode: the double-page
    // view is a separate renderer with no text plumbing yet, so the entry is
    // offered but explains itself when clicked there.
    QAction* extract = menu.addAction(QString::fromUtf8("提取图文"));
    connect(extract, &QAction::triggered, this, &ReaderView::extractPageText);

    menu.exec(m_kebabButton->mapToGlobal(
        QPoint(m_kebabButton->width() - menu.sizeHint().width(),
               m_kebabButton->height() + 2)));
}

void ReaderView::extractPageText()
{
    if (!m_document || m_document->pageCount() == 0)
        return;

    const int page = currentPage();
    if (page < 0 || page >= m_document->pageCount())
        return;

    // A page with no text layer has nothing to drag over, so say so instead of
    // arming a selection that can never produce anything (a scanned page).
    const QPdfSelection sel = m_document->getAllText(page);
    if (!sel.isValid() || sel.text().trimmed().isEmpty()) {
        QToolTip::showText(
            QCursor::pos(),
            QString::fromUtf8("第 %1 页没有可提取的文字（可能是扫描件）")
                .arg(page + 1),
            m_kebabButton);
        return;
    }

    // Extract only ENTERS the extraction session: arm drag-selection. Nothing
    // is copied and nothing is announced - the cursor turning into an I-beam
    // over the page IS the feedback. Answering the copy prompt (or turning the
    // page) leaves the session again; see onPageSelectionFinished() /
    // endTextExtraction().
    for (DoublePageView* v : {m_singleView, m_doubleView})
        if (v)
            v->setSelectionEnabled(true);
}

void ReaderView::endTextExtraction()
{
    // Leave the extraction session: drop the tint, forget the pick, and stop
    // intercepting drags so normal reading gestures work again.
    clearTextHighlights();
    for (DoublePageView* v : {m_singleView, m_doubleView}) {
        if (!v)
            continue;
        v->clearSelection();
        v->setSelectionEnabled(false);
    }
}

void ReaderView::showTextHighlights(int page, const QList<QRectF>& boxesPdfPoints)
{
    m_highlightPage = page;
    m_highlightBoxes = boxesPdfPoints;

    // Both views are DoublePageView now, so the tint is drawn the same way in
    // each - no per-mode branch.
    if (m_singleView)
        m_singleView->setHighlights(page, boxesPdfPoints);
    if (m_doubleView)
        m_doubleView->setHighlights(page, boxesPdfPoints);
}

void ReaderView::clearTextHighlights()
{
    if (m_highlightPage < 0)
        return;
    if (m_singleView)
        m_singleView->setHighlights(m_highlightPage, {});
    if (m_doubleView)
        m_doubleView->setHighlights(m_highlightPage, {});
    m_highlightPage = -1;
    m_highlightBoxes.clear();
}

void ReaderView::onPageSelectionFinished(int page, const QString& text,
                                         const QList<QRectF>& boxesPdfPoints)
{
    if (text.trimmed().isEmpty())
        return;

    // Show where the selection landed while the prompt is up.
    showTextHighlights(page, boxesPdfPoints);

    // The prompt reports the size only - the page itself already shows WHAT
    // was picked, so echoing the text here was redundant.
    const bool copy = FramelessDialog::question(
        this, QString::fromUtf8("复制选中文字"),
        QString::fromUtf8("已选中 %1 个字符").arg(text.size()));

    // Either answer CLOSES the extraction: copy on confirm, then leave the
    // session entirely - tint down, pick forgotten, drag-selection disarmed.
    if (copy)
        if (QClipboard* cb = QGuiApplication::clipboard())
            cb->setText(text);

    endTextExtraction();
}

void ReaderView::addBookmark()
{
    if (m_bookPath.isEmpty() || m_document->pageCount() == 0)
        return;

    // Take the page from the view, not from the page box: the box is a text
    // field the user may be mid-edit in, and an empty one reads back as 0,
    // which would silently file the bookmark against page 1. currentPage() is
    // the page actually on screen and cannot be in an invalid state.
    const int page = qBound(0, currentPage(), m_document->pageCount() - 1);

    // Name-it dialog: pre-filled with "第 N 页", accept on 确定/回车.
    FramelessDialog dlg(QString::fromUtf8("添加书签"), window());

    QLabel* hint = new QLabel(
        QString::fromUtf8("书签位置：第 %1 页").arg(page + 1), &dlg);
    hint->setStyleSheet("color: #888888; font-size: 12px;");
    dlg.contentLayout()->addWidget(hint);

    QLineEdit* nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(QString::fromUtf8("书签名称"));
    nameEdit->setText(QString::fromUtf8("第 %1 页").arg(page + 1));
    nameEdit->selectAll();
    nameEdit->setMinimumWidth(260);
    nameEdit->setStyleSheet(
        "QLineEdit { background: #FFFFFF; color: #333333;"
        "  border: 1px solid #CCEEFF; border-radius: 4px; padding: 5px 8px; }"
        "QLineEdit:focus { border-color: #33CCFF; }");
    dlg.contentLayout()->addWidget(nameEdit);

    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addStretch();
    QPushButton* cancel = new QPushButton(QString::fromUtf8("取消"), &dlg);
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setStyleSheet(
        "QPushButton { background: #FFFFFF; color: #33CCFF;"
        "  border: 1px solid #CCEEFF; border-radius: 4px; padding: 6px 18px; }"
        "QPushButton:hover { background: #E6F7FF; }"
        "QPushButton:pressed { background: #CCEEFF; }");
    QPushButton* ok = new QPushButton(QString::fromUtf8("确定"), &dlg);
    ok->setCursor(Qt::PointingHandCursor);
    ok->setDefault(true);
    ok->setStyleSheet(
        "QPushButton { background: #33CCFF; color: #FFFFFF; border: none;"
        "  border-radius: 4px; padding: 6px 18px; }"
        "QPushButton:hover { background: #2AB8E8; }"
        "QPushButton:pressed { background: #229FCC; }");
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    dlg.contentLayout()->addSpacing(4);
    dlg.contentLayout()->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(nameEdit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
    nameEdit->setFocus();

    if (dlg.exec() != QDialog::Accepted)
        return;

    Bookmark bm;
    bm.bookPath = m_bookPath;
    bm.name = nameEdit->text().trimmed();
    if (bm.name.isEmpty())
        bm.name = QString::fromUtf8("第 %1 页").arg(page + 1);
    bm.page = page;
    bm.createdAt = QDateTime::currentDateTime();
    BookmarkStore::add(bm);
}

void ReaderView::setDoublePage(bool on)
{
    if (m_doublePage == on)
        return;
    // Carry the reading position across the switch.
    const int page = currentPage();
    m_doublePage = on;
    m_viewStack->setCurrentWidget(on ? static_cast<QWidget*>(m_doubleView)
                                     : static_cast<QWidget*>(m_singleView));
    m_viewStack->currentWidget()->setFocus();
    applyFitMode();
    jumpToPage(page);
}

int ReaderView::currentPage() const
{
    return m_doublePage ? m_doubleView->currentPage()
                        : m_singleView->currentPage();
}

void ReaderView::jumpToPage(int page)
{
    if (m_document->pageCount() == 0)
        return;
    page = qBound(0, page, m_document->pageCount() - 1);
    if (m_doublePage)
        m_doubleView->jumpToPage(page);
    else
        m_singleView->jumpToPage(page);
    syncPageBox(page);
}

void ReaderView::syncPageBox(int page)
{
    if (!m_pageBox->hasFocus())   // don't fight the user while typing
        m_pageBox->setText(QString::number(page + 1));
}

void ReaderView::zoomBy(qreal factor)
{
    // Zooming leaves fit mode (Custom), on whichever view is showing.
    DoublePageView* v = m_doublePage ? m_doubleView : m_singleView;
    v->setFitMode(DoublePageView::FitMode::Custom);
    v->setZoomFactor(v->zoomFactor() * factor);
}

void ReaderView::toggleFitMode()
{
    m_fitToWidth = !m_fitToWidth;
    applyFitMode();
}

void ReaderView::applyFitMode()
{
    // Same call on either view - only the page count per row differs.
    DoublePageView* v = m_doublePage ? m_doubleView : m_singleView;
    v->setFitMode(m_fitToWidth ? DoublePageView::FitMode::FitWidth
                               : DoublePageView::FitMode::FitPage);

    // Sync the icon to the CURRENT mode (icon shows what the NEXT click does).
    m_fitButton->setGlyph(m_fitToWidth ? IconButton::Glyph::FitPage
                                       : IconButton::Glyph::FitWidth);
    m_fitButton->setToolTip(m_fitToWidth
        ? QString::fromUtf8("适应页面")
        : QString::fromUtf8("适应宽度"));
}

} // namespace Genesis
