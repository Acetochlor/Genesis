#include "FramelessDialog.h"
#include "../titlebar/WindowButton.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QShowEvent>
#include <QWindow>

namespace Genesis {

namespace {

// The one currently-open standalone dialog, or nullptr. Used for mutual
// exclusion: opening a new standalone dialog closes the previous one.
FramelessDialog* s_activeStandalone = nullptr;

const int kTitleBarHeight = 34;
const int kCornerRadius = 8;   // dialog corner rounding, px

// Themed push button used in the message-box helpers. Primary = filled blue;
// secondary = white with a blue border.
QPushButton* makeThemedButton(const QString& text, bool primary, QWidget* parent)
{
    QPushButton* b = new QPushButton(text, parent);
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumWidth(84);
    if (primary) {
        b->setStyleSheet(
            "QPushButton { background-color: #33CCFF; color: #FFFFFF; border: none;"
            "  border-radius: 4px; padding: 6px 18px; }"
            "QPushButton:hover { background-color: #2AB8E8; }"
            "QPushButton:pressed { background-color: #229FCC; }");
    } else {
        b->setStyleSheet(
            "QPushButton { background-color: #FFFFFF; color: #33CCFF;"
            "  border: 1px solid #CCEEFF; border-radius: 4px; padding: 6px 18px; }"
            "QPushButton:hover { background-color: #E6F7FF; }"
            "QPushButton:pressed { background-color: #CCEEFF; }");
    }
    return b;
}

// Shared skeleton for the information()/question() helpers.
FramelessDialog* makeMessageDialog(QWidget* parent, const QString& title,
                                   const QString& text)
{
    FramelessDialog* dlg = new FramelessDialog(title, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose, false);

    QLabel* body = new QLabel(text, dlg);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->setStyleSheet("color: #333333; font-size: 13px;");
    body->setMinimumWidth(280);
    dlg->contentLayout()->addWidget(body);
    return dlg;
}

} // namespace

FramelessDialog::FramelessDialog(const QString& title, QWidget* parent)
    : FramelessDialog(title, Chrome::Titled, parent)
{
}

FramelessDialog::FramelessDialog(const QString& title, Chrome chrome, QWidget* parent)
    : QDialog(parent)
    , m_contentLayout(nullptr)
    , m_dragging(false)
    , m_followParent(nullptr)
    , m_followOffset(0, 0)
    , m_ratioW(0)
    , m_ratioH(0)
    , m_bottomPad(0)
    , m_leftAligned(false)
    , m_followActive(false)
{
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    // Translucent window so the rounded frame's corners show whatever is
    // behind the dialog instead of opaque black.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setModal(true);

    // 1px themed border with rounded corners around the whole dialog
    // (frameless windows otherwise blend into whatever is behind them).
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QWidget* frame = new QWidget(this);
    frame->setObjectName("dlgFrame");
    frame->setAttribute(Qt::WA_StyledBackground, true);
    frame->setStyleSheet(QString(
        "#dlgFrame { background-color: #FFFFFF;"
        "  border: 1px solid #CCEEFF; border-radius: %1px; }")
        .arg(kCornerRadius));
    rootLayout->addWidget(frame);

    QVBoxLayout* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(1, 1, 1, 1);
    frameLayout->setSpacing(0);

    // --- title bar: title text left, close button right ---
    // CloseOnly keeps the strip (it hosts the close button and stays the drag
    // area) but drops the title text and the bottom separator, so the content
    // appears to start right at the top.
    QWidget* bar = new QWidget(frame);
    bar->setObjectName("dlgTitleBar");
    bar->setFixedHeight(kTitleBarHeight);
    bar->setAttribute(Qt::WA_StyledBackground, true);
    // Top corners follow the frame's rounding so the bar doesn't overpaint it.
    bar->setStyleSheet(QString(
        "#dlgTitleBar { background-color: #FFFFFF;"
        "  %2"
        "  border-top-left-radius: %1px; border-top-right-radius: %1px; }")
        .arg(kCornerRadius - 1)
        .arg(chrome == Chrome::Titled
                 ? QStringLiteral("border-bottom: 1px solid #E0E6ED;")
                 : QString()));

    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(12, 0, 0, 0);
    barLayout->setSpacing(6);

    if (chrome == Chrome::Titled) {
        QLabel* titleLabel = new QLabel(title, bar);
        titleLabel->setStyleSheet(
            "color: #33CCFF; font-size: 13px; font-weight: bold;");
        barLayout->addWidget(titleLabel, 0, Qt::AlignVCenter);
    } else {
        setWindowTitle(title);   // taskbar / accessibility only
    }
    barLayout->addStretch();

    WindowButton* closeBtn = new WindowButton(WindowButton::Glyph::Close, bar);
    closeBtn->setToolTip(QString::fromUtf8("关闭"));
    // The button sits flush against the dialog's rounded top-right corner:
    // round its hover fill there so the red highlight follows the curve.
    closeBtn->setCornerRadius(kCornerRadius - 1, false, true, false, false);
    barLayout->addWidget(closeBtn, 0, Qt::AlignTop);
    connect(closeBtn, &WindowButton::clicked, this, &QDialog::reject);

    frameLayout->addWidget(bar);

    // --- content area ---
    QWidget* content = new QWidget(frame);
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(16, 16, 16, 16);
    m_contentLayout->setSpacing(10);
    frameLayout->addWidget(content, 1);
}

void FramelessDialog::information(QWidget* parent, const QString& title,
                                  const QString& text)
{
    FramelessDialog* dlg = makeMessageDialog(parent, title, text);

    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addStretch();
    QPushButton* ok = makeThemedButton(QString::fromUtf8("确定"), true, dlg);
    buttons->addWidget(ok);
    dlg->contentLayout()->addSpacing(4);
    dlg->contentLayout()->addLayout(buttons);
    QObject::connect(ok, &QPushButton::clicked, dlg, &QDialog::accept);

    dlg->exec();
    dlg->deleteLater();
}

bool FramelessDialog::question(QWidget* parent, const QString& title,
                               const QString& text)
{
    FramelessDialog* dlg = makeMessageDialog(parent, title, text);

    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addStretch();
    QPushButton* no  = makeThemedButton(QString::fromUtf8("取消"), false, dlg);
    QPushButton* yes = makeThemedButton(QString::fromUtf8("确定"), true, dlg);
    buttons->addWidget(no);
    buttons->addWidget(yes);
    dlg->contentLayout()->addSpacing(4);
    dlg->contentLayout()->addLayout(buttons);
    QObject::connect(no,  &QPushButton::clicked, dlg, &QDialog::reject);
    QObject::connect(yes, &QPushButton::clicked, dlg, &QDialog::accept);

    const bool confirmed = (dlg->exec() == QDialog::Accepted);
    dlg->deleteLater();
    return confirmed;
}

void FramelessDialog::centerOverParent()
{
    if (QWidget* w = parentWidget()) {
        const QPoint c = w->window()->frameGeometry().center();
        move(c.x() - width() / 2, c.y() - height() / 2);
    }
}

void FramelessDialog::followParent(const QPoint& offset, qreal wRatio,
                                   qreal hRatio, int bottomPad,
                                   bool leftAligned)
{
    m_followActive = true;
    m_followOffset = offset;
    m_ratioW = wRatio;
    m_ratioH = hRatio;
    m_bottomPad = bottomPad;
    m_leftAligned = leftAligned;
    m_followParent = parentWidget() ? parentWidget()->window()
                                    : static_cast<QWidget*>(parent());
    if (m_followParent) {
        // Follow the parent's moves/resizes; watch the native window too.
        m_followParent->installEventFilter(this);
        if (QWindow* pw = m_followParent->windowHandle())
            pw->installEventFilter(this);
    }
    if (isVisible())
        updateFollow();
}

void FramelessDialog::showStandalone()
{
    // Mutual exclusion: any other standalone dialog is closed first, so at
    // most one is ever open (the new one replaces the old).
    if (FramelessDialog* prev = s_activeStandalone) {
        if (prev != this)
            prev->close();
    }
    s_activeStandalone = this;
    setModal(false);
    // When the active standalone is closed, clear the tracker.
    connect(this, &QDialog::finished, this, [this]() {
        if (s_activeStandalone == this)
            s_activeStandalone = nullptr;
    });
    show();
    // Deliberately NO raise()/activateWindow(): those force the dialog to
    // the system foreground (SetForegroundWindow), covering other apps. An
    // owned window already appears above its own main window via show().
    // Also clear any topmost flag that an owned dialog may have inherited
    // from a pinned (置顶) main window, so it never floats over other apps.
    if (QWindow* w = windowHandle())
        w->setFlag(Qt::WindowStaysOnTopHint, false);
}

void FramelessDialog::updateFollow()
{
    if (!m_followParent)
        return;

    // Live size tracking (only when ratios were given): size = parent ×
    // ratio, minus bottomPad px, so the dialog scales with the parent.
    if (m_ratioW > 0 && m_ratioH > 0) {
        const QSize sz(qRound(m_followParent->width() * m_ratioW),
                       qMax(1, qRound(m_followParent->height() * m_ratioH)
                               - m_bottomPad));
        if (sz != size())
            resize(sz);
    }

    // Position: leftAligned dialogs (Markdown editor only) anchor flush-left
    // to the parent; everything else centers over the parent.
    if (m_leftAligned) {
        const QPoint anchor(m_followParent->mapToGlobal(
            QPoint(0, m_followParent->height() / 2)));
        move(anchor.x() + m_followOffset.x(),
             anchor.y() - height() / 2 + m_followOffset.y());
    } else {
        const QPoint c = m_followParent->mapToGlobal(
            QPoint(m_followParent->width() / 2,
                   m_followParent->height() / 2));
        move(c.x() - width() / 2, c.y() - height() / 2 + m_followOffset.y());
    }
}

bool FramelessDialog::eventFilter(QObject* watched, QEvent* event)
{
    // While pinned, keep centered over the parent whenever it moves/resizes.
    if (m_followActive
        && (event->type() == QEvent::Move || event->type() == QEvent::Resize)
        && (watched == m_followParent
            || (m_followParent && watched == m_followParent->windowHandle())))
        updateFollow();
    return QDialog::eventFilter(watched, event);
}

void FramelessDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (m_followActive && m_followParent)
        updateFollow();
    else
        centerOverParent();
}

void FramelessDialog::mousePressEvent(QMouseEvent* event)
{
    // A pinned dialog cannot be moved by its title bar.
    if (m_followActive)
        return;
    // Drag anywhere in the title-bar strip.
    if (event->button() == Qt::LeftButton
        && event->position().y() <= kTitleBarHeight) {
        m_dragging = true;
        m_dragStartGlobal = event->globalPosition().toPoint();
        m_startPos = pos();
    }
    QDialog::mousePressEvent(event);
}

void FramelessDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
        move(m_startPos + event->globalPosition().toPoint() - m_dragStartGlobal);
    QDialog::mouseMoveEvent(event);
}

void FramelessDialog::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    QDialog::mouseReleaseEvent(event);
}

} // namespace Genesis
