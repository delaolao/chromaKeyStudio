#pragma once

#include <QImage>
#include <QWidget>

// 预览控件: 棋盘格/纯色底上画 RGBA 图像
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    enum Mode { Checker, Solid, Matte };

    explicit PreviewWidget(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void clearImage();
    void setMode(Mode m, const QColor& solid = QColor(30, 90, 200));
    void setPlaceholder(const QString& text);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QRect targetRect() const;
    QImage image_;
    Mode mode_ = Checker;
    QColor solid_{30, 90, 200};
    QString placeholder_;
    QPixmap tile_;
    QImage matteImage_;
};
