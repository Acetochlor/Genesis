#include "BookshelfView.h"
#include "IconButton.h"
#include "BookCardWidget.h"
#include "CardGrid.h"
#include "../dialogs/FramelessDialog.h"
#include "../widgets/MenuOption.h"
#include "../library/MetadataStore.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QGridLayout>
#include <QMenu>
#include <QWidgetAction>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>

namespace Genesis {

namespace {
// Accent-blue check mark for the selection checkboxes.
QString checkImagePath()
{
    static QString path;
    if (!path.isEmpty())
        return path;

    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.mkpath(QStringLiteral("."));
    path = dir.filePath(QStringLiteral("check.png"));

    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#33CCFF"), 4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    QPainterPath check;
    check.moveTo(7, 17);
    check.lineTo(13, 23);
    check.lineTo(25, 9);
    p.drawPath(check);
    p.end();
    pm.save(path, "PNG");
    return path;
}

} // namespace

BookshelfView::BookshelfView(QWidget* parent)
    : QWidget(parent)
    , m_searchBox(nullptr)
    , m_searchButton(nullptr)
    , m_kebabButton(nullptr)
    , m_selectionBar(nullptr)
    , m_selectAll(nullptr)
    , m_gridContainer(nullptr)
    , m_sortMode(Library::SortMode::ByName)
    , m_selectionMode(false)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("BookshelfView { background: transparent; }");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 12);
    layout->setSpacing(8);

    buildToolbar();

    // --- selection bar (hidden by default) ---
    layout->addWidget(m_selectionBar);

    // --- toolbar row: stretch, search box, search icon, kebab icon ---
    QHBoxLayout* toolbar = new QHBoxLayout;
    toolbar->setSpacing(4);
    toolbar->addStretch();
    toolbar->addWidget(m_searchBox);
    toolbar->addWidget(m_searchButton);
    toolbar->addWidget(m_kebabButton);
    layout->insertLayout(0, toolbar);

    // --- scrollable book card grid ---
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(
        "QScrollArea { background: transparent; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }");

    m_gridContainer = new CardGrid(this);
    scroll->setWidget(m_gridContainer);
    layout->addWidget(scroll, 1);

    refresh();
}

void BookshelfView::buildToolbar()
{
    // Inline search box, collapsed until the search icon is clicked.
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(QString::fromUtf8("搜索图书…"));
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setFixedWidth(200);
    m_searchBox->setStyleSheet(
        "QLineEdit { background: #FFFFFF; color: #333333;"
        "  border: 1px solid #CCEEFF; border-radius: 6px; padding: 4px 8px; }"
        "QLineEdit:focus { border-color: #33CCFF; }");
    m_searchBox->hide();
    connect(m_searchBox, &QLineEdit::textChanged,
            this, &BookshelfView::applyFilter);

    m_searchButton = new IconButton(IconButton::Glyph::Search, this);
    m_searchButton->setToolTip(QString::fromUtf8("搜索"));
    connect(m_searchButton, &IconButton::clicked,
            this, &BookshelfView::toggleSearch);

    m_kebabButton = new IconButton(IconButton::Glyph::Kebab, this);
    m_kebabButton->setToolTip(QString::fromUtf8("更多"));
    connect(m_kebabButton, &IconButton::clicked,
            this, &BookshelfView::showKebabMenu);

    // --- selection-mode action row (hidden by default) ---
    m_selectionBar = new QWidget(this);
    QHBoxLayout* selLayout = new QHBoxLayout(m_selectionBar);
    selLayout->setContentsMargins(0, 0, 0, 0);
    selLayout->setSpacing(8);

    const char* smallBtnCss =
        "QPushButton { background: #FFFFFF; color: #33CCFF;"
        "  border: 1px solid #CCEEFF; border-radius: 4px; padding: 4px 14px; }"
        "QPushButton:hover { background: #E6F7FF; }"
        "QPushButton:pressed { background: #CCEEFF; }";

    m_selectAll = new QPushButton(QString::fromUtf8("全选"), m_selectionBar);
    m_selectAll->setCursor(Qt::PointingHandCursor);
    m_selectAll->setStyleSheet(smallBtnCss);
    connect(m_selectAll, &QPushButton::clicked, this, [this]() {
        const bool allChecked = [this]() {
            for (auto* card : m_cards) {
                if (!card->isVisible()) continue;
                if (!card->isSelected())
                    return false;
            }
            for (auto* card : m_cards) {
                if (card->isVisible()) return true;
            }
            return false;
        }();
        for (auto* card : m_cards) {
            if (!card->isVisible()) continue;
            card->setSelected(!allChecked);
            if (allChecked)
                m_selectedPaths.remove(card->filePath());
            else
                m_selectedPaths.insert(card->filePath());
        }
    });
    selLayout->addWidget(m_selectAll);

    QPushButton* del = new QPushButton(QString::fromUtf8("删除"), m_selectionBar);
    del->setCursor(Qt::PointingHandCursor);
    del->setStyleSheet(
        "QPushButton { background: #FFFFFF; color: #E81123;"
        "  border: 1px solid #F5C6CB; border-radius: 4px; padding: 4px 14px; }"
        "QPushButton:hover { background: #FDECEA; }"
        "QPushButton:pressed { background: #F5C6CB; }");
    connect(del, &QPushButton::clicked, this, &BookshelfView::deleteSelected);
    selLayout->addWidget(del);

    selLayout->addStretch();

    QPushButton* cancel = new QPushButton(QString::fromUtf8("取消"), m_selectionBar);
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setStyleSheet(smallBtnCss);
    connect(cancel, &QPushButton::clicked,
            this, &BookshelfView::leaveSelectionMode);
    selLayout->addWidget(cancel);

    m_selectionBar->hide();
}

void BookshelfView::refresh()
{
    const QStringList books = Library::books(m_sortMode);
    const auto allMeta = MetadataStore::loadAll();

    // Remove old cards.
    for (auto* card : m_cards)
        card->deleteLater();
    m_cards.clear();
    m_selectedPaths.clear();

    for (const QString& path : books) {
        BookInfo info;
        const auto it = allMeta.constFind(path);
        if (it != allMeta.constEnd())
            info = it.value();
        else {
            info.filePath = path;
            info.title = QFileInfo(path).completeBaseName();
        }

        BookCardWidget* card = new BookCardWidget(path, info, m_gridContainer);
        card->setSelectionMode(m_selectionMode);
        // Single click: toggles selection (only in selection mode).
        connect(card, &BookCardWidget::clicked,
                this, [this, card](const QString& p) {
                    if (!m_selectionMode)
                        return;
                    card->setSelected(!card->isSelected());
                    if (card->isSelected())
                        m_selectedPaths.insert(p);
                    else
                        m_selectedPaths.remove(p);
                });
        // Double click: opens the book (outside selection mode).
        connect(card, &BookCardWidget::activated,
                this, [this](const QString& p) {
                    if (!m_selectionMode)
                        emit bookActivated(p);
                });
        m_cards << card;
    }

    auto* grid = qobject_cast<CardGrid*>(m_gridContainer);
    if (grid)
        grid->setCards(m_cards);

    applyFilter(m_searchBox->isVisible() ? m_searchBox->text() : QString());
}

void BookshelfView::toggleSearch()
{
    if (m_searchBox->isVisible()) {
        m_searchBox->hide();
        m_searchBox->clear();
    } else {
        m_searchBox->show();
        m_searchBox->setFocus();
    }
}

void BookshelfView::applyFilter(const QString& text)
{
    for (auto* card : m_cards)
        card->setVisible(text.isEmpty()
            || card->filePath().contains(text, Qt::CaseInsensitive));
}

void BookshelfView::showKebabMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #FFFFFF; color: #333333;"
        "  border: 1px solid #CCEEFF; }"
        "QMenu::item { padding: 6px 26px; }"
        "QMenu::item:selected { background: #E6F7FF; color: #33CCFF; }");

    QAction* refreshAct = menu.addAction(QString::fromUtf8("刷新"));
    connect(refreshAct, &QAction::triggered, this, &BookshelfView::refresh);

    QAction* selectAct = menu.addAction(QString::fromUtf8("选择"));
    connect(selectAct, &QAction::triggered,
            this, &BookshelfView::enterSelectionMode);

    // Sort submenu via MenuOption (green dot).
    QMenu* sortMenu = menu.addMenu(QString::fromUtf8("排序方式"));
    sortMenu->setStyleSheet(menu.styleSheet());
    const auto addSortOption = [&](const QString& text, Library::SortMode mode) {
        QWidgetAction* act = new QWidgetAction(sortMenu);
        act->setDefaultWidget(new MenuOption(
            text, m_sortMode == mode, [this, mode, &menu]() {
                m_sortMode = mode;
                menu.close();
                refresh();
            }));
        sortMenu->addAction(act);
    };
    addSortOption(QString::fromUtf8("按名称排序"), Library::SortMode::ByName);
    addSortOption(QString::fromUtf8("按时间排序"), Library::SortMode::ByTime);

    menu.exec(m_kebabButton->mapToGlobal(
        QPoint(m_kebabButton->width() - menu.sizeHint().width(),
               m_kebabButton->height() + 2)));
}

void BookshelfView::enterSelectionMode()
{
    if (m_selectionMode)
        return;
    m_selectionMode = true;
    m_selectedPaths.clear();
    m_selectionBar->show();
    // Show the hollow dot slot on every cover.
    for (auto* card : m_cards)
        card->setSelectionMode(true);
}

void BookshelfView::leaveSelectionMode()
{
    if (!m_selectionMode)
        return;
    m_selectionMode = false;
    m_selectedPaths.clear();
    m_selectionBar->hide();
    refresh();
}

void BookshelfView::deleteSelected()
{
    QStringList paths;
    for (const QString& p : m_selectedPaths)
        paths << p;
    if (paths.isEmpty()) {
        FramelessDialog::information(
            window(), QString::fromUtf8("删除图书"),
            QString::fromUtf8("请先点选要删除的图书。"));
        return;
    }
    if (!FramelessDialog::question(
            window(), QString::fromUtf8("删除图书"),
            QString::fromUtf8("确定从书架删除选中的 %1 本书吗？此操作不可恢复。")
                .arg(paths.size())))
        return;

    const QStringList failed = Library::removeFiles(paths);
    leaveSelectionMode();   // refreshes: deleted books drop off the shelf

    if (!failed.isEmpty()) {
        // Reaching here means the retry exhausted: another program (an
        // external PDF viewer, antivirus) is holding the file open.
        FramelessDialog::information(
            window(), QString::fromUtf8("删除图书"),
            QString::fromUtf8(
                "以下 %1 本书删除失败（文件正被其他程序占用）：\n%2\n\n"
                "请关闭正在使用该文件的程序后重试。")
                .arg(failed.size())
                .arg(failed.join(QStringLiteral("\n"))));
    }
}

} // namespace Genesis
