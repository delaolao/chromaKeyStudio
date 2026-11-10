// 主题分段开关: 跟随系统 / 浅色 / 深色。
// 样式参照 MyPassword 的 .theme-quick / .theme-quick-btn(圆角容器 + 三个图标按钮),
// 整块自绘 —— QSS 在 Fusion 风格下对 border-radius 的支持不稳定。
#pragma once

#include <QWidget>

class ThemeSwitch : public QWidget {
    Q_OBJECT

public:
    explicit ThemeSwitch(QWidget* parent = nullptr);

    int mode() const { return mode_; }
    void setMode(int mode);  // 0=跟随系统 1=浅色 2=深色

signals:
    void modeChanged(int mode);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    QSize sizeHint() const override;

private:
    int indexAt(const QPoint& pos) const;
    QRect segmentRect(int i) const;
    void drawGlyph(QPainter& p, int index, const QRectF& r, const QColor& color) const;

    int mode_ = 0;
    int hovered_ = -1;
    bool focused_ = false;
};
