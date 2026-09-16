#include "BackgroundDialog.h"
#include "BackgroundWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>

namespace Genesis {

namespace {

// Same look as FramelessDialog's message-box buttons.
QPushButton* makeButton(const QString& text, bool primary, QWidget* parent)
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

} // namespace

BackgroundDialog::BackgroundDialog(BackgroundWidget* target, QWidget* parent)
    : FramelessDialog(QString::fromUtf8("背景设置"), parent)
    , m_target(target)
    , m_pathLabel(nullptr)
    , m_slider(nullptr)
    , m_oldPath(target->imagePath())
    , m_oldOpacity(target->imageOpacity())
{
    QVBoxLayout* layout = contentLayout();

    // --- image row: current file name + choose / clear buttons ---
    QLabel* imgTitle = new QLabel(QString::fromUtf8("背景图片"), this);
    imgTitle->setStyleSheet("color: #333333; font-size: 13px; font-weight: bold;");
    layout->addWidget(imgTitle);

    QHBoxLayout* imgRow = new QHBoxLayout;
    imgRow->setSpacing(8);
    m_pathLabel = new QLabel(this);
    m_pathLabel->setStyleSheet("color: #888888; font-size: 12px;");
    m_pathLabel->setMinimumWidth(160);
    imgRow->addWidget(m_pathLabel, 1);

    QPushButton* choose = makeButton(QString::fromUtf8("选择图片…"), false, this);
    connect(choose, &QPushButton::clicked, this, &BackgroundDialog::chooseImage);
    imgRow->addWidget(choose);

    QPushButton* clear = makeButton(QString::fromUtf8("清除"), false, this);
    connect(clear, &QPushButton::clicked, this, &BackgroundDialog::clearImage);
    imgRow->addWidget(clear);
    layout->addLayout(imgRow);

    // --- opacity row: slider + live percentage ---
    QLabel* opTitle = new QLabel(QString::fromUtf8("背景透明度"), this);
    opTitle->setStyleSheet("color: #333333; font-size: 13px; font-weight: bold;");
    layout->addSpacing(6);
    layout->addWidget(opTitle);

    QHBoxLayout* opRow = new QHBoxLayout;
    opRow->setSpacing(10);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 100);
    m_slider->setValue(qRound(m_oldOpacity * 100));
    m_slider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #E6F7FF;"
        "  border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #33CCFF; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0;"
        "  background: #33CCFF; border-radius: 7px; }");
    opRow->addWidget(m_slider, 1);

    QLabel* percent = new QLabel(this);
    percent->setStyleSheet("color: #888888; font-size: 12px;");
    percent->setFixedWidth(38);
    percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    percent->setText(QStringLiteral("%1%").arg(m_slider->value()));
    opRow->addWidget(percent);
    layout->addLayout(opRow);

    // Live preview: apply opacity to the target as the slider moves.
    connect(m_slider, &QSlider::valueChanged, this, [this, percent](int v) {
        percent->setText(QStringLiteral("%1%").arg(v));
        m_target->setImageOpacity(v / 100.0);
    });

    // --- bottom buttons ---
    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addStretch();
    QPushButton* cancel = makeButton(QString::fromUtf8("取消"), false, this);
    QPushButton* ok     = makeButton(QString::fromUtf8("确定"), true, this);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addSpacing(8);
    layout->addLayout(buttons);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    // Cancel (or the close button) rolls the live preview back.
    connect(this, &QDialog::rejected, this, [this]() {
        m_target->setImagePath(m_oldPath);
        m_target->setImageOpacity(m_oldOpacity);
    });

    updatePathLabel();
}

void BackgroundDialog::chooseImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QString::fromUtf8("选择背景图片"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        QString::fromUtf8("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (path.isEmpty())
        return;
    m_target->setImagePath(path);   // live preview
    updatePathLabel();
}

void BackgroundDialog::clearImage()
{
    m_target->setImagePath(QString());
    updatePathLabel();
}

void BackgroundDialog::updatePathLabel()
{
    const QString path = m_target->imagePath();
    if (path.isEmpty()) {
        m_pathLabel->setText(QString::fromUtf8("未设置"));
        m_pathLabel->setToolTip(QString());
    } else {
        m_pathLabel->setText(QFileInfo(path).fileName());
        m_pathLabel->setToolTip(path);
    }
}

} // namespace Genesis
