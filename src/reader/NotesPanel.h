#ifndef NOTESPANEL_H
#define NOTESPANEL_H

#include <QStringList>
#include <QWidget>

class QPlainTextEdit;
class QTextBrowser;
class QTextDocument;
class QTimer;
class QImage;
class QLineEdit;
class QLabel;

namespace Genesis {

class IconButton;

// The reader's note panel (right 40% column). It embeds the standalone
// Markdown editor's source-editing surface only; rendering is shown on demand
// in a separate preview drawer supplied by ReaderView. Notes persist per book
// as .md files and autosave after a short idle pause and on book switch/close.
class NotesPanel : public QWidget {
    Q_OBJECT

public:
    explicit NotesPanel(QWidget* parent = nullptr);

    void setBook(const QString& bookPath);
    void save();
    QString markdownText() const;
    // 0-based index of the source line currently at the top of the note
    // editor, so the rendered preview can open at the same place.
    int topVisibleLine() const;

    // The Markdown syntax guide (shared with the 更多 → Markdown dialog).
    static QString markdownGuide();

    // Post-process a document produced by setMarkdown(): swap code-span
    // fonts to Consolas. Qt assigns the system fixed font (Courier New on
    // Windows), whose "*" glyph sits in the upper half of the line — with
    // Consolas it is vertically centered.
    static void polishGuideDocument(QTextDocument* doc);

signals:
    void markdownRenderRequested();
    void markdownTextChanged(const QString& markdown);

protected:
    void resizeEvent(QResizeEvent* event) override;
    // Esc in the find field closes the find bar.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QString notesFilePath(const QString& bookPath) const;
    void setBarVisible(bool visible);
    void showKebabMenu();
    // Show/hide the editor's line-number gutter and remember the choice.
    void setLineNumbers(bool on);
    // Find bar (revealed by the magnifier button in the control bar).
    void setFindBarVisible(bool visible);
    // Copy the note to the clipboard with every locally-pasted picture
    // inlined as a data: URI, so it renders on a site that cannot read this
    // machine's files (Yuque and friends). Returns the picture count.
    int copyAsPortableMarkdown();
    // Re-run the current query: highlight every match and jump to the one
    // after (or before) the caret, wrapping around the document.
    void updateFindHighlights(bool forward);
    void clearFindHighlights();
    void stepFind(bool forward);
    // Persist a pasted image next to the note and return the Markdown
    // reference to insert at the cursor ("" = nothing to insert).
    QString pasteImageAsMarkdown(const QImage& image);
    // Same for pasted/dropped local picture FILES: copy each beside the note
    // and return the Markdown block for all of them ("" = nothing to insert).
    QString pasteImageFilesAsMarkdown(const QStringList& imagePaths);

    QWidget* m_bar;            // hideable control bar (kebab right)
    IconButton* m_kebab;
    QWidget* m_reveal;         // floating chevron shown when bar is hidden
    QPlainTextEdit* m_editor;
    QTimer* m_saveTimer;       // debounce: save 1s after the last keystroke
    QString m_bookPath;
    bool m_loading;
    bool m_lineNumbers;        // persisted in QSettings under notes/lineNumbers
    QWidget* m_findBar;        // search row, shown under the control bar
    QLineEdit* m_findEdit;
    QLabel* m_findCount;       // "n/m" match readout
    QList<int> m_findHits;     // match anchors, document order
};

} // namespace Genesis

#endif // NOTESPANEL_H
