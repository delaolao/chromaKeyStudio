#include "previewwidget.h"

#include <QPainter>
#include <QPixmap>

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 270);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setPlaceholder(QStringLiteral("点左上角「打开视频…」，或把视频文件拖进来"));

    // 棋盘格纹理只画一次
    const int cell = 12;
    tile_ = QPixmap(cell * 2, cell * 2);
    tile_.fill(QColor(200, 200, 200));
    QPainter p(&tile_);
    p.fillRect(0, 0, cell, cell, QColor(154, 154, 154));
    p.fillRect(cell, cell, cell, cell, QColor(154, 154, 154));
    p.end();
}

void PreviewWidget::setImage(const QImage& img) {
    image_ = img;
    if (mode_ == Matte) {
        // 遮罩模式: alpha 当灰度铺出来
        matteImage_ = image_.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < matteImage_.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(matteImage_.scanLine(y));
            for (int x = 0; x < matteImage_.width(); ++x) {
                const int a = qAlpha(line[x]);
                line[x] = qRgb(a, a, a);
            }
        }
    }
    update();
}

void PreviewWidget::clearImage() {
    image_ = QImage();
    update();
}

void PreviewWidget::setMode(Mode m, const QColor& solid) {
    mode_ = m;
    solid_ = solid;
    if (image_.isNull()) {
        update();
        return;
    }
    if (m == Matte) setImage(image_);
    update();
}

void PreviewWidget::setPlaceholder(const QString& text) {
    placeholder_ = text;
    update();
}

QRect PreviewWidget::targetRect() const {
    const QSize src = image_.size();
    if (src.isEmpty()) return QRect();
    const double s = std::min(static_cast<double>(width()) / src.width(),
                              static_cast<double>(height()) / src.height());
    const int w = static_cast<int>(src.width() * s);
    const int h = static_cast<int>(src.height() * s);
    return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    switch (mode_) {
        case Solid: p.fillRect(rect(), solid_); break;
        case Matte: p.fillRect(rect(), QColor(24, 24, 24)); break;
        default: p.fillRect(rect(), QBrush(tile_)); break;
    }

    if (image_.isNull()) {
        QFont f = p.font();
        f.setPointSize(f.pointSize() + 3);
        p.setFont(f);
        const QRect box = p.fontMetrics().boundingRect(placeholder_)
                              .adjusted(-18, -9, 18, 9);
        QRect r = box;
        r.moveCenter(rect().center());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 225));
        p.drawRoundedRect(r, 10, 10);
        p.setPen(QColor(60, 62, 68));
        p.drawText(r, Qt::AlignCenter, placeholder_);
        return;
    }

    const QRect target = targetRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (mode_ == Matte && !matteImage_.isNull())
        p.drawImage(target, matteImage_);
    else
        p.drawImage(target, image_);
    p.setPen(QColor(90, 90, 90));
    p.drawRect(target.adjusted(0, 0, -1, -1));
}
