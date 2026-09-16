#include "TitleBar.h"
#include "WindowButton.h"
#include "PinButton.h"
#include "TitleMenuItem.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QIcon>
#include <QPixmap>
#include <QWindow>
#include <QtMath>

namespace Genesis {

namespace {
// Title-bar height as a fraction of the window height, anchored at the
// default 760px-tall window: there the bar is the original 36px (scale 1.0,
// full-size 44×34 buttons). Taller windows grow the bar sub-linearly (√ratio).
// Window width drives a SECOND scale factor that shrinks every control as the
// window narrows, so the menu items and buttons ALWAYS fit — QMenuBar's
// overflow "kebab" never appears.
constexpr qreal kBarHeightRatio = 36.0 / 760.0;
constexpr qreal kWidthScaleBase = 800.0;   // window width where w-scale = 1
} // namespace

TitleBar::TitleBar(QWidget* topLevel, QWidget* parent)
    : QWidget(parent)
    , m_win(topLevel)
    , m_layout(nullptr)
    , m_iconLabel(nullptr)
    , m_pinButton(nullptr)
    , m_minButton(nullptr)
    , m_maxButton(nullptr)
    , m_closeButton(nullptr)
    , m_scale(1.0)
    , m_dragging(false)
{
    setObjectName("genesisTitleBar");
    // A QWidget subclass only paints stylesheet background/border when styled
    // backgrounds are enabled — without this the bottom separator won't show.
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "#genesisTitleBar { background-color: #FFFFFF;"
        "  border-bottom: 1px solid #E0E6ED; }");

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(8, 0, 0, 0);
    m_layout->setSpacing(6);

    m_iconLabel = new QLabel(this);
    // Center every item vertically within the bar.
    m_layout->addWidget(m_iconLabel, 0, Qt::AlignVCenter);

    // The menu bar is inserted right after the icon via setMenuBar(); the
    // stretch keeps the window-control buttons pinned to the right.
    m_layout->addStretch();

    // --- window-control buttons (vector-drawn, uniform stroke) ---
    // Pin sits left of minimize: toggles always-on-top and its icon.
    m_pinButton  = new PinButton(this);
    m_minButton  = new WindowButton(WindowButton::Glyph::Minimize, this);
    m_maxButton  = new WindowButton(WindowButton::Glyph::Maximize, this);
    m_closeButton = new WindowButton(WindowButton::Glyph::Close, this);

    m_pinButton->setToolTip(QString::fromUtf8("窗口置顶"));
    m_minButton->setToolTip(QString::fromUtf8("最小化"));
    m_maxButton->setToolTip(QString::fromUtf8("最大化"));
    m_closeButton->setToolTip(QString::fromUtf8("关闭"));

    // Buttons scale with the window and stay vertically centered whatever
    // the resulting bar height is.
    m_layout->addWidget(m_pinButton,   0, Qt::AlignVCenter);
    m_layout->addWidget(m_minButton,   0, Qt::AlignVCenter);
    m_layout->addWidget(m_maxButton,   0, Qt::AlignVCenter);
    m_layout->addWidget(m_closeButton, 0, Qt::AlignVCenter);

    connect(m_pinButton, &PinButton::clicked, this, [this]() {
        const bool pinned = !m_pinButton->isPinned();
        m_pinButton->setPinned(pinned);
        m_pinButton->setToolTip(pinned ? QString::fromUtf8("取消置顶")
                                       : QString::fromUtf8("窗口置顶"));
        // Toggle the topmost flag on the native window (QWindow::setFlag).
        // QWidget::setWindowFlag() would hide+re-show the widget to rebuild
        // its window, flashing the whole app; the QWindow path updates the
        // Z-order in place with no visible flicker.
        if (QWindow* win = m_win->windowHandle())
            win->setFlag(Qt::WindowStaysOnTopHint, pinned);
    });

    connect(m_minButton,   &WindowButton::clicked, this, [this]() { m_win->showMinimized(); });
    connect(m_maxButton,   &WindowButton::clicked, this, &TitleBar::toggleMaximize);
    connect(m_closeButton, &WindowButton::clicked, this, [this]() { m_win->close(); });

    updateMaximizeButton(m_win->isMaximized());
}

void TitleBar::updateBarHeight()
{
    // Height scales with the window height (anchored at 760px → 36px) but
    // sub-linearly, so a 2× window doesn't double the bar. Clamp sane bounds.
    const qreal winH = m_win->height();
    const qreal hRatio = qMax(0.0, qreal(winH) / 760.0);
    const qreal hScale = qSqrt(hRatio);
    const int barH = qBound(24, qRound(36.0 * hScale), 56);

    // Buttons shrink as the window narrows, down to a small-but-usable size,
    // so the menu items + buttons always share the bar without Qt collapsing
    // the menu bar into an overflow kebab.
    const qreal wScale = qBound(0.5, qreal(m_win->width()) / kWidthScaleBase,
                                1.0);
    const qreal scale = qMin(hScale, wScale);

    setFixedHeight(barH);
    m_scale = scale;
    m_pinButton->setScale(scale);
    m_minButton->setScale(scale);
    m_maxButton->setScale(scale);
    m_closeButton->setScale(scale);

    // Icon + menu items scale with the controls.
    const int iconPx = qRound(22 * scale);
    if (!m_win->windowIcon().isNull())
        m_iconLabel->setPixmap(m_win->windowIcon().pixmap(iconPx, iconPx));
    for (TitleMenuItem* item : m_menuItems)
        item->setScale(scale);
}

void TitleBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // The window's height drives the bar's height; recompute on every change
    // (first show, window resizes, fullscreen/maximize transitions).
    if (m_win->isVisible())
        updateBarHeight();
}

void TitleBar::setMenuBar(QWidget* menuBar)
{
    if (!menuBar)
        return;
    // Parenting + the layout insertion below are what keep the strip alive;
    // no member needs to hold it.
    menuBar->setParent(this);
    // Collect the strip's custom-drawn items so they scale with the window.
    m_menuItems = menuBar->findChildren<TitleMenuItem*>();
    // Insert right after the icon (index 1): icon | menu | stretch | buttons.
    m_layout->insertWidget(1, menuBar, 0, Qt::AlignVCenter);
    if (isVisible())
        updateBarHeight();
}

void TitleBar::updateMaximizeButton(bool maximized)
{
    m_maxButton->setGlyph(maximized ? WindowButton::Glyph::Restore
                                     : WindowButton::Glyph::Maximize);
    m_maxButton->setToolTip(maximized ? QString::fromUtf8("向下还原")
                                      : QString::fromUtf8("最大化"));
}

void TitleBar::toggleMaximize()
{
    if (m_win->isMaximized())
        m_win->showNormal();
    else
        m_win->showMaximized();
    updateMaximizeButton(m_win->isMaximized());
}

void TitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_win->isMaximized()) {
        m_dragging = true;
        m_dragStartGlobal = event->globalPosition().toPoint();
        m_winStartPos = m_win->frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint delta = event->globalPosition().toPoint() - m_dragStartGlobal;
        m_win->move(m_winStartPos + delta);
    }
    QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        toggleMaximize();
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace Genesis
