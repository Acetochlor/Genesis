#ifndef BACKGROUNDDIALOG_H
#define BACKGROUNDDIALOG_H

#include "../dialogs/FramelessDialog.h"

class QLabel;
class QSlider;

namespace Genesis {

class BackgroundWidget;

// Settings → 背景: pick a local image for the main content background and
// tune its opacity. Changes are applied to the target live for preview;
// Cancel restores what was set when the dialog opened.
class BackgroundDialog : public FramelessDialog {
    Q_OBJECT

public:
    BackgroundDialog(BackgroundWidget* target, QWidget* parent);

private:
    void chooseImage();
    void clearImage();
    void updatePathLabel();

    BackgroundWidget* m_target;
    QLabel* m_pathLabel;
    QSlider* m_slider;

    // State on entry, restored when the dialog is rejected.
    QString m_oldPath;
    qreal m_oldOpacity;
};

} // namespace Genesis

#endif // BACKGROUNDDIALOG_H
