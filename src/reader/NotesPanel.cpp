#include "NotesPanel.h"
#include "../bookshelf/IconButton.h"
#include "../dialogs/MdEditorDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QDateTime>
#include <QCursor>
#include <QImage>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QToolTip>
#include <QUrl>
#include <QTextFormat>
#include <QTextList>
#include <QTextListFormat>
#include <QTimer>

namespace Genesis {

// ============================== NotesPanel ==============================

NotesPanel::NotesPanel(QWidget* parent)
    : QWidget(parent)
    , m_bar(nullptr)
    , m_kebab(nullptr)
    , m_reveal(nullptr)
    , m_editor(nullptr)
    , m_saveTimer(new QTimer(this))
    , m_loading(false)
    , m_lineNumbers(false)
    , m_findBar(nullptr)
    , m_findEdit(nullptr)
    , m_findCount(nullptr)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: transparent;");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- hideable control bar: kebab pinned right (chevron to hide) ---
    m_bar = new QWidget(this);
    m_bar->setFixedHeight(32);
    m_bar->setAttribute(Qt::WA_StyledBackground, true);
    m_bar->setStyleSheet(
        "background: #FFFFFF; border-bottom: 1px solid #E0E6ED;");
    QHBoxLayout* barLayout = new QHBoxLayout(m_bar);
    barLayout->setContentsMargins(6, 0, 6, 0);
    barLayout->setSpacing(4);
    barLayout->addStretch();

    IconButton* hide = new IconButton(IconButton::Glyph::ChevronUp, m_bar);
    hide->setToolTip(QString::fromUtf8("隐藏控制栏"));
    connect(hide, &IconButton::clicked,
            this, [this]() { setBarVisible(false); });
    barLayout->addWidget(hide);

    IconButton* find = new IconButton(IconButton::Glyph::Search, m_bar);
    find->setToolTip(QString::fromUtf8("搜索"));
    connect(find, &IconButton::clicked, this, [this]() {
        // Toggle: a second click on the magnifier puts the bar away.
        setFindBarVisible(!m_findBar->isVisible());
    });
    barLayout->addWidget(find);

    m_kebab = new IconButton(IconButton::Glyph::Kebab, m_bar);
    m_kebab->setToolTip(QString::fromUtf8("更多"));
    connect(m_kebab, &IconButton::clicked,
            this, &NotesPanel::showKebabMenu);
    barLayout->addWidget(m_kebab);

    layout->addWidget(m_bar);

    // --- find bar: revealed under the control bar by the magnifier ---
    m_findBar = new QWidget(this);
    m_findBar->setAttribute(Qt::WA_StyledBackground, true);
    m_findBar->setStyleSheet(
        "background: #F6F8FA; border-bottom: 1px solid #E0E6ED;");
    {
        QHBoxLayout* fl = new QHBoxLayout(m_findBar);
        fl->setContentsMargins(6, 4, 6, 4);
        fl->setSpacing(4);

        m_findEdit = new QLineEdit(m_findBar);
        m_findEdit->setPlaceholderText(QString::fromUtf8("查找…"));
        m_findEdit->setStyleSheet(
            "QLineEdit { background: #FFFFFF; color: #333333;"
            "  border: 1px solid #D0D7DE; border-radius: 4px;"
            "  padding: 2px 6px; font-size: 13px; }"
            "QLineEdit:focus { border-color: #33CCFF; }");
        connect(m_findEdit, &QLineEdit::textChanged, this,
                [this]() { updateFindHighlights(true); });
        connect(m_findEdit, &QLineEdit::returnPressed, this,
                [this]() { stepFind(true); });
        fl->addWidget(m_findEdit, 1);

        m_findCount = new QLabel(m_findBar);
        m_findCount->setStyleSheet(
            "QLabel { color: #8A94A0; font-size: 12px; }");
        m_findCount->setMinimumWidth(40);
        m_findCount->setAlignment(Qt::AlignCenter);
        fl->addWidget(m_findCount);

        IconButton* prev = new IconButton(IconButton::Glyph::ChevronUp,
                                          m_findBar);
        prev->setFixedSize(22, 22);
        prev->setToolTip(QString::fromUtf8("上一处"));
        connect(prev, &IconButton::clicked,
                this, [this]() { stepFind(false); });
        fl->addWidget(prev);

        IconButton* next = new IconButton(IconButton::Glyph::ChevronDown,
                                          m_findBar);
        next->setFixedSize(22, 22);
        next->setToolTip(QString::fromUtf8("下一处"));
        connect(next, &IconButton::clicked,
                this, [this]() { stepFind(true); });
        fl->addWidget(next);
    }
    m_findBar->hide();
    layout->addWidget(m_findBar);

    // Esc in the find field closes the bar.
    m_findEdit->installEventFilter(this);

    // --- Markdown source editor fills the rest (no in-place rendering) ---
    m_editor = createMarkdownSourceEditor(
        this,
        [this](const QImage& image) { return pasteImageAsMarkdown(image); },
        [this](const QStringList& paths) {
            return pasteImageFilesAsMarkdown(paths);
        });
    m_editor->setPlaceholderText(QString::fromUtf8("输入 Markdown 笔记…"));
    // Keep wheel/keyboard scrolling, but remove the visible side strips.
    m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setEnabled(false);
    layout->addWidget(m_editor, 1);

    // Restore the line-number preference. Applied programmatically so it does
    // not echo back into QSettings (the read already reflects what is stored).
    m_lineNumbers = QSettings(QStringLiteral("Genesis"), QStringLiteral("Genesis"))
                        .value(QStringLiteral("notes/lineNumbers"), false)
                        .toBool();
    Genesis::setMarkdownEditorLineNumbers(m_editor, m_lineNumbers);

    // --- floating reveal strip, shown when the bar is hidden ---
    m_reveal = new QWidget(this);
    m_reveal->setFixedSize(44, 18);
    m_reveal->setAttribute(Qt::WA_StyledBackground, true);
    m_reveal->setStyleSheet(
        "background: rgba(255, 255, 255, 220);"
        "border: 1px solid #CCEEFF; border-top: none;"
        "border-bottom-left-radius: 6px; border-bottom-right-radius: 6px;");
    {
        QHBoxLayout* rl = new QHBoxLayout(m_reveal);
        rl->setContentsMargins(0, 0, 0, 0);
        IconButton* reveal =
            new IconButton(IconButton::Glyph::ChevronDown, m_reveal);
        reveal->setFixedSize(42, 16);
        reveal->setToolTip(QString::fromUtf8("显示控制栏"));
        connect(reveal, &IconButton::clicked,
                this, [this]() { setBarVisible(true); });
        rl->addWidget(reveal);
    }
    m_reveal->hide();

    // Debounced autosave.
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1000);
    connect(m_saveTimer, &QTimer::timeout, this, &NotesPanel::save);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_loading)
            m_saveTimer->start();
        emit markdownTextChanged(m_editor->toPlainText());
    });
}

void NotesPanel::setBarVisible(bool visible)
{
    m_bar->setVisible(visible);
    m_reveal->setVisible(!visible);
    if (!visible) {
        m_reveal->move((width() - m_reveal->width()) / 2, 0);
        m_reveal->raise();
    }
}

void NotesPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_reveal->isVisible())
        m_reveal->move((width() - m_reveal->width()) / 2, 0);
}

void NotesPanel::showKebabMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #FFFFFF; color: #333333;"
        "  border: 1px solid #CCEEFF; }"
        "QMenu::item { padding: 6px 26px; }"
        "QMenu::item:selected { background: #E6F7FF; color: #33CCFF; }");

    QAction* render = menu.addAction(QString::fromUtf8("Markdown渲染"));
    connect(render, &QAction::triggered,
            this, &NotesPanel::markdownRenderRequested);

    // Line-number toggle. The label names the action available, so it reads
    // "显示行数" while the gutter is off and "关闭行数" while it is on; the menu
    // is rebuilt on each open, so it always reflects the current state.
    QAction* lines = menu.addAction(
        QString::fromUtf8(m_lineNumbers ? "关闭行数" : "显示行数"));
    connect(lines, &QAction::triggered,
            this, [this]() { setLineNumbers(!m_lineNumbers); });

    // Since the note's own images point at files on this machine, copying the
    // Markdown verbatim gives a site like Yuque nothing to render. This copies
    // the same note with every local picture inlined as a data: URI.
    QAction* port = menu.addAction(
        QString::fromUtf8("复制为可移植 Markdown"));
    connect(port, &QAction::triggered, this, [this]() {
        const int n = copyAsPortableMarkdown();
        QToolTip::showText(QCursor::pos(),
                           n > 0
                               ? QString::fromUtf8("已复制，内嵌 %1 张图片")
                                     .arg(n)
                               : QString::fromUtf8("已复制（无本地图片需内嵌）"),
                           m_kebab);
    });

    menu.exec(m_kebab->mapToGlobal(
        QPoint(m_kebab->width() - menu.sizeHint().width(),
               m_kebab->height() + 2)));
}

void NotesPanel::setLineNumbers(bool on)
{
    m_lineNumbers = on;
    Genesis::setMarkdownEditorLineNumbers(m_editor, on);
    QSettings(QStringLiteral("Genesis"), QStringLiteral("Genesis"))
        .setValue(QStringLiteral("notes/lineNumbers"), on);
}

void NotesPanel::setFindBarVisible(bool visible)
{
    m_findBar->setVisible(visible);
    if (!visible) {
        clearFindHighlights();
        // Hand focus back to the note so typing continues where it left off.
        m_editor->setFocus();
        return;
    }
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    updateFindHighlights(true);
}

bool NotesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_findEdit && event->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            setFindBarVisible(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NotesPanel::clearFindHighlights()
{
    m_findHits.clear();
    m_findCount->clear();
    // An empty extra-selection list clears every highlight in one go.
    if (auto* pte = qobject_cast<QPlainTextEdit*>(m_editor))
        pte->setExtraSelections({});
}

void NotesPanel::updateFindHighlights(bool forward)
{
    if (!m_findBar->isVisible())
        return;
    const QString needle = m_findEdit->text();
    clearFindHighlights();
    if (needle.isEmpty())
        return;

    // Collect every match in document order. Match case-insensitively so a
    // search for a word finds it mid-sentence too.
    const QString hay = m_editor->toPlainText();
    QList<QTextEdit::ExtraSelection> sel;
    m_findHits.clear();
    for (int at = hay.indexOf(needle, 0, Qt::CaseInsensitive); at >= 0;
         at = hay.indexOf(needle, at + needle.size(), Qt::CaseInsensitive)) {
        m_findHits.append(at);
    }
    if (m_findHits.isEmpty()) {
        m_findCount->setText(QString::fromUtf8("0/0"));
        if (auto* pte = qobject_cast<QPlainTextEdit*>(m_editor))
            pte->setExtraSelections({});
        return;
    }

    // Pick the current match: the first one at or after the caret when going
    // forward, else the last one before it; both wrap around the document.
    const int caret = m_editor->textCursor().position();
    int current = 0;
    if (forward) {
        current = 0;
        for (int i = 0; i < m_findHits.size(); ++i)
            if (m_findHits[i] >= caret) {
                current = i;
                break;
            }
    } else {
        current = m_findHits.size() - 1;
        for (int i = m_findHits.size() - 1; i >= 0; --i)
            if (m_findHits[i] <= caret) {
                current = i;
                break;
            }
    }

    // Every hit gets a yellow band; the current one a stronger colour so it is
    // obvious which match the arrows will step from.
    for (int i = 0; i < m_findHits.size(); ++i) {
        QTextEdit::ExtraSelection s;
        s.cursor = m_editor->textCursor();
        s.cursor.setPosition(m_findHits[i]);
        s.cursor.setPosition(m_findHits[i] + needle.size(),
                             QTextCursor::KeepAnchor);
        s.format.setBackground(i == current ? QColor("#FFD666")
                                            : QColor("#FFF3BF"));
        sel.append(s);
    }
    if (auto* pte = qobject_cast<QPlainTextEdit*>(m_editor))
        pte->setExtraSelections(sel);

    // Reveal the current hit and put the caret on it, so the next step is
    // relative to where the user is looking.
    QTextCursor c = m_editor->textCursor();
    c.setPosition(m_findHits[current]);
    c.setPosition(m_findHits[current] + needle.size(),
                  QTextCursor::KeepAnchor);
    m_editor->setTextCursor(c);
    m_editor->ensureCursorVisible();

    m_findCount->setText(QStringLiteral("%1/%2")
                             .arg(current + 1)
                             .arg(m_findHits.size()));
}

void NotesPanel::stepFind(bool forward)
{
    if (!m_findBar->isVisible() || m_findHits.isEmpty())
        return;
    if (m_findEdit->text().isEmpty())
        return;
    // Move the anchor to just outside the current match so
    // updateFindHighlights() lands on the neighbouring hit. The caret sits at
    // the END of the current selection, so step from selectionStart(), not
    // position() - otherwise 上一处 from the first match anchors inside that
    // same match and never moves. Backward past the first match anchors to the
    // END of the document, which is what makes the wrap work.
    const int size = m_findEdit->text().size();
    const int selStart = m_editor->textCursor().selectionStart();
    const int docEnd = m_editor->toPlainText().size();
    int anchor = 0;
    if (forward)
        anchor = qMin(selStart + qMax(1, size), docEnd);
    else
        anchor = (selStart <= 0) ? docEnd : selStart - 1;
    QTextCursor c = m_editor->textCursor();
    c.setPosition(anchor);
    m_editor->setTextCursor(c);
    updateFindHighlights(forward);
}

void NotesPanel::polishGuideDocument(QTextDocument* doc)
{
    // setMarkdown() gives code spans the system fixed font — Courier New on
    // Windows, whose asterisk glyph is drawn superscript-style in the upper
    // half of the line (it LOOKS cropped). No stylesheet reaches these spans
    // (setMarkdown bypasses HTML/CSS), so walk the document and swap every
    // fixed-pitch run to Consolas, whose "*" is vertically centered.
    for (QTextBlock block = doc->begin(); block != doc->end();
         block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            QTextCharFormat f = frag.charFormat();
            const QStringList fams = f.fontFamilies().toStringList();
            const bool isCode = f.fontFixedPitch()
                || (!fams.isEmpty()
                    && fams.first().contains(QStringLiteral("Courier")));
            if (!isCode)
                continue;
            f.setFontFamilies({QStringLiteral("Consolas"),
                               QStringLiteral("Courier New")});
            QTextCursor c(doc);
            c.setPosition(frag.position());
            c.setPosition(frag.position() + frag.length(),
                          QTextCursor::KeepAnchor);
            c.mergeCharFormat(f);
        }
    }
}

QString NotesPanel::markdownGuide()
{
    return QString::fromUtf8(
        "# Markdown 语法指南\n\n"
        "Markdown 是一种轻量级的标记语言，它的核心设计理念是"
        "“可读性即一切”，扩展名通常为 .md 或 .markdown，"
        "即使不进行渲染，原始文本也层次分明、清晰易读。"
        "本指南分为两部分：**Markdown基本语法** 与 "
        "**Markdown 扩展语法**。\n\n"
        "---\n\n"
        "# Markdown基本语法\n\n"
        "以下为Markdown基本语法<https://markdown.com.cn>，"
        "本应用支持在阅读页右侧的笔记板块输入以下语法，"
        "通过右上角菜单的 **Markdown渲染** 查看最终效果：\n\n"
        "## 1. 标题\n\n"
        "行首输入 `#` + 空格 → 一级标题\n\n"
        "`##`、`###`、`####`、`#####`、`######` + 空格 → 二至六级标题，"
        "字号逐级递减\n\n"
        "## 2. 段落\n\n"
        "段落之间用**空行**分隔；连续的文字行属于同一段落。\n\n"
        "## 3. 换行\n\n"
        "行尾输入两个空格后回车 → 段内强制换行（不产生新段落）\n\n"
        "## 4. 强调\n\n"
        "输入**成对**的标记符号，敲下收尾标记的瞬间转换：\n\n"
        "| 输入 | 效果 |\n"
        "| --- | --- |\n"
        "| `**文字**` | **粗体** |\n"
        "| `*文字*` | *斜体* |\n"
        "| `***文字***` | ***粗斜体*** |\n\n"
        "## 5. 引用块\n\n"
        "行首输入 `>` + 空格 → 引用块（灰色文字、左侧缩进），"
        "适合摘录书中原文\n\n"
        "## 6. 列表\n\n"
        "行首输入 `-` 或 `*` + 空格 → 无序列表（圆点）\n\n"
        "行首输入 `1.` + 空格 → 有序列表（数字编号）\n\n"
        "列表中按回车自动延续下一项；在空项上回车退出列表\n\n"
        "## 7. 代码\n\n"
        "行内代码用反引号包裹：`` `代码` `` → `等宽代码`（灰底）\n\n"
        "行首输入三个反引号 + 回车 → 代码块（等宽字体、灰底整块）\n\n"
        "## 8. 分隔线\n\n"
        "单独一行输入 `---` 后回车 → 水平分隔线\n\n"
        "## 9. 链接\n\n"
        "输入 `[链接文字](网址)` → 可点击的链接\n\n"
        "输入 `<网址>` → 自动链接（如 <https://markdown.com.cn>）\n\n"
        "## 10. 图片\n\n"
        "输入 `![图片描述](图片路径)` → 内嵌显示本地或网络图片\n\n"
        "支持直接粘贴图片（Ctrl+V）：图片自动保存并插入图片引用\n\n"
        "## 11. 转义字符\n\n"
        "要显示标记符号本身时，前面加反斜杠：`\\*不是斜体\\*`\n\n"
        "可转义的字符：`` \\ ` * _ { } [ ] ( ) # + - . ! | ``\n\n"
        "## 12. 内嵌HTML标签\n\n"
        "可以直接书写少量 HTML，如 `<br>`（换行）、`<b>粗体</b>`、"
        "`<sub>下标</sub>`、`<sup>上标</sup>`\n\n"
        "---\n\n"
        "# Markdown扩展语法\n\n"
        "以下为Markdown扩展语法，部分语法暂不支持渲染。包含：\n\n"
        "CommonMark<https://commonmark.org>、\n\n"
        "GFM（GitHub Flavored Markdown）"
        "<https://github.github.com/gfm>。\n\n"
        "尽管Markdown尚未形成单一国际标准，但CommonMark + GFM的组合"
        "已构成当前事实上的“双轨制行业规范”。\n\n"
        "## 1. 表格\n\n"
        "```\n"
        "| 左对齐 | 居中 | 右对齐 |\n"
        "| :--- | :---: | ---: |\n"
        "| 单元格 | 单元格 | 单元格 |\n"
        "```\n\n"
        "分隔行的冒号位置控制对齐方式。\n\n"
        "## 2. 围栏代码块\n\n"
        "三个反引号（或波浪线）围起代码块，"
        "开头一行可标注语言名获得语法着色：\n\n"
        "```\n"
        "int main() { return 0; }   // 上下各一行 ```cpp 与 ``` 包裹\n"
        "```\n\n"
        "## 3. 脚注\n\n"
        "```\n"
        "正文中的引用标记[^1]\n"
        "\n"
        "[^1]: 页面底部的脚注内容。\n"
        "```\n\n"
        "## 4. 标题编号\n\n"
        "为标题指定自定义锚点 ID，便于页内跳转：\n\n"
        "```\n"
        "### 我的标题 {#custom-id}\n"
        "\n"
        "[跳转到我的标题](#custom-id)\n"
        "```\n\n"
        "## 5. 定义列表\n\n"
        "```\n"
        "术语\n"
        ": 术语的定义说明\n"
        "```\n\n"
        "## 6. 删除线\n\n"
        "`~~文字~~` → ~~删除线~~\n\n"
        "## 7. 任务列表\n\n"
        "```\n"
        "- [ ] 未完成事项\n"
        "- [x] 已完成事项\n"
        "```\n\n"
        "## 8. Emoji表情\n\n"
        "输入表情短代码，如 `:smile:`、`:heart:`、`:+1:`，"
        "在支持 GFM 的平台上渲染为表情符号。\n\n"
        "## 9. 自动网址链接\n\n"
        "GFM 下直接书写网址（如 https://markdown.com.cn ）"
        "无需尖括号即可自动转为链接。\n");
}

QString NotesPanel::notesFilePath(const QString& bookPath) const
{
    // <AppData>/Genesis/notes/<book-base-name>.md
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.mkpath(QStringLiteral("notes"));
    return dir.filePath(QStringLiteral("notes/")
                        + QFileInfo(bookPath).completeBaseName()
                        + QStringLiteral(".md"));
}

void NotesPanel::setBook(const QString& bookPath)
{
    if (m_bookPath == bookPath)
        return;
    save();   // flush the previous book's pending edits

    m_bookPath = bookPath;
    m_loading = true;
    if (bookPath.isEmpty()) {
        m_editor->clear();
        m_editor->setEnabled(false);
    } else {
        QString text;
        QFile f(notesFilePath(bookPath));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            text = QString::fromUtf8(f.readAll());
        // Notes now keep Markdown as source text. Rendering is performed only
        // by the on-demand preview drawer.
        m_editor->setPlainText(text);
        m_editor->setEnabled(true);
    }
    m_loading = false;
}

QString NotesPanel::markdownText() const
{
    return m_editor ? m_editor->toPlainText() : QString();
}

int NotesPanel::topVisibleLine() const
{
    return m_editor ? Genesis::markdownEditorTopLine(m_editor) : 0;
}

namespace {   // ---- helpers for persisting pasted pictures beside the note ----

// <AppData>/Genesis/notes/images  (AppData already ends with …/Genesis in-app)
QString notesImagesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/notes/images");
}

// Absolute path in `dir` named `<stem>[_n]<suffix>` that does not exist yet.
QString uniquifyImagePath(const QString& dir, const QString& stem,
                          const QString& suffix)
{
    for (int i = 0;; ++i) {   // same-second pastes must not collide
        const QString name =
            i ? QStringLiteral("%1_%2").arg(stem).arg(i) : stem;
        const QString p = QDir(dir).filePath(name + suffix);
        if (!QFileInfo::exists(p))
            return p;
    }
}

// Normalised Markdown inline-image reference; a space in the path is only
// legal inside a <...> destination.
QString markdownImageRef(const QString& path)
{
    return QStringLiteral("![图片](%1)").arg(
        path.contains(QLatin1Char(' '))
            ? QStringLiteral("<%1>").arg(path)
            : path);
}

// The per-note filename stem "<book base name>_<isodate>" shared by every
// image pasted for the currently open book.
QString imageFileStem(const QString& bookPath)
{
    return QFileInfo(bookPath).completeBaseName()
        + QLatin1Char('_')
        + QDateTime::currentDateTime().toString(
              QStringLiteral("yyyyMMdd-HHmmss"));
}

// Resolve one image destination from the note's Markdown to a readable local
// file. Mirrors the preview's own lookup (see MdPreviewDocument::
// resolveLocalImage): absolute paths as-is, relative ones against the notes
// folder and its images/ subfolder. Returns "" when nothing is on disk.
QString resolveNoteImage(const QString& dest)
{
    QString s = dest;
    if (s.startsWith(QLatin1Char('<')) && s.endsWith(QLatin1Char('>')))
        s = s.mid(1, s.size() - 2);              // <...> destination form
    s = QUrl::fromPercentEncoding(s.toUtf8());
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (s.isEmpty() || s.contains(QLatin1String("://"))
        || s.startsWith(QLatin1String("data:")))
        return QString();                        // remote/already inline

    const QFileInfo fi(s);
    if (fi.isFile())
        return s;
    if (!fi.isRelative())
        return QString();
    const QString notes =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/notes");
    const QStringList bases = {QDir::currentPath(), notes,
                               notes + QStringLiteral("/images")};
    for (const QString& base : bases) {
        const QString p = QDir(base).filePath(s);
        if (QFileInfo::exists(p))
            return p;
    }
    return QString();
}

// Inline every local picture in `md` as a data: URI. A site like Yuque cannot
// fetch this machine's files, so an exported note must carry its bytes along.
// Remote URLs and references that are already inline are left untouched.
QString inlineLocalImages(const QString& md, int* inlinedCount)
{
    static const QRegularExpression imgRe(QStringLiteral(
        "(!\\[[^\\]]*\\]\\()\\s*(<[^>]*>|[^)\\s]+)(\\s*\\))"));
    QString out;
    int last = 0;
    int count = 0;
    auto it = imgRe.globalMatch(md);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString path = resolveNoteImage(m.captured(2));
        if (path.isEmpty())
            continue;
        QImage img(path);
        if (img.isNull())
            continue;
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        // Keep the source format: a JPEG pasted as a photo stays JPEG (base64
        // of a re-encoded PNG would balloon the note), everything else PNG.
        const QString suffix = QFileInfo(path).suffix().toLower();
        const bool jpeg = suffix == QLatin1String("jpg")
                          || suffix == QLatin1String("jpeg");
        const char* fmt = jpeg ? "JPEG" : "PNG";
        if (!img.save(&buf, fmt))
            continue;
        buf.close();
        const QString mime = jpeg ? QStringLiteral("image/jpeg")
                                  : QStringLiteral("image/png");
        out += md.mid(last, m.capturedStart() - last);
        out += m.captured(1) + QStringLiteral("data:%1;base64,%2")
                                    .arg(mime, QString::fromLatin1(
                                                   bytes.toBase64()))
               + m.captured(3);
        last = m.capturedEnd();
        ++count;
    }
    out += md.mid(last);
    if (inlinedCount)
        *inlinedCount = count;
    return out;
}

} // namespace

int NotesPanel::copyAsPortableMarkdown()
{
    if (!m_editor)
        return 0;
    int inlined = 0;
    const QString portable =
        inlineLocalImages(m_editor->toPlainText(), &inlined);
    if (QClipboard* cb = QGuiApplication::clipboard())
        cb->setText(portable);
    return inlined;
}

QString NotesPanel::pasteImageAsMarkdown(const QImage& image)
{
    if (m_bookPath.isEmpty() || image.isNull())
        return QString();
    QDir().mkpath(notesImagesDir());
    const QString path = uniquifyImagePath(
        notesImagesDir(), imageFileStem(m_bookPath), QStringLiteral(".png"));
    if (!image.save(path, "PNG"))
        return QString();

    // The picture lands on its own line unless the cursor already is.
    const QTextCursor c = m_editor->textCursor();
    QString md;
    if (c.positionInBlock() != 0)
        md += QLatin1Char('\n');
    md += markdownImageRef(path);
    if (c.positionInBlock() != c.block().length())
        md += QLatin1Char('\n');
    return md;
}

QString NotesPanel::pasteImageFilesAsMarkdown(const QStringList& imagePaths)
{
    if (m_bookPath.isEmpty())
        return QString();
    if (imagePaths.isEmpty())
        return QString();
    if (!m_editor)
        return QString();

    const QString dir = notesImagesDir();
    QDir().mkpath(dir);

    // Import each local picture file: make its own time-stamped copy under
    // notes/images (keep the source suffix), so the note stays self-contained.
    QStringList refs;
    for (const QString& src : imagePaths) {
        QString suffix = QFileInfo(src).suffix();
        if (suffix.isEmpty())
            suffix = QStringLiteral("png");
        else
            suffix.prepend(QLatin1Char('.'));
        const QString dst = uniquifyImagePath(dir, imageFileStem(m_bookPath),
                                              suffix.toLower());
        if (!QFile::copy(src, dst))
            continue;
        refs << markdownImageRef(dst);
    }
    if (refs.isEmpty())
        return QString();

    // One picture per line; the block sits on its own lines at the cursor.
    const QTextCursor c = m_editor->textCursor();
    QString md;
    if (c.positionInBlock() != 0)
        md += QLatin1Char('\n');
    md += refs.join(QLatin1Char('\n'));
    if (c.positionInBlock() != c.block().length())
        md += QLatin1Char('\n');
    return md;
}

void NotesPanel::save()
{
    m_saveTimer->stop();
    if (m_bookPath.isEmpty())
        return;
    const QString path = notesFilePath(m_bookPath);
    const QString text = m_editor->toPlainText();
    if (text.trimmed().isEmpty()) {
        QFile::remove(path);
        return;
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        f.write(text.toUtf8());
}

} // namespace Genesis
