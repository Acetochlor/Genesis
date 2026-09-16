#include "BookmarkDialog.h"

#include "../widgets/MenuOption.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>

namespace Genesis {

namespace {

// Roles on QListWidgetItem::data.
constexpr int kRoleBookPath = Qt::UserRole;
constexpr int kRolePage     = Qt::UserRole + 1;
constexpr int kRoleName     = Qt::UserRole + 2;
constexpr int kRoleCreated  = Qt::UserRole + 3;
constexpr int kRoleBadge    = Qt::UserRole + 4;   // badge text ("3" / "第 5 页")
constexpr int kRoleChecked  = Qt::UserRole + 5;   // selection-dot state
constexpr int kRoleTime     = Qt::UserRole + 6;   // creation-time badge text

const char* kListCss =
    "QListWidget { background: #FFFFFF; color: #333333;"
    "  border: 1px solid #CCEEFF; border-radius: 8px; font-size: 13px;"
    "  padding: 4px; outline: none; }"
    "QListWidget::item { padding: 8px 10px; border-radius: 4px; }"
    "QListWidget::item:hover { background: #E6F7FF; }"
    "QListWidget::item:selected { background: #CCEEFF; color: #333333; }";

const char* kSmallBtnCss =
    "QPushButton { background: #FFFFFF; color: #33CCFF;"
    "  border: 1px solid #CCEEFF; border-radius: 4px; padding: 4px 12px; }"
    "QPushButton:hover { background: #E6F7FF; }"
    "QPushButton:pressed { background: #CCEEFF; }"
    "QPushButton:checked { background: #CCEEFF; }";

const char* kDangerBtnCss =
    "QPushButton { background: #FFFFFF; color: #E81123;"
    "  border: 1px solid #F5C6CB; border-radius: 4px; padding: 4px 12px; }"
    "QPushButton:hover { background: #FDECEA; }"
    "QPushButton:pressed { background: #F5C6CB; }";

const char* kPrimaryBtnCss =
    "QPushButton { background: #33CCFF; color: #FFFFFF; border: none;"
    "  border-radius: 4px; padding: 6px 18px; }"
    "QPushButton:hover { background: #2AB8E8; }"
    "QPushButton:pressed { background: #229FCC; }";

const char* kLineEditCss =
    "QLineEdit { background: #FFFFFF; color: #333333;"
    "  border: 1px solid #CCEEFF; border-radius: 4px; padding: 5px 8px; }"
    "QLineEdit:focus { border-color: #33CCFF; }";

} // namespace

// Paints one list row: elided text on the left; a rounded rectangular badge
// ("3" for group counts, "第 5 页" for bookmarks) hugging the right edge; in
// selection mode the app-wide dot (hollow black / filled green) sits at the
// very right and the badge shifts left so they never overlap.
class BookmarkRowDelegate : public QStyledItemDelegate {
public:
    explicit BookmarkRowDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_selectionMode(false) {}

    void setSelectionMode(bool on) { m_selectionMode = on; }
    bool selectionMode() const { return m_selectionMode; }

    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        // Background (hover/selected) only: draw the item with its text
        // cleared. NOTE: QStyledItemDelegate::paint() would re-run
        // initStyleOption() and restore the text — drawing it twice — so go
        // through the style directly.
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        const QWidget* w = option.widget;
        QStyle* style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

        const QRect r = option.rect.adjusted(10, 0, -10, 0);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        // --- right side, right-to-left: dot (selection mode), time badge,
        //     page/count badge — none of them ever overlap ---
        int rightEdge = r.right();
        if (m_selectionMode) {
            const bool checked = index.data(kRoleChecked).toBool();
            const QPointF c(rightEdge - MenuOption::kDotRadius,
                            r.center().y() + 0.5);
            MenuOption::drawDot(*p, c, checked);
            rightEdge -= 2 * MenuOption::kDotRadius + 10;
        }

        rightEdge = drawBadge(p, index.data(kRoleTime).toString(),
                              r, rightEdge);
        rightEdge = drawBadge(p, index.data(kRoleBadge).toString(),
                              r, rightEdge);

        // --- left: item text, elided so it never runs under the badges ---
        p->setFont(option.font);
        p->setPen(QColor("#333333"));
        const QFontMetrics fm(option.font);
        const QString elided = fm.elidedText(
            index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
            rightEdge - r.left());
        p->drawText(QRect(r.left(), r.top(), rightEdge - r.left(),
                          r.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, elided);
        p->restore();
    }

private:
    // Rounded-rect badge anchored at rightEdge; returns the new rightEdge
    // (unchanged when text is empty).
    static int drawBadge(QPainter* p, const QString& text, const QRect& r,
                         int rightEdge)
    {
        if (text.isEmpty())
            return rightEdge;
        QFont bf;
        bf.setPixelSize(11);
        const QFontMetrics bfm(bf);
        const int bw = bfm.horizontalAdvance(text) + 14;
        const int bh = bfm.height() + 4;
        const QRectF badgeRect(rightEdge - bw,
                               r.center().y() - bh / 2.0, bw, bh);
        p->setPen(QPen(QColor("#CCEEFF"), 1));
        p->setBrush(QColor("#F4FBFF"));
        p->drawRoundedRect(badgeRect, 8, 8);
        p->setPen(QColor("#33CCFF"));
        p->setFont(bf);
        p->drawText(badgeRect, Qt::AlignCenter, text);
        return rightEdge - bw - 8;
    }

    bool m_selectionMode;
};

BookmarkDialog::BookmarkDialog(QWidget* parent)
    : FramelessDialog(QString::fromUtf8("书签"), parent)
    , m_stack(nullptr)
    , m_groupList(nullptr)
    , m_groupDelegate(nullptr)
    , m_groupSelectBtn(nullptr)
    , m_groupAllBtn(nullptr)
    , m_groupRenameBtn(nullptr)
    , m_groupDeleteBtn(nullptr)
    , m_groupSelMode(false)
    , m_bookmarkList(nullptr)
    , m_bookmarkDelegate(nullptr)
    , m_backButton(nullptr)
    , m_bookSelectBtn(nullptr)
    , m_bookAllBtn(nullptr)
    , m_bmRenameBtn(nullptr)
    , m_bmDeleteBtn(nullptr)
    , m_bookTitle(nullptr)
    , m_bookSelMode(false)
{
    resize(420, 480);

    // Drop bookmarks of books deleted from the shelf before showing anything.
    BookmarkStore::purgeMissingBooks();

    m_stack = new QStackedWidget(this);
    contentLayout()->addWidget(m_stack, 1);

    // ================= page 0: groups =================
    QWidget* groupPage = new QWidget(m_stack);
    QVBoxLayout* groupLayout = new QVBoxLayout(groupPage);
    // 16px at the bottom, matching the left/right margins (the shared
    // content margin has no bottom — each page supplies its own).
    groupLayout->setContentsMargins(0, 0, 0, 16);
    groupLayout->setSpacing(8);

    // Header: 选择 (→取消) far left, 全选 beside it in selection mode;
    // 重命名 / 删除 appear on the far right meanwhile.
    QHBoxLayout* groupHeader = new QHBoxLayout;
    groupHeader->setSpacing(8);
    m_groupSelectBtn = new QPushButton(QString::fromUtf8("选择"), groupPage);
    m_groupSelectBtn->setCheckable(true);
    m_groupSelectBtn->setCursor(Qt::PointingHandCursor);
    m_groupSelectBtn->setStyleSheet(kSmallBtnCss);
    connect(m_groupSelectBtn, &QPushButton::toggled,
            this, &BookmarkDialog::setGroupSelectionMode);
    groupHeader->addWidget(m_groupSelectBtn);

    m_groupAllBtn = new QPushButton(QString::fromUtf8("全选"), groupPage);
    m_groupAllBtn->setCursor(Qt::PointingHandCursor);
    m_groupAllBtn->setStyleSheet(kSmallBtnCss);
    m_groupAllBtn->hide();
    connect(m_groupAllBtn, &QPushButton::clicked,
            this, [this]() { toggleCheckAll(m_groupList); });
    groupHeader->addWidget(m_groupAllBtn);
    groupHeader->addStretch();

    m_groupRenameBtn = new QPushButton(QString::fromUtf8("重命名"), groupPage);
    m_groupRenameBtn->setCursor(Qt::PointingHandCursor);
    m_groupRenameBtn->setStyleSheet(kSmallBtnCss);
    m_groupRenameBtn->hide();
    connect(m_groupRenameBtn, &QPushButton::clicked,
            this, &BookmarkDialog::renameCheckedGroup);
    groupHeader->addWidget(m_groupRenameBtn);

    m_groupDeleteBtn = new QPushButton(QString::fromUtf8("删除"), groupPage);
    m_groupDeleteBtn->setCursor(Qt::PointingHandCursor);
    m_groupDeleteBtn->setStyleSheet(kDangerBtnCss);
    m_groupDeleteBtn->hide();
    connect(m_groupDeleteBtn, &QPushButton::clicked,
            this, &BookmarkDialog::deleteCheckedGroups);
    groupHeader->addWidget(m_groupDeleteBtn);
    groupLayout->addLayout(groupHeader);

    m_groupList = new QListWidget(groupPage);
    m_groupList->setStyleSheet(kListCss);
    m_groupList->setCursor(Qt::PointingHandCursor);
    m_groupDelegate = new BookmarkRowDelegate(m_groupList);
    m_groupList->setItemDelegate(m_groupDelegate);
    connect(m_groupList, &QListWidget::itemClicked,
            this, [this](QListWidgetItem* item) {
                if (item->data(kRoleBookPath).toString().isEmpty())
                    return;
                if (m_groupSelMode) {
                    // Toggle the row's dot.
                    item->setData(kRoleChecked,
                                  !item->data(kRoleChecked).toBool());
                    m_groupList->viewport()->update();
                } else {
                    showBook(item->data(kRoleBookPath).toString());
                }
            });
    groupLayout->addWidget(m_groupList, 1);
    m_stack->addWidget(groupPage);

    // ================= page 1: bookmarks of one book =================
    QWidget* bookPage = new QWidget(m_stack);
    QVBoxLayout* bookLayout = new QVBoxLayout(bookPage);
    // 8px below the footer strip mirrors the 8px layout spacing above it,
    // so the bottom text sits with equal gaps above and below.
    bookLayout->setContentsMargins(0, 0, 0, 8);
    bookLayout->setSpacing(8);

    // Header: 返回 | 选择 (→取消) | 全选 …… [重命名] [删除]
    QHBoxLayout* header = new QHBoxLayout;
    header->setSpacing(8);
    m_backButton = new QPushButton(QString::fromUtf8("返回"), bookPage);
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setStyleSheet(kSmallBtnCss);
    connect(m_backButton, &QPushButton::clicked,
            this, &BookmarkDialog::showGroups);
    header->addWidget(m_backButton);

    m_bookSelectBtn = new QPushButton(QString::fromUtf8("选择"), bookPage);
    m_bookSelectBtn->setCheckable(true);
    m_bookSelectBtn->setCursor(Qt::PointingHandCursor);
    m_bookSelectBtn->setStyleSheet(kSmallBtnCss);
    connect(m_bookSelectBtn, &QPushButton::toggled,
            this, &BookmarkDialog::setBookSelectionMode);
    header->addWidget(m_bookSelectBtn);

    m_bookAllBtn = new QPushButton(QString::fromUtf8("全选"), bookPage);
    m_bookAllBtn->setCursor(Qt::PointingHandCursor);
    m_bookAllBtn->setStyleSheet(kSmallBtnCss);
    m_bookAllBtn->hide();
    connect(m_bookAllBtn, &QPushButton::clicked,
            this, [this]() { toggleCheckAll(m_bookmarkList); });
    header->addWidget(m_bookAllBtn);

    header->addStretch();

    m_bmRenameBtn = new QPushButton(QString::fromUtf8("重命名"), bookPage);
    m_bmRenameBtn->setCursor(Qt::PointingHandCursor);
    m_bmRenameBtn->setStyleSheet(kSmallBtnCss);
    m_bmRenameBtn->hide();
    connect(m_bmRenameBtn, &QPushButton::clicked,
            this, &BookmarkDialog::renameCheckedBookmark);
    header->addWidget(m_bmRenameBtn);

    m_bmDeleteBtn = new QPushButton(QString::fromUtf8("删除"), bookPage);
    m_bmDeleteBtn->setCursor(Qt::PointingHandCursor);
    m_bmDeleteBtn->setStyleSheet(kDangerBtnCss);
    m_bmDeleteBtn->hide();
    connect(m_bmDeleteBtn, &QPushButton::clicked,
            this, &BookmarkDialog::deleteCheckedBookmarks);
    header->addWidget(m_bmDeleteBtn);
    bookLayout->addLayout(header);

    m_bookmarkList = new QListWidget(bookPage);
    m_bookmarkList->setStyleSheet(kListCss);
    // Drag reorder is always armed (suspended only during selection mode).
    m_bookmarkList->setDragDropMode(QAbstractItemView::InternalMove);
    m_bookmarkList->setDefaultDropAction(Qt::MoveAction);
    m_bookmarkList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bookmarkDelegate = new BookmarkRowDelegate(m_bookmarkList);
    m_bookmarkList->setItemDelegate(m_bookmarkDelegate);
    connect(m_bookmarkList->model(), &QAbstractItemModel::rowsMoved,
            this, [this]() { persistOrder(); });
    connect(m_bookmarkList, &QListWidget::itemClicked,
            this, [this](QListWidgetItem* item) {
                if (!m_bookSelMode)
                    return;
                item->setData(kRoleChecked,
                              !item->data(kRoleChecked).toBool());
                m_bookmarkList->viewport()->update();
            });
    connect(m_bookmarkList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
                if (m_bookSelMode)
                    return;
                emit bookmarkActivated(item->data(kRoleBookPath).toString(),
                                       item->data(kRolePage).toInt());
                accept();
            });
    bookLayout->addWidget(m_bookmarkList, 1);

    // Footer: group name (elided) bottom-left, 双击跳转 hint bottom-right.
    // The strip IS the page's 8px bottom band: fixed 8px tall, 8px font, no
    // margin below — so page 1's bottom edge spacing equals 8px total.
    QHBoxLayout* footer = new QHBoxLayout;
    footer->setSpacing(8);
    m_bookTitle = new QLabel(bookPage);
    m_bookTitle->setStyleSheet("color: #888888; font-size: 8px;");
    m_bookTitle->setFixedHeight(8);
    footer->addWidget(m_bookTitle, 1);
    QLabel* jumpHint = new QLabel(
        QString::fromUtf8("【单击拖拽排序】【双击跳转】"), bookPage);
    jumpHint->setStyleSheet("color: #888888; font-size: 8px;");
    jumpHint->setFixedHeight(8);
    footer->addWidget(jumpHint, 0, Qt::AlignRight);
    bookLayout->addLayout(footer);

    m_stack->addWidget(bookPage);

    // Uniform rhythm: 16px left/right, 8px top; bottom is per-page — page 0
    // pads 16px below the list (matches left/right), page 1 ends with the
    // 8px footer strip flush to the content edge.
    contentLayout()->setContentsMargins(16, 8, 16, 0);

    showGroups();
}

void BookmarkDialog::showGroups()
{
    m_currentBook.clear();
    m_groupSelectBtn->setChecked(false);   // also hides rename/delete
    m_groupList->clear();

    // Group bookmarks by book, preserving first-seen order.
    QStringList books;
    QHash<QString, int> counts;
    for (const Bookmark& bm : BookmarkStore::loadAll()) {
        if (!counts.contains(bm.bookPath))
            books << bm.bookPath;
        counts[bm.bookPath]++;
    }

    if (books.isEmpty()) {
        QListWidgetItem* empty = new QListWidgetItem(
            QString::fromUtf8("暂无书签 — 在阅读页的更多菜单中添加"),
            m_groupList);
        empty->setFlags(Qt::NoItemFlags);
    }
    for (const QString& path : books) {
        // Group title: custom name if renamed, else the PDF's base name.
        QListWidgetItem* item = new QListWidgetItem(
            BookmarkStore::groupName(path), m_groupList);
        item->setData(kRoleBookPath, path);
        item->setData(kRoleBadge, QString::number(counts.value(path)));
        item->setData(kRoleChecked, false);
    }

    m_stack->setCurrentIndex(0);
}

void BookmarkDialog::showBook(const QString& bookPath)
{
    if (bookPath.isEmpty())
        return;
    m_currentBook = bookPath;
    m_bookSelectBtn->setChecked(false);

    // Bottom-left group name, elided to one line.
    const QString name = BookmarkStore::groupName(bookPath);
    m_bookTitle->setText(QFontMetrics(m_bookTitle->font())
        .elidedText(name, Qt::ElideRight, 260));

    m_bookmarkList->clear();

    // Stored order = display order. New bookmarks are appended on creation,
    // so an untouched group naturally shows in time order; manual drags
    // rewrite the stored order.
    for (const Bookmark& bm : BookmarkStore::forBook(bookPath)) {
        QListWidgetItem* item = new QListWidgetItem(bm.name, m_bookmarkList);
        item->setData(kRoleBookPath, bm.bookPath);
        item->setData(kRolePage, bm.page);
        item->setData(kRoleName, bm.name);
        item->setData(kRoleCreated, bm.createdAt);
        item->setData(kRoleBadge,
                      QString::fromUtf8("第 %1 页").arg(bm.page + 1));
        item->setData(kRoleTime, bm.createdAt.toString(
            QStringLiteral("yyyy-MM-dd HH:mm")));
        item->setData(kRoleChecked, false);
    }

    m_stack->setCurrentIndex(1);
}

void BookmarkDialog::setGroupSelectionMode(bool on)
{
    m_groupSelMode = on;
    m_groupDelegate->setSelectionMode(on);
    m_groupSelectBtn->setText(on ? QString::fromUtf8("取消")
                                 : QString::fromUtf8("选择"));
    m_groupAllBtn->setVisible(on);
    m_groupRenameBtn->setVisible(on);
    m_groupDeleteBtn->setVisible(on);
    if (!on) {
        for (int i = 0; i < m_groupList->count(); ++i)
            m_groupList->item(i)->setData(kRoleChecked, false);
    }
    m_groupList->viewport()->update();
}

void BookmarkDialog::setBookSelectionMode(bool on)
{
    m_bookSelMode = on;
    m_bookmarkDelegate->setSelectionMode(on);
    m_bookSelectBtn->setText(on ? QString::fromUtf8("取消")
                                : QString::fromUtf8("选择"));
    m_bookAllBtn->setVisible(on);
    m_bmRenameBtn->setVisible(on);
    m_bmDeleteBtn->setVisible(on);
    // Suspend drag reorder while selecting; re-arm it afterwards.
    m_bookmarkList->setDragDropMode(on ? QAbstractItemView::NoDragDrop
                                       : QAbstractItemView::InternalMove);
    if (!on) {
        for (int i = 0; i < m_bookmarkList->count(); ++i)
            m_bookmarkList->item(i)->setData(kRoleChecked, false);
    }
    m_bookmarkList->viewport()->update();
}

void BookmarkDialog::toggleCheckAll(QListWidget* list)
{
    // If every (selectable) row is already checked, uncheck all instead.
    bool allChecked = true;
    int selectable = 0;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        if (!(item->flags() & Qt::ItemIsEnabled))
            continue;
        ++selectable;
        if (!item->data(kRoleChecked).toBool())
            allChecked = false;
    }
    if (selectable == 0)
        return;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        if (item->flags() & Qt::ItemIsEnabled)
            item->setData(kRoleChecked, !allChecked);
    }
    list->viewport()->update();
}

QList<QListWidgetItem*> BookmarkDialog::checkedItems(QListWidget* list) const
{
    QList<QListWidgetItem*> checked;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(kRoleChecked).toBool())
            checked << list->item(i);
    }
    return checked;
}

QString BookmarkDialog::promptText(const QString& title, const QString& hint,
                                   const QString& initial, bool* ok)
{
    *ok = false;
    FramelessDialog dlg(title, this);

    if (!hint.isEmpty()) {
        QLabel* hintLabel = new QLabel(hint, &dlg);
        hintLabel->setStyleSheet("color: #888888; font-size: 12px;");
        dlg.contentLayout()->addWidget(hintLabel);
    }

    QLineEdit* edit = new QLineEdit(&dlg);
    edit->setText(initial);
    edit->selectAll();
    edit->setMinimumWidth(260);
    edit->setStyleSheet(kLineEditCss);
    dlg.contentLayout()->addWidget(edit);

    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addStretch();
    QPushButton* cancel = new QPushButton(QString::fromUtf8("取消"), &dlg);
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setStyleSheet(kSmallBtnCss);
    QPushButton* okBtn = new QPushButton(QString::fromUtf8("确定"), &dlg);
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setDefault(true);
    okBtn->setStyleSheet(kPrimaryBtnCss);
    buttons->addWidget(cancel);
    buttons->addWidget(okBtn);
    dlg.contentLayout()->addSpacing(4);
    dlg.contentLayout()->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
    edit->setFocus();

    if (dlg.exec() != QDialog::Accepted)
        return QString();
    *ok = true;
    return edit->text().trimmed();
}

void BookmarkDialog::renameCheckedGroup()
{
    const auto checked = checkedItems(m_groupList);
    if (checked.size() != 1) {
        FramelessDialog::information(
            this, QString::fromUtf8("重命名分组"),
            QString::fromUtf8("请先点选一个分组（每次只能重命名一个）。"));
        return;
    }
    const QString path = checked.first()->data(kRoleBookPath).toString();
    bool ok = false;
    const QString name = promptText(
        QString::fromUtf8("重命名分组"), QString(),
        BookmarkStore::groupName(path), &ok);
    if (!ok)
        return;
    BookmarkStore::setGroupName(path, name);
    showGroups();
}

void BookmarkDialog::deleteCheckedGroups()
{
    const auto checked = checkedItems(m_groupList);
    if (checked.isEmpty()) {
        FramelessDialog::information(
            this, QString::fromUtf8("删除分组"),
            QString::fromUtf8("请先点选要删除的分组。"));
        return;
    }
    if (!FramelessDialog::question(
            this, QString::fromUtf8("删除分组"),
            QString::fromUtf8("确定删除选中的 %1 个分组及其全部书签吗？")
                .arg(checked.size())))
        return;
    for (const QListWidgetItem* item : checked) {
        const QString path = item->data(kRoleBookPath).toString();
        BookmarkStore::setForBook(path, {});
        BookmarkStore::setGroupName(path, QString());   // drop custom name
    }
    showGroups();
}

void BookmarkDialog::renameCheckedBookmark()
{
    const auto checked = checkedItems(m_bookmarkList);
    if (checked.size() != 1) {
        FramelessDialog::information(
            this, QString::fromUtf8("重命名书签"),
            QString::fromUtf8("请先点选一个书签（每次只能重命名一个）。"));
        return;
    }
    QListWidgetItem* item = checked.first();
    bool ok = false;
    const QString name = promptText(
        QString::fromUtf8("重命名书签"), QString(),
        item->data(kRoleName).toString(), &ok);
    if (!ok || name.isEmpty())
        return;
    item->setData(kRoleName, name);
    item->setText(name);
    persistOrder();   // rewrites the book's list with the new name
    m_bookmarkList->viewport()->update();
}

void BookmarkDialog::deleteCheckedBookmarks()
{
    const auto checked = checkedItems(m_bookmarkList);
    if (checked.isEmpty()) {
        FramelessDialog::information(
            this, QString::fromUtf8("删除书签"),
            QString::fromUtf8("请先点选要删除的书签。"));
        return;
    }
    if (!FramelessDialog::question(
            this, QString::fromUtf8("删除书签"),
            QString::fromUtf8("确定删除选中的 %1 个书签吗？")
                .arg(checked.size())))
        return;
    for (QListWidgetItem* item : checked)
        delete m_bookmarkList->takeItem(m_bookmarkList->row(item));
    persistOrder();
    if (m_bookmarkList->count() == 0)
        showGroups();   // group vanished with its last bookmark
}

void BookmarkDialog::persistOrder()
{
    if (m_currentBook.isEmpty())
        return;
    QList<Bookmark> ordered;
    for (int i = 0; i < m_bookmarkList->count(); ++i) {
        const QListWidgetItem* item = m_bookmarkList->item(i);
        Bookmark bm;
        bm.bookPath = item->data(kRoleBookPath).toString();
        bm.page = item->data(kRolePage).toInt();
        bm.name = item->data(kRoleName).toString();
        bm.createdAt = item->data(kRoleCreated).toDateTime();
        ordered << bm;
    }
    BookmarkStore::setForBook(m_currentBook, ordered);
}

} // namespace Genesis
