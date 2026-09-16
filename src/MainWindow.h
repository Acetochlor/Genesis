#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QStackedWidget;
class QFileDialog;
class QLabel;

namespace Genesis {

class TitleBar;
class BackgroundWidget;
class BookshelfView;
class ReaderView;
class ExcerptView;
class FramelessDialog;
class TitleMenuItem;
class ReadingStatsPanel;

// The main application window: frameless (Qt::FramelessWindowHint) with a
// self-drawn TitleBar and manual edge/corner resizing. The central area is an
// empty white panel — fill it in for the new project.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Build the 导入/关于 dialogs ahead of time (called at startup idle) so
    // their first click shows an already-constructed window.
    void prewarmDialogs();

protected:
    // Manual edge/corner resize for the frameless window.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    // Flush the reader's pending reading time on the way out.
    void closeEvent(QCloseEvent* event) override;

private:
    void buildLayout();
    // Push the reader's accumulated reading time into ReadingLogStore and
    // refresh the statistics panel. Safe to call at any time: it does nothing
    // when no book is open or nothing new has accrued.
    void flushReadingStats();

    // Build the standalone menu strip widget embedded in the title bar
    // (custom-drawn items so they scale with the window — no QMenuBar
    // overflow kebab when it gets narrow).
    QWidget* buildMenuBar();

    // Top-bar entry handlers.
    void showBookshelf();
    void showBookmarks();
    void importBooks();
    void showAbout();

    // Lazily-built, reused dialogs (see prewarmDialogs).
    QFileDialog* ensureImportDialog();
    FramelessDialog* ensureAboutDialog();

    // Settings → 背景: image + opacity dialog, persisted via QSettings.
    void showBackgroundSettings();
    // Settings → 清除阅读记录: wipe all recorded reading time and dates.
    void clearReadingHistory();
    // 更多 → 书摘: add/edit the app's single excerpt, then show it on part1.
    void editExcerpt();
    // Repaint the part1 excerpt from the stored excerpt.
    void refreshExcerpt();
    void loadBackgroundSettings();
    void saveBackgroundSettings();

    // ---- frameless resize ---------------------------------------------------
    // Which window edges the point (window coords) is on, as Qt::Edges flags.
    Qt::Edges edgesAt(const QPoint& pos) const;
    // Resize cursor for a given edge combination (never calls setCursor itself;
    // eventFilter uses QApplication::setOverrideCursor instead).
    Qt::CursorShape cursorForEdges(Qt::Edges edges) const;

    TitleBar* m_titleBar;
    BackgroundWidget* m_content;     // central area with the user background
    QStackedWidget* m_stack;         // page 0: shelf layout; page 1: reader
    QWidget* m_part1;                // top-left panel (25% x 25%)
    QWidget* m_part2;                // bottom-left panel (25% x 75%)
    ExcerptView* m_excerptView;      // the 书摘 shown on part1
    BookshelfView* m_bookshelf;      // part3: right-side bookshelf (75%)
    ReaderView* m_reader;            // PDF reading page
    ReadingStatsPanel* m_statsPanel; // reading statistics, fills m_part2
    QFileDialog* m_importDialog;     // pre-built 导入 file picker (reused)
    FramelessDialog* m_aboutDialog;  // pre-built 关于 dialog (reused)
    // Flushes the reader's pending reading time into the log and refreshes the
    // statistics. Runs on a timer while reading so a crash loses at most one
    // interval, and again on exit.
    QTimer* m_logTimer;

    Qt::Edges m_resizeEdges;         // edges grabbed for the active resize
    bool m_resizing;                 // true while dragging an edge/corner
    QRect m_resizeStartGeom;         // window geometry at resize start
    QPoint m_resizeStartGlobal;      // cursor position at resize start
};

} // namespace Genesis

#endif // MAINWINDOW_H
