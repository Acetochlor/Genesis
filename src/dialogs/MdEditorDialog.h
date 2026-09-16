#ifndef MDEDITORDIALOG_H
#define MDEDITORDIALOG_H

#include <QStringList>
#include <functional>

class QPlainTextEdit;
class QString;
class QTextBrowser;
class QWidget;
class QImage;

namespace Genesis {

// Shared Markdown source/preview pieces used by the reader's notes panel.
//
// imagePasteHandler     (optional): real pasted pixels (screenshot Ctrl+V, or
//   the context menu). Receives the QImage; returns the Markdown to insert; an
//   empty return leaves the editor unchanged.
// imageFilePasteHandler (optional): pasted/dropped local picture FILES (a file
//   copied in the file manager or a drag into the editor), delivered as the
//   resolved list of local image paths. Otherwise such a paste would collapse
//   to a bare "file:///…" text that never renders as a picture.
QPlainTextEdit* createMarkdownSourceEditor(
    QWidget* parent = nullptr,
    std::function<QString(const QImage&)> imagePasteHandler = {},
    std::function<QString(const QStringList&)> imageFilePasteHandler = {});
QTextBrowser* createMarkdownPreview(QWidget* parent = nullptr);
void renderMarkdownPreview(QTextBrowser* preview, const QString& markdown);

// Show/hide a left line-number gutter on an editor returned by
// createMarkdownSourceEditor(). No-op on any other QPlainTextEdit.
void setMarkdownEditorLineNumbers(QPlainTextEdit* editor, bool visible);

// 0-based index of the first line currently visible at the top of an editor
// returned by createMarkdownSourceEditor(); 0 on any other QPlainTextEdit.
// Used to open the rendered preview at the same place the source is scrolled.
int markdownEditorTopLine(QPlainTextEdit* editor);

// Scroll a preview created by createMarkdownPreview() so the given SOURCE line
// (0-based, as returned by markdownEditorTopLine) sits at the top. Returns
// false when the line cannot be located.
bool scrollMarkdownPreviewToSourceLine(QTextBrowser* preview, int sourceLine);

} // namespace Genesis

#endif // MDEDITORDIALOG_H
