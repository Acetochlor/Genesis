#ifndef BACKGROUNDWIDGET_H
#define BACKGROUNDWIDGET_H

#include <QWidget>
#include <QPixmap>

namespace Genesis {

// The main window's content area. Paints an optional user-chosen background
// image (scaled to cover, centered, cropped) at an adjustable opacity over a
// white base. With no image set it is just a plain white panel.
class BackgroundWidget : public QWidget {
    Q_OBJECT

public:
    explicit BackgroundWidget(QWidget* parent = nullptr);

    // Path of the current background image ("" = none).
    QString imagePath() const { return m_imagePath; }
    // Load (or clear, with an empty path) the background image.
    void setImagePath(const QString& path);

    // Image opacity, 0.0 (invisible) .. 1.0 (fully opaque).
    qreal imageOpacity() const { return m_opacity; }
    void setImageOpacity(qreal opacity);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_imagePath;
    QPixmap m_pixmap;
    qreal m_opacity;
};

} // namespace Genesis

#endif // BACKGROUNDWIDGET_H
