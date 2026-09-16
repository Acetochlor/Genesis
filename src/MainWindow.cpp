#include "MainWindow.h"
#include "titlebar/TitleBar.h"
#include "titlebar/TitleMenuItem.h"
#include "dialogs/FramelessDialog.h"
#include "dialogs/BookmarkDialog.h"
#include "dialogs/ExcerptDialog.h"
#include "library/Library.h"
#include "library/BookInfo.h"
#include "library/MetadataStore.h"
#include "library/ReadingLogStore.h"
#include "library/ExcerptStore.h"
#include "settings/BackgroundDialog.h"
#include "settings/BackgroundWidget.h"
#include "settings/Theme.h"
#include "bookshelf/BookshelfView.h"
#include "reader/ReaderView.h"
#include "reader/ExcerptView.h"
#include "reader/NotesPanel.h"
#include "stats/ReadingStatsPanel.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QCloseEvent>
#include <QTimer>
#include <QStackedWidget>
#include <QMouseEvent>
#include <QEvent>
#include <QFileDialog>
#include <QShowEvent>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QSettings>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTextEdit>

namespace Genesis {

namespace {
// A native file dialog that centers itself over its parent window. The centering
// has to happen in showEvent(): a native QFileDialog reports a placeholder size
// (100x30) until the platform window is actually mapped, so computing the
// position beforehand lands it in the wrong place.
class CenteredFileDialog : public QFileDialog {
public:
    using QFileDialog::QFileDialog;

protected:
    void showEvent(QShowEvent* event) override
    {
        QFileDialog::showEvent(event);
        // Only on the first show of each appearance: repositioning on every
        // show would fight the user after they have moved the dialog.
        if (m_placed)
            return;
        m_placed = true;
        if (QWidget* p = parentWidget()) {
            const QPoint c = p->window()->frameGeometry().center();
            move(c.x() - width() / 2, c.y() - height() / 2);
        }
    }

private:
    bool m_placed = false;
};
} // namespace

// Width (px) of the invisible grab band along the window edges used for manual
// resizing of the frameless window.
static constexpr int kResizeBorder = 6;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_titleBar(nullptr)
    , m_content(nullptr)
    , m_stack(nullptr)
    , m_part1(nullptr)
    , m_part2(nullptr)
    , m_bookshelf(nullptr)
    , m_reader(nullptr)
    , m_statsPanel(nullptr)
    , m_importDialog(nullptr)
    , m_aboutDialog(nullptr)
    , m_logTimer(new QTimer(this))
    , m_resizeEdges(Qt::Edges())
    , m_resizing(false)
{
    setWindowTitle("Genesis");
    resize(1200, 760);

    // Frameless: no native title bar / border. We draw our own TitleBar and
    // handle edge resizing manually. Mouse tracking lets us show resize
    // cursors on hover (not just while a button is held).
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMouseTracking(true);

    buildLayout();
    refreshExcerpt();   // restore the stored 书摘 onto part1

    // Center the window on the primary screen's available area.
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        move(avail.center() - QPoint(width() / 2, height() / 2));
    }

    // Global event filter so we catch MouseMove even over child widgets —
    // needed to properly manage (and unset) resize cursors.
    qApp->installEventFilter(this);

    // Bank reading time while reading, so a crash costs at most this interval
    // and the statistics stay fresh without leaving the reader. The same tick
    // is the only periodic writer, keeping MainWindow the sole owner of
    // metadata/log persistence.
    m_logTimer->setInterval(5 * 60 * 1000);
    connect(m_logTimer, &QTimer::timeout, this,
            [this]() { flushReadingStats(); });
    m_logTimer->start();

    // Belt and braces for the exit path: the frameless close may be routed
    // through the title bar rather than a normal window close.
    connect(qApp, &QCoreApplication::aboutToQuit, this,
            [this]() { flushReadingStats(); });
}

void MainWindow::flushReadingStats()
{
    // Deliberately tolerant of being called when nothing is happening - the
    // reader's watermark makes a second call a no-op, which is what lets this
    // run from the timer, from leaving the reader, and from app exit alike.
    if (m_reader && !m_reader->currentBookPath().isEmpty())
        m_reader->logPendingReading();
    if (m_statsPanel)
        m_statsPanel->reload();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    flushReadingStats();
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow() = default;

QWidget* MainWindow::buildMenuBar()
{
    // Custom-drawn strip: 书架 / 更多 / 设置 / 关于 as TitleMenuItem widgets
    // that scale continuously with the window width. A real QMenuBar would
    // fold its items into an overflow kebab once the window narrows.
    QWidget* strip = new QWidget;
    strip->setAttribute(Qt::WA_StyledBackground, true);
    strip->setStyleSheet("background: transparent;");
    QHBoxLayout* stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(0, 0, 0, 0);
    stripLayout->setSpacing(0);

    auto* bookshelf = new TitleMenuItem(QString::fromUtf8("书架"), strip);
    bookshelf->setOnClick([this]() { showBookshelf(); });
    stripLayout->addWidget(bookshelf);

    // 更多 groups the secondary entries in one dropdown.
    auto* more = new TitleMenuItem(QString::fromUtf8("更多"), strip);
    QMenu* moreMenu = new QMenu(more);
    moreMenu->setStyleSheet(
        "QMenu { background-color: #FFFFFF; color: #33CCFF;"
        "  border: 1px solid #CCEEFF; }"
        "QMenu::item { padding: 6px 22px; color: #333333; }"
        "QMenu::item:selected { background: #E6F7FF; color: #33CCFF; }");
    QAction* bookmarks = moreMenu->addAction(QString::fromUtf8("书签"));
    connect(bookmarks, &QAction::triggered, this, &MainWindow::showBookmarks);
    QAction* import = moreMenu->addAction(QString::fromUtf8("导入"));
    connect(import, &QAction::triggered, this, &MainWindow::importBooks);
    QAction* excerpt = moreMenu->addAction(QString::fromUtf8("书摘"));
    connect(excerpt, &QAction::triggered, this, &MainWindow::editExcerpt);
    QAction* markdown = moreMenu->addAction(QString::fromUtf8("Markdown参考"));
    connect(markdown, &QAction::triggered, this, [this]() {
        // Markdown window: shows the syntax guide for the note editor.
        // 50% of the main window's width, spanning from just below the
        // title bar to the window's bottom edge, centered horizontally.
        // Non-modal (showStandalone); title-bar buttons stay usable.
        FramelessDialog* dialog =
            new FramelessDialog(QStringLiteral("Markdown"), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);

        // QTextBrowser: clickable external links. No scrollbar strip — the
        // wheel still scrolls.
        QTextBrowser* guide = new QTextBrowser(dialog);
        guide->setOpenExternalLinks(true);
        guide->setFrameShape(QFrame::NoFrame);
        guide->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        guide->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        guide->setStyleSheet(
            "QTextBrowser { background: transparent; color: #333333;"
            "  border: none; font-size: 13px; }");
        guide->document()->setMarkdown(
            NotesPanel::markdownGuide(),
            QTextDocument::MarkdownDialectGitHub);
        // Swap Courier New code spans to Consolas: Courier New's "*" glyph
        // renders in the upper half of the line and looks cropped.
        NotesPanel::polishGuideDocument(guide->document());
        dialog->contentLayout()->addWidget(guide, 1);

        // Pinned: follows the main window, not draggable, size tracks the
        // window live. Width = 50%; height = full window height minus the
        // title bar + top grab band. Horizontally CENTERED.
        const int barH = m_titleBar->height();
        const int titleBottom = kResizeBorder + barH;
        dialog->followParent(QPoint(0, titleBottom / 2), 0.5, 1.0,
                             titleBottom);
        // Standalone: closes any other open standalone dialog first.
        dialog->showStandalone();
        // Start reading from the very top (setMarkdown can leave the view
        // scrolled after layout).
        guide->moveCursor(QTextCursor::Start);
        guide->verticalScrollBar()->setValue(0);
    });
    more->setMenu(moreMenu);
    stripLayout->addWidget(more);

    // 设置 dropdown.
    auto* settingsItem = new TitleMenuItem(QString::fromUtf8("设置"), strip);
    QMenu* settingsMenu = new QMenu(settingsItem);
    settingsMenu->setStyleSheet(moreMenu->styleSheet());
    QAction* background = settingsMenu->addAction(QString::fromUtf8("背景"));
    connect(background, &QAction::triggered,
            this, &MainWindow::showBackgroundSettings);

    // Theme text colour: ONE entry that names the switch it will perform, the
    // same convention as the notes panel's 显示行数/关闭行数 item. Rebuilt on
    // each open so the label always reflects the current colour.
    connect(settingsMenu, &QMenu::aboutToShow, this, [this, settingsMenu]() {
        settingsMenu->clear();
        QAction* bg = settingsMenu->addAction(QString::fromUtf8("背景"));
        connect(bg, &QAction::triggered,
                this, &MainWindow::showBackgroundSettings);

        const bool white =
            Theme::instance().textColor() == Theme::TextColor::White;
        QAction* theme = settingsMenu->addAction(
            white ? QString::fromUtf8("主题文字：黑")
                  : QString::fromUtf8("主题文字：白"));
        connect(theme, &QAction::triggered, this, [this]() {
            const bool isWhite =
                Theme::instance().textColor() == Theme::TextColor::White;
            Theme::instance().setTextColor(isWhite ? Theme::TextColor::Black
                                                   : Theme::TextColor::White);
        });

        QAction* clear = settingsMenu->addAction(
            QString::fromUtf8("清除阅读记录"));
        connect(clear, &QAction::triggered, this,
                &MainWindow::clearReadingHistory);
    });

    settingsItem->setMenu(settingsMenu);
    stripLayout->addWidget(settingsItem);

    auto* aboutItem = new TitleMenuItem(QString::fromUtf8("关于"), strip);
    aboutItem->setOnClick([this]() { showAbout(); });
    stripLayout->addWidget(aboutItem);

    return strip;
}

void MainWindow::showBookshelf()
{
    // Back to the shelf page (from the reader): save the reader's accumulated
    // reading time to metadata before switching away.
    if (m_stack->currentWidget() == m_reader && !m_reader->currentBookPath().isEmpty()) {
        const QString path = m_reader->currentBookPath();
        auto meta = MetadataStore::load(path).value_or(BookInfo{});
        meta.filePath = path;
        meta.title = QFileInfo(path).completeBaseName();
        meta.lastOpened = QDateTime::currentDateTime();
        meta.readSeconds = m_reader->readSeconds();
        MetadataStore::save(meta);
        // Bank the reading time before the reader lets go of the book, so the
        // just-finished stretch shows up in the statistics below.
        m_reader->logPendingReading();
        // Release the PDF file handle so the book can be deleted from the
        // shelf while we're away from the reader.
        m_reader->closeBook();
    }

    m_stack->setCurrentIndex(0);
    m_bookshelf->refresh();
    // Reflect anything banked above (and any time accrued since the last tick).
    if (m_statsPanel)
        m_statsPanel->reload();
}

void MainWindow::showBookmarks()
{
    // Non-modal (showStandalone); title-bar buttons stay usable.
    BookmarkDialog* dialog = new BookmarkDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(dialog, &BookmarkDialog::bookmarkActivated,
            this, [this](const QString& path, int page) {
                // If the reader currently holds a DIFFERENT book, settle its
                // metadata first the same way the 书架 entry does.
                if (m_stack->currentWidget() == m_reader
                    && !m_reader->currentBookPath().isEmpty()
                    && m_reader->currentBookPath() != path)
                    showBookshelf();
                m_reader->openBookAt(path, page);
                m_stack->setCurrentWidget(m_reader);
            });
    // Pinned: centered over the main window, follows its moves/resizes,
    // not draggable. Standalone: closes any other open standalone dialog.
    dialog->followParent();
    dialog->showStandalone();
}

QFileDialog* MainWindow::ensureImportDialog()
{
    if (m_importDialog)
        return m_importDialog;
    // NATIVE picker, deliberately. The Qt-drawn one enumerates volumes itself
    // through QStorageInfo/shell APIs, and a mapped network drive that is
    // offline (a disconnected \\server\share on a drive letter) makes that
    // enumeration block for the full SMB timeout: the 查找范围 list and the
    // file list come up nearly empty, and every directory change hangs the UI
    // while it retries. The native dialog leaves volume enumeration to the
    // shell, which does it asynchronously and never blocks on a dead share.
    //
    // Note there is deliberately NO offscreen prewarming here. The trick used
    // for the app's own dialogs (show/hide under WA_DontShowOnScreen) would
    // force the real shell/COM stack to build at startup, which is precisely
    // the cost the old widget-based picker existed to avoid - and the native
    // dialog builds it faster on first show anyway.
    m_importDialog = new CenteredFileDialog(this, QString::fromUtf8("导入电子书"));
    m_importDialog->setFileMode(QFileDialog::ExistingFiles);
    m_importDialog->setNameFilter(QString::fromUtf8("PDF 电子书 (*.pdf)"));
    m_importDialog->setDirectory(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    return m_importDialog;
}

void MainWindow::prewarmDialogs()
{
    ensureImportDialog();
    ensureAboutDialog();
}

void MainWindow::importBooks()
{
    // Pick one or more local PDFs; each is copied onto the shelf by Library.
    QFileDialog* dlg = ensureImportDialog();
    if (dlg->exec() != QDialog::Accepted)
        return;
    const QStringList paths = dlg->selectedFiles();
    if (paths.isEmpty())
        return;

    const Library::ImportResult result = Library::importFiles(paths);
    if (!result.imported.isEmpty())
        m_bookshelf->refresh();

    // Summarize the outcome, mentioning duplicates/failures only when present.
    QStringList lines;
    if (!result.imported.isEmpty())
        lines << QString::fromUtf8("成功导入 %1 本书：\n%2")
                     .arg(result.imported.size())
                     .arg(result.imported.join(QStringLiteral("\n")));
    if (!result.duplicates.isEmpty())
        lines << QString::fromUtf8("已在书架中，跳过 %1 本：\n%2")
                     .arg(result.duplicates.size())
                     .arg(result.duplicates.join(QStringLiteral("\n")));
    if (!result.failed.isEmpty())
        lines << QString::fromUtf8("导入失败 %1 本：\n%2")
                     .arg(result.failed.size())
                     .arg(result.failed.join(QStringLiteral("\n")));

    FramelessDialog::information(this, QString::fromUtf8("导入结果"),
                                 lines.join(QStringLiteral("\n\n")));
}

void MainWindow::showBackgroundSettings()
{
    // Non-modal (showStandalone); title-bar buttons stay usable.
    BackgroundDialog* dialog = new BackgroundDialog(m_content, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(dialog, &QDialog::accepted,
            this, &MainWindow::saveBackgroundSettings);
    // Rejected: the dialog already rolled the live preview back.
    // Pinned: centered over the main window, follows its moves/resizes,
    // not draggable. Standalone: closes any other open standalone dialog.
    dialog->followParent();
    dialog->showStandalone();
}

void MainWindow::clearReadingHistory()
{
    // Destructive and not undoable, so always confirm first.
    if (!FramelessDialog::question(
            this, QString::fromUtf8("清除阅读记录"),
            QString::fromUtf8("将清空所有阅读时长与阅读时间记录，"
                              "书架统计和阅读趋势都会归零。\n"
                              "笔记、书签和书籍文件不受影响。\n\n"
                              "此操作不可撤销，确定继续？")))
        return;

    // Bank nothing on the way out: any time accrued in the current session
    // would immediately re-appear in the freshly cleared statistics.
    m_reader->closeBook();

    // Two stores hold reading history: the per-book totals behind the shelf
    // cards, and the session log behind the charts. Clear both.
    MetadataStore::clearReadingHistory();
    ReadingLogStore::clearAll();

    m_bookshelf->refresh();      // cards fall back to "未读" / no timestamp
    if (m_statsPanel)
        m_statsPanel->reload();  // charts empty out
}

void MainWindow::editExcerpt()
{
    const Excerpt current = ExcerptStore::load();
    ExcerptDialog dialog(current.text, current.color, current.hAlign,
                         current.vAlign, this);
    dialog.followParent();
    dialog.showStandalone();

    if (dialog.exec() != QDialog::Accepted)
        return;
    Excerpt edited;
    edited.text = dialog.text();
    edited.color = dialog.color();
    edited.hAlign = dialog.hAlign();
    edited.vAlign = dialog.vAlign();
    // An emptied excerpt removes the entry rather than storing a blank.
    if (edited.text.isEmpty())
        ExcerptStore::clear();
    else
        ExcerptStore::save(edited);
    refreshExcerpt();
}

void MainWindow::refreshExcerpt()
{
    if (!m_excerptView)
        return;
    m_excerptView->setExcerpt(ExcerptStore::load());
}

void MainWindow::loadBackgroundSettings()
{
    QSettings settings(QStringLiteral("Genesis"), QStringLiteral("Genesis"));
    m_content->setImagePath(
        settings.value(QStringLiteral("background/image")).toString());
    m_content->setImageOpacity(
        settings.value(QStringLiteral("background/opacity"), 1.0).toReal());
}

void MainWindow::saveBackgroundSettings()
{
    QSettings settings(QStringLiteral("Genesis"), QStringLiteral("Genesis"));
    settings.setValue(QStringLiteral("background/image"),
                      m_content->imagePath());
    settings.setValue(QStringLiteral("background/opacity"),
                      m_content->imageOpacity());
}

FramelessDialog* MainWindow::ensureAboutDialog()
{
    if (m_aboutDialog)
        return m_aboutDialog;

    // Built once (at startup idle) and reused: the first FramelessDialog
    // pays stylesheet-engine setup plus the translucent-window composition
    // path, which made the first 关于 click feel slow.
    // Close-only chrome: no title bar text/separator, just the close button.
    m_aboutDialog = new FramelessDialog(QString::fromUtf8("关于 Genesis"),
                                        FramelessDialog::Chrome::CloseOnly,
                                        this);
    m_aboutDialog->setAttribute(Qt::WA_DeleteOnClose, false);
    FramelessDialog& dialog = *m_aboutDialog;
    QVBoxLayout* layout = dialog.contentLayout();
    layout->setContentsMargins(24, 0, 24, 16);
    // One uniform gap between every row (no extra addSpacing anywhere).
    layout->setSpacing(8);

    // --- app icon, centered on top ---
    QLabel* icon = new QLabel(&dialog);
    icon->setPixmap(windowIcon().pixmap(80, 80));
    icon->setAlignment(Qt::AlignHCenter);
    layout->addWidget(icon, 0, Qt::AlignHCenter);

    // --- name / version / developer / email, top to bottom ---
    QLabel* name = new QLabel(QString::fromUtf8("Genesis（发轫）"), &dialog);
    name->setAlignment(Qt::AlignHCenter);
    name->setStyleSheet("color: #33CCFF; font-size: 18px; font-weight: bold;");
    layout->addWidget(name);

    // Kept in step with the project() version in CMakeLists.txt, which is the
    // release number; the two are shown/produced from different places but must
    // agree, so bump both together.
    QLabel* version = new QLabel(QStringLiteral("v0.1.0"), &dialog);
    version->setAlignment(Qt::AlignHCenter);
    version->setStyleSheet("color: #888888; font-size: 12px;");
    layout->addWidget(version);

    QLabel* dev = new QLabel(QString::fromUtf8("作者：Spring"), &dialog);
    dev->setAlignment(Qt::AlignHCenter);
    dev->setStyleSheet("color: #333333; font-size: 13px;");
    layout->addWidget(dev);

    QLabel* mail = new QLabel(QString::fromUtf8(
        "反馈邮箱：<a style='color:#33CCFF;' "
        "href='mailto:xspringg@163.com'>xspringg@163.com</a>"), &dialog);
    mail->setAlignment(Qt::AlignHCenter);
    mail->setTextFormat(Qt::RichText);
    mail->setOpenExternalLinks(true);
    mail->setStyleSheet("color: #333333; font-size: 13px;");
    layout->addWidget(mail);

    QLabel* motto = new QLabel(QString::fromUtf8("独钓寒江雪"), &dialog);
    motto->setAlignment(Qt::AlignHCenter);
    motto->setStyleSheet(
        "color: #000000; font-size: 14px;"
        "font-family: 'KaiTi', 'STKaiti', '楷体', serif;");
    layout->addWidget(motto);

    // --- bottom row: copyright left, project link right ---
    QHBoxLayout* bottom = new QHBoxLayout;
    bottom->setSpacing(12);

    QLabel* copyright = new QLabel(QString::fromUtf8(
        "Copyright © 2026 Spring. All rights reserved.<br>"
        "本软件由作者独立编写，所有权保留。"), &dialog);
    copyright->setTextFormat(Qt::RichText);
    copyright->setStyleSheet("color: #999999; font-size: 11px;");
    bottom->addWidget(copyright, 0, Qt::AlignLeft | Qt::AlignBottom);

    bottom->addStretch();

    QLabel* project = new QLabel(QString::fromUtf8(
        "项目地址：<a style='color:#33CCFF;' "
        "href='https://github.com/Acetochlor/Genesis'>GitHub</a>"), &dialog);
    project->setTextFormat(Qt::RichText);
    project->setOpenExternalLinks(true);
    project->setStyleSheet("color: #333333; font-size: 12px;");
    bottom->addWidget(project, 0, Qt::AlignRight | Qt::AlignBottom);

    layout->addLayout(bottom);

    // About keeps its ORIGINAL fixed proportions — it is the one dialog
    // that does not scale with the window.
    dialog.setFixedWidth(400);
    // Realize the platform window now (off-screen) so exec() later has
    // nothing left to build.
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.show();
    dialog.hide();
    dialog.setAttribute(Qt::WA_DontShowOnScreen, false);
    return m_aboutDialog;
}

void MainWindow::showAbout()
{
    // Non-modal; pinned & centered over the main window, not draggable.
    // Standalone: closes any other open standalone dialog.
    FramelessDialog* dlg = ensureAboutDialog();
    dlg->followParent();
    dlg->showStandalone();
}

void MainWindow::buildLayout()
{
    // Central panel: paints the user-chosen background image (if any) over a
    // white base — the three content parts render on top of it.
    m_content = new BackgroundWidget;
    loadBackgroundSettings();

    // The content hosts a two-page stack: the shelf page (part1/part2 left,
    // bookshelf right) and the reader page (PDF left 75%, notes right 25%).
    QVBoxLayout* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    m_stack = new QStackedWidget(m_content);
    // Keep pages transparent so the background image shows through the stack.
    m_stack->setStyleSheet("QStackedWidget { background: transparent; }");
    contentLayout->addWidget(m_stack);

    // --- page 0, the shelf: left column 25% (part1 over part2), right 75% ---
    // Parts 1/2 are transparent placeholders for now; part3 is the bookshelf.
    QWidget* shelfPage = new QWidget;
    QHBoxLayout* shelfLayout = new QHBoxLayout(shelfPage);
    shelfLayout->setContentsMargins(0, 0, 0, 0);
    shelfLayout->setSpacing(0);

    QVBoxLayout* leftColumn = new QVBoxLayout;
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(0);

    m_part1 = new QWidget(shelfPage);
    m_part1->setObjectName("genesisPart1");
    m_part1->setStyleSheet(
        "#genesisPart1 { background: transparent;"
        "  border-right: 1px solid rgba(204, 238, 255, 180);"
        "  border-bottom: 1px solid rgba(204, 238, 255, 180); }");
    m_part1->setAttribute(Qt::WA_StyledBackground, true);
    {
        // The book excerpt fills this cell. It sizes its own font to whatever
        // the cell currently is, in the same calligraphic face the about box
        // uses for 独钓寒江雪, so the two read as one voice.
        QVBoxLayout* part1Layout = new QVBoxLayout(m_part1);
        part1Layout->setContentsMargins(12, 8, 12, 8);
        m_excerptView = new ExcerptView(m_part1);
        part1Layout->addWidget(m_excerptView, 1);
    }
    leftColumn->addWidget(m_part1, 1);   // top quarter (1 : 3 against part2)

    m_part2 = new QWidget(shelfPage);
    m_part2->setObjectName("genesisPart2");
    m_part2->setStyleSheet(
        "#genesisPart2 { background: transparent;"
        "  border-right: 1px solid rgba(204, 238, 255, 180); }");
    m_part2->setAttribute(Qt::WA_StyledBackground, true);
    {
        // Reading statistics fill this cell. Kept in a zero-margin layout so
        // the panel spans the whole area; the panel itself is transparent and
        // paints only its charts and tiles.
        QVBoxLayout* part2Layout = new QVBoxLayout(m_part2);
        part2Layout->setContentsMargins(0, 0, 0, 0);
        part2Layout->setSpacing(0);
        m_statsPanel = new ReadingStatsPanel(m_part2);
        part2Layout->addWidget(m_statsPanel);
    }
    leftColumn->addWidget(m_part2, 3);   // bottom three quarters

    shelfLayout->addLayout(leftColumn, 1);   // 25%

    m_bookshelf = new BookshelfView(shelfPage);
    shelfLayout->addWidget(m_bookshelf, 3);  // 75%
    m_stack->addWidget(shelfPage);

    // --- page 1, the reader: opened by double-clicking a book; the 书架
    // menu-bar entry brings the shelf page back ---
    m_reader = new ReaderView;
    m_stack->addWidget(m_reader);
    connect(m_bookshelf, &BookshelfView::bookActivated,
            this, [this](const QString& path) {
                m_reader->openBook(path);
                m_stack->setCurrentWidget(m_reader);
            });

    // Self-drawn title bar spans the top: app icon + menu bar on the left,
    // window-control buttons on the right. The content area fills the rest.
    // A uniform border margin around the content is left as the window's own
    // area so edge/corner resizing has a grab zone the children don't consume.
    m_titleBar = new TitleBar(this);
    m_titleBar->setMenuBar(buildMenuBar());

    QWidget* container = new QWidget;
    container->setObjectName("genesisContainer");
    container->setStyleSheet("#genesisContainer { background-color: #FFFFFF; }");
    // The resize grab band lives in this container's margin, so it (not the
    // window) receives those mouse events: watch them via an event filter.
    container->setMouseTracking(true);
    container->installEventFilter(this);
    QVBoxLayout* rootLayout = new QVBoxLayout(container);
    rootLayout->setContentsMargins(kResizeBorder, kResizeBorder,
                                   kResizeBorder, kResizeBorder);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(m_titleBar);
    rootLayout->addWidget(m_content, 1);

    setCentralWidget(container);
}

Qt::Edges MainWindow::edgesAt(const QPoint& pos) const
{
    Qt::Edges edges;
    const int b = kResizeBorder;
    if (pos.x() <= b)                 edges |= Qt::LeftEdge;
    if (pos.x() >= width() - b)       edges |= Qt::RightEdge;
    if (pos.y() <= b)                 edges |= Qt::TopEdge;
    if (pos.y() >= height() - b)      edges |= Qt::BottomEdge;
    return edges;
}

Qt::CursorShape MainWindow::cursorForEdges(Qt::Edges edges) const
{
    if ((edges & Qt::LeftEdge && edges & Qt::TopEdge) ||
        (edges & Qt::RightEdge && edges & Qt::BottomEdge))
        return Qt::SizeFDiagCursor;
    if ((edges & Qt::RightEdge && edges & Qt::TopEdge) ||
        (edges & Qt::LeftEdge && edges & Qt::BottomEdge))
        return Qt::SizeBDiagCursor;
    if (edges & (Qt::LeftEdge | Qt::RightEdge))
        return Qt::SizeHorCursor;
    if (edges & (Qt::TopEdge | Qt::BottomEdge))
        return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Only process mouse events on widgets that belong to this window.
    // Without this guard the global filter would fire for every widget in
    // every process dialog, file picker, or FramelessDialog:
    if (!isAncestorOf(qobject_cast<QWidget*>(watched)) && watched != this)
        return QMainWindow::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(event);

        // --- resize-in-progress: track geometry changes ---
        if (m_resizing) {
            // If the left button was released (child widget swallowed it),
            // stop immediately.
            if (!(me->buttons() & Qt::LeftButton)) {
                m_resizing = false;
                m_resizeEdges = Qt::Edges();
                QApplication::restoreOverrideCursor();
                break;
            }
            const QPoint delta = me->globalPosition().toPoint() - m_resizeStartGlobal;
            QRect g = m_resizeStartGeom;
            const QSize minS = minimumSizeHint().expandedTo(minimumSize())
                                   .expandedTo(QSize(400, 300));
            if (m_resizeEdges & Qt::LeftEdge)
                g.setLeft(qMin(g.left() + delta.x(), g.right() - minS.width()));
            if (m_resizeEdges & Qt::RightEdge)
                g.setRight(qMax(g.right() + delta.x(), g.left() + minS.width()));
            if (m_resizeEdges & Qt::TopEdge)
                g.setTop(qMin(g.top() + delta.y(), g.bottom() - minS.height()));
            if (m_resizeEdges & Qt::BottomEdge)
                g.setBottom(qMax(g.bottom() + delta.y(), g.top() + minS.height()));
            setGeometry(g);
            return true;
        }

        // --- hover in the resize band: manage cursor via application stack ---
        // NEVER call setCursor() on MainWindow: that propagates to every child
        // widget that doesn't set its own cursor, polluting the entire window.
        if (!isMaximized() && !isFullScreen()) {
            // Map the global position into window coordinates (the container's
            // root layout margin is the resize band — same coordinate space as
            // the window itself).
            const QPoint windowPos = mapFromGlobal(me->globalPosition().toPoint());
            const Qt::Edges edges = edgesAt(windowPos);
            const Qt::CursorShape shape = cursorForEdges(edges);
            // Only push/restore when the cursor actually changes, to prevent
            // QApplication's cursor stack from growing unbounded.
            if (shape != Qt::ArrowCursor) {
                // Set the resize cursor via application stack — this only
                // affects the cursor shape globally without touching any
                // widget's per-widget cursor style (which would cascade).
                if (!QApplication::overrideCursor()
                    || QApplication::overrideCursor()->shape() != shape) {
                    // Restore any previous override first (e.g. from resize
                    // state) before pushing the new one.
                    while (QApplication::overrideCursor())
                        QApplication::restoreOverrideCursor();
                    QApplication::setOverrideCursor(shape);
                }
            } else {
                // Not on a resize edge: clear any resize cursor override.
                while (QApplication::overrideCursor())
                    QApplication::restoreOverrideCursor();
            }
        } else {
            // Maximized or fullscreen: no resize band.
            while (QApplication::overrideCursor())
                QApplication::restoreOverrideCursor();
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint windowPos = mapFromGlobal(me->globalPosition().toPoint());
        if (me->button() == Qt::LeftButton
            && !isMaximized() && !isFullScreen()
            && edgesAt(windowPos)) {
            m_resizing = true;
            m_resizeEdges = edgesAt(windowPos);
            m_resizeStartGeom = geometry();
            m_resizeStartGlobal = me->globalPosition().toPoint();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        if (m_resizing && (static_cast<QMouseEvent*>(event)->button()
                           == Qt::LeftButton)) {
            m_resizing = false;
            m_resizeEdges = Qt::Edges();
            while (QApplication::overrideCursor())
                QApplication::restoreOverrideCursor();
            return true;
        }
        break;
    }
    case QEvent::Leave: {
        // The pointer left the container's widget area: clear the resize
        // cursor so children (and the content area) show a normal arrow.
        if (!m_resizing) {
            while (QApplication::overrideCursor())
                QApplication::restoreOverrideCursor();
        }
        break;
    }
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        // Fullscreen/maximized/restored transitions can leave a stale resize
        // cursor. Always reset: the next MouseMove will set the correct one.
        m_resizing = false;
        m_resizeEdges = Qt::Edges();
        while (QApplication::overrideCursor())
            QApplication::restoreOverrideCursor();
        if (m_titleBar)
            m_titleBar->updateMaximizeButton(isMaximized());
    }
    QMainWindow::changeEvent(event);
}

} // namespace Genesis
