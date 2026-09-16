#include "ExcerptDialog.h"

#include "../library/ExcerptStore.h"
#include "../stats/SegmentedToggle.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace Genesis {

namespace {
constexpr int kSwatchSize = 24;

// A plain coloured square that draws a ring when it is the chosen colour.
class Swatch : public QWidget {
public:
    Swatch(const QColor& color, QWidget* parent)
        : QWidget(parent)
        , m_color(color)
    {
        setFixedSize(kSwatchSize, kSwatchSize);
        setCursor(Qt::PointingHandCursor);
    }

    QColor color() const { return m_color; }
    void setSelected(bool on)
    {
        if (on == m_selected)
            return;
        m_selected = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRect r = rect().adjusted(3, 3, -3, -3);
        p.setPen(m_selected ? QPen(QColor("#33CCFF"), 2) : QPen(QColor("#D0D7DE"), 1));
        p.setBrush(m_color);
        // White is hard to see on a white dialog; give it a visible edge.
        p.drawRoundedRect(r, 4, 4);
    }

private:
    QColor m_color;
    bool m_selected = false;
};

QPushButton* themedButton(const QString& text, bool primary, QWidget* parent)
{
    QPushButton* b = new QPushButton(text, parent);
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumWidth(84);
    if (primary) {
        b->setStyleSheet(
            "QPushButton { background-color: #33CCFF; color: #FFFFFF;"
            "  border: none; border-radius: 4px; padding: 6px 18px; }"
            "QPushButton:hover { background-color: #2AB8E8; }"
            "QPushButton:pressed { background-color: #229FCC; }");
    } else {
        b->setStyleSheet(
            "QPushButton { background-color: #FFFFFF; color: #33CCFF;"
            "  border: 1px solid #CCEEFF; border-radius: 4px;"
            "  padding: 6px 18px; }"
            "QPushButton:hover { background-color: #E6F7FF; }"
            "QPushButton:pressed { background-color: #CCEEFF; }");
    }
    return b;
}
} // namespace

ExcerptDialog::ExcerptDialog(const QString& text, const QColor& color,
                             HAlign hAlign, VAlign vAlign, QWidget* parent)
    : FramelessDialog(QString::fromUtf8("书摘"), parent)
    , m_color(color.isValid() ? color : ExcerptStore::defaultColor())
{
    // --- the text ---
    m_edit = new QPlainTextEdit(this);
    m_edit->setPlainText(text);
    m_edit->setPlaceholderText(QString::fromUtf8("输入书摘…"));
    m_edit->setFixedHeight(120);
    m_edit->setStyleSheet(
        "QPlainTextEdit { background: #FFFFFF; color: #333333;"
        "  border: 1px solid #CCEEFF; border-radius: 6px; padding: 6px;"
        "  font-size: 14px;"
        "  font-family: 'KaiTi', 'STKaiti', '楷体', serif; }");
    contentLayout()->addWidget(m_edit);

    // --- the six colours ---
    QLabel* colourLabel = new QLabel(QString::fromUtf8("文字颜色"), this);
    colourLabel->setStyleSheet("color: #888888; font-size: 12px;");
    contentLayout()->addWidget(colourLabel);

    QHBoxLayout* swatchRow = new QHBoxLayout;
    swatchRow->setContentsMargins(0, 0, 0, 0);
    swatchRow->setSpacing(8);
    for (const QColor& c : ExcerptStore::palette()) {
        auto* sw = new Swatch(c, this);
        // Clicking picks the colour; the row is repainted to show the choice.
        sw->installEventFilter(this);
        m_swatches << sw;
        swatchRow->addWidget(sw);
    }
    swatchRow->addStretch();
    contentLayout()->addLayout(swatchRow);
    refreshSwatches();

    // --- where the excerpt sits in the panel ---
    // Two SegmentedToggle strips: the app's shared "pick one of N" control.
    // Both are horizontal, so 上/中/下 read left-to-right here even though the
    // panel stacks them vertically.
    const QString labelCss = "color: #888888; font-size: 12px;";

    QHBoxLayout* hRow = new QHBoxLayout;
    hRow->setContentsMargins(0, 0, 0, 0);
    hRow->setSpacing(8);
    QLabel* hLabel = new QLabel(QString::fromUtf8("水平位置"), this);
    hLabel->setStyleSheet(labelCss);
    m_hAlign = new SegmentedToggle(
        {QString::fromUtf8("左"), QString::fromUtf8("中"),
         QString::fromUtf8("右")},
        this);
    // setCurrentIndex does not emit changed(), so seeding the stored choice
    // cannot look like a user edit.
    m_hAlign->setCurrentIndex(static_cast<int>(hAlign));
    hRow->addWidget(hLabel);
    hRow->addWidget(m_hAlign);
    hRow->addStretch();
    contentLayout()->addLayout(hRow);

    QHBoxLayout* vRow = new QHBoxLayout;
    vRow->setContentsMargins(0, 0, 0, 0);
    vRow->setSpacing(8);
    QLabel* vLabel = new QLabel(QString::fromUtf8("垂直位置"), this);
    vLabel->setStyleSheet(labelCss);
    m_vAlign = new SegmentedToggle(
        {QString::fromUtf8("上"), QString::fromUtf8("中"),
         QString::fromUtf8("下")},
        this);
    m_vAlign->setCurrentIndex(static_cast<int>(vAlign));
    vRow->addWidget(vLabel);
    vRow->addWidget(m_vAlign);
    vRow->addStretch();
    contentLayout()->addLayout(vRow);

    // --- buttons ---
    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addStretch();
    QPushButton* cancel = themedButton(QString::fromUtf8("取消"), false, this);
    QPushButton* ok = themedButton(QString::fromUtf8("确定"), true, this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    contentLayout()->addSpacing(4);
    contentLayout()->addLayout(buttons);

    m_edit->setFocus();
    m_edit->moveCursor(QTextCursor::End);
}

QString ExcerptDialog::text() const
{
    return m_edit ? m_edit->toPlainText().trimmed() : QString();
}

HAlign ExcerptDialog::hAlign() const
{
    return static_cast<HAlign>(m_hAlign->currentIndex());
}

VAlign ExcerptDialog::vAlign() const
{
    return static_cast<VAlign>(m_vAlign->currentIndex());
}

void ExcerptDialog::refreshSwatches()
{
    for (QWidget* w : std::as_const(m_swatches)) {
        if (auto* sw = static_cast<Swatch*>(w))
            sw->setSelected(sw->color().name() == m_color.name());
    }
}

bool ExcerptDialog::eventFilter(QObject* watched, QEvent* event)
{
    // A swatch is picked on mouse RELEASE inside it, matching the app's other
    // custom widgets.
    if (event->type() == QEvent::MouseButtonRelease) {
        if (auto* sw = static_cast<Swatch*>(watched)) {
            m_color = sw->color();
            refreshSwatches();
            return true;
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

} // namespace Genesis
