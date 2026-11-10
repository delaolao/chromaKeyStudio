#include "themeswitch.h"

#include <QKeyEvent>

#include <cmath>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "theme.h"

namespace {
// 与 MyPassword 的 .theme-quick-btn 一致: 29x27, 间距 2, 容器内边距 3
constexpr int kW = 29;
constexpr int kH = 27;
constexpr int kGap = 2;
constexpr int kPad = 3;
constexpr double kPi = 3.14159265358979323846;
}  // namespace

ThemeSwitch::ThemeSwitch(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("主题：跟随系统 / 浅色 / 深色（← → 切换）"));
    setAccessibleName(QStringLiteral("主题模式"));
    setFixedSize(kPad * 2 + kW * 3 + kGap * 2 + 2, kPad * 2 + kH + 2);
    setMouseTracking(true);
}

QSize ThemeSwitch::sizeHint() const { return size(); }

void ThemeSwitch::setMode(int mode) {
    mode = qBound(0, mode, 2);
    if (mode_ == mode) return;
    mode_ = mode;
    update();
    emit modeChanged(mode_);
}

QRect ThemeSwitch::segmentRect(int i) const {
    return QRect(1 + kPad + i * (kW + kGap), 1 + kPad, kW, kH);
}

int ThemeSwitch::indexAt(const QPoint& pos) const {
    for (int i = 0; i < 3; ++i)
        if (segmentRect(i).contains(pos)) return i;
    return -1;
}

void ThemeSwitch::paintEvent(QPaintEvent*) {
    const auto& t = theme::tokens(theme::systemIsDark());
    const QColor accent(t.accent);
    const QColor border(t.borderSoft);
    const QColor chip(t.panelAlt);
    const QColor idle(t.muted);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 容器
    const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath bg;
    bg.addRoundedRect(outer, 10, 10);
    p.fillPath(bg, chip);
    p.setPen(QPen(border, 1));
    p.drawPath(bg);

    // 三个分段
    for (int i = 0; i < 3; ++i) {
        const QRectF r = QRectF(segmentRect(i)).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath seg;
        seg.addRoundedRect(r, 7, 7);

        const bool active = (i == mode_);
        if (active) {
            QColor soft(accent);
            soft.setAlpha(46);
            p.fillPath(seg, soft);
            QColor line(accent);
            line.setAlpha(110);
            p.setPen(QPen(line, 1));
            p.drawPath(seg);
        } else if (i == hovered_) {
            QColor hov(accent);
            hov.setAlpha(20);
            p.fillPath(seg, hov);
        }

        const QRectF glyphBox(r.center().x() - 7.5, r.center().y() - 7.5, 15, 15);
        drawGlyph(p, i, glyphBox, active ? accent : idle);
    }

    // 键盘焦点环
    if (focused_) {
        QColor ring(accent);
        ring.setAlpha(150);
        p.setPen(QPen(ring, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 9, 9);
    }
}

void ThemeSwitch::drawGlyph(QPainter& p, int index, const QRectF& r,
                            const QColor& color) const {
    QPen pen(color, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const QPointF c = r.center();
    if (index == 0) {
        // 显示器
        const QRectF screen(c.x() - 5.5, c.y() - 5.0, 11, 8);
        p.drawRoundedRect(screen, 1.5, 1.5);
        p.drawLine(QPointF(c.x(), c.y() + 3.0), QPointF(c.x(), c.y() + 5.2));
        p.drawLine(QPointF(c.x() - 2.6, c.y() + 5.2), QPointF(c.x() + 2.6, c.y() + 5.2));
    } else if (index == 1) {
        // 太阳
        p.setBrush(color);
        p.drawEllipse(c, 3.1, 3.1);
        p.setBrush(Qt::NoBrush);
        for (int k = 0; k < 8; ++k) {
            const double a = k * kPi / 4.0;
            p.drawLine(QPointF(c.x() + std::cos(a) * 5.2, c.y() + std::sin(a) * 5.2),
                       QPointF(c.x() + std::cos(a) * 6.8, c.y() + std::sin(a) * 6.8));
        }
    } else {
        // 月亮: 大圆减掉偏移的圆
        QPainterPath moon;
        moon.addEllipse(c, 5.6, 5.6);
        QPainterPath cut;
        cut.addEllipse(QPointF(c.x() + 3.4, c.y() - 1.8), 4.9, 4.9);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPath(moon.subtracted(cut));
    }
}

void ThemeSwitch::mouseMoveEvent(QMouseEvent* e) {
    const int i = indexAt(e->pos());
    if (i != hovered_) {
        hovered_ = i;
        setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void ThemeSwitch::leaveEvent(QEvent* e) {
    hovered_ = -1;
    update();
    QWidget::leaveEvent(e);
}

void ThemeSwitch::mousePressEvent(QMouseEvent* e) {
    const int i = indexAt(e->pos());
    if (i >= 0) {
        setFocus(Qt::MouseFocusReason);
        setMode(i);
    }
    QWidget::mousePressEvent(e);
}

void ThemeSwitch::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Left:
            setMode(mode_ == 0 ? 2 : mode_ - 1);
            return;
        case Qt::Key_Right:
            setMode((mode_ + 1) % 3);
            return;
        case Qt::Key_Home:
            setMode(0);
            return;
        case Qt::Key_End:
            setMode(2);
            return;
        default:
            break;
    }
    QWidget::keyPressEvent(e);
}

void ThemeSwitch::focusInEvent(QFocusEvent* e) {
    focused_ = true;
    update();
    QWidget::focusInEvent(e);
}

void ThemeSwitch::focusOutEvent(QFocusEvent* e) {
    focused_ = false;
    update();
    QWidget::focusOutEvent(e);
}
