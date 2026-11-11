#include "previewwidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include <cmath>

namespace {

QColor colorForMode(ck::MaskMode m) {
    switch (m) {
        case ck::MaskMode::HoldOut: return QColor(47, 210, 122);   // 绿: 保留
        case ck::MaskMode::FillKey: return QColor(77, 157, 255);   // 蓝: 填键色
        default: return QColor(255, 91, 91);                       // 红: 透明
    }
}

constexpr int kVertexHitPx = 9;

bool pointInPolygon(const QPointF& pt, const std::vector<ck::MaskPoint>& poly) {
    bool inside = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = poly[i].x, yi = poly[i].y;
        const double xj = poly[j].x, yj = poly[j].y;
        if (((yi > pt.y()) != (yj > pt.y())) &&
            (pt.x() < (xj - xi) * (pt.y() - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

}  // namespace

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 270);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
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

// ---------------------------------------------------------------- 遮罩编辑

void PreviewWidget::attachMask(std::vector<ck::MaskPolygon>* polys) {
    polys_ = polys;
    selected_ = -1;
    drawing_ = false;
    update();
}

void PreviewWidget::setMaskEditMode(bool on) {
    if (editMode_ == on) return;
    editMode_ = on;
    if (!on) {
        cancelDrawing();
        selected_ = -1;
    }
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    setFocusPolicy(on ? Qt::StrongFocus : Qt::NoFocus);
    if (on) setFocus();
    update();
}

void PreviewWidget::setNewPolygonMode(ck::MaskMode m) {
    newMode_ = m;
    // 有选中块时, 顺手改它的模式
    if (polys_ && selected_ >= 0 && selected_ < static_cast<int>(polys_->size())) {
        (*polys_)[selected_].mode = m;
        emit maskEdited();
        update();
    }
}

void PreviewWidget::setSelectedPolygon(int i) {
    selected_ = i;
    update();
}

void PreviewWidget::cancelDrawing() {
    if (drawing_ && polys_ && selected_ >= 0 &&
        selected_ < static_cast<int>(polys_->size())) {
        // 点数不足的临时多边形直接丢掉
        if ((*polys_)[selected_].points.size() < 3)
            polys_->erase(polys_->begin() + selected_);
        selected_ = -1;
    }
    drawing_ = false;
    dragVertex_ = -1;
    dragPoly_ = -1;
    update();
}

QPointF PreviewWidget::toNorm(const QPoint& widgetPos) const {
    const QRect r = targetRect();
    if (r.isEmpty()) return QPointF(-1, -1);
    return QPointF(double(widgetPos.x() - r.left()) / r.width(),
                   double(widgetPos.y() - r.top()) / r.height());
}

QPoint PreviewWidget::toWidget(const ck::MaskPoint& p) const {
    const QRect r = targetRect();
    return QPoint(r.left() + int(std::lround(p.x * r.width())),
                  r.top() + int(std::lround(p.y * r.height())));
}

int PreviewWidget::hitVertex(const QPoint& pos, int* polyIndex) const {
    if (!polys_) return -1;
    for (int i = static_cast<int>(polys_->size()) - 1; i >= 0; --i) {
        const auto& pts = (*polys_)[i].points;
        for (size_t k = 0; k < pts.size(); ++k) {
            const QPoint w = toWidget(pts[k]);
            if ((w - pos).manhattanLength() <= kVertexHitPx * 2) {
                if (polyIndex) *polyIndex = i;
                return static_cast<int>(k);
            }
        }
    }
    return -1;
}

int PreviewWidget::hitPolygon(const QPoint& pos) const {
    if (!polys_) return -1;
    const QPointF n = toNorm(pos);
    for (int i = static_cast<int>(polys_->size()) - 1; i >= 0; --i)
        if (pointInPolygon(n, (*polys_)[i].points)) return i;
    return -1;
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

    drawOverlay(p);
}

void PreviewWidget::drawOverlay(QPainter& p) {
    if (!editMode_) return;
    const QRect r = targetRect();
    if (r.isEmpty()) return;

    // 编辑时压暗一点, 让轮廓更清楚
    p.fillRect(r, QColor(0, 0, 0, 28));

    p.setRenderHint(QPainter::Antialiasing, true);
    if (polys_) {
        for (size_t i = 0; i < polys_->size(); ++i) {
            const auto& poly = (*polys_)[i];
            if (poly.points.size() < 2) continue;
            const bool isSel = (static_cast<int>(i) == selected_);
            const QColor c = colorForMode(poly.mode);

            QPolygonF qp;
            for (const auto& pt : poly.points) qp << toWidget(pt);

            QColor fill = c;
            fill.setAlpha(isSel ? 70 : 40);
            p.setBrush(fill);
            QPen pen(c, isSel ? 2.0 : 1.4);
            if (!isSel) pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            if (poly.closed && poly.points.size() >= 3)
                p.drawPolygon(qp);
            else
                p.drawPolyline(qp);

            // 顶点
            for (const auto& pt : poly.points) {
                const QPoint w = toWidget(pt);
                p.setPen(QPen(Qt::white, 1.2));
                p.setBrush(c);
                p.drawRect(QRect(w.x() - 3, w.y() - 3, 6, 6));
            }
        }
    }

    // 橡皮筋线
    if (drawing_ && polys_ && selected_ >= 0 && hasCursor_) {
        const auto& pts = (*polys_)[selected_].points;
        if (!pts.empty()) {
            const QPoint last = toWidget(pts.back());
            const QRect tr = targetRect();
            const QPoint cur(tr.left() + int(cursorNorm_.x() * tr.width()),
                             tr.top() + int(cursorNorm_.y() * tr.height()));
            p.setPen(QPen(QColor(255, 255, 255, 200), 1.0, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawLine(last, cur);
        }
    }

    // 操作提示
    const QString tip =
        drawing_ ? QStringLiteral("单击加点 · 双击/回车闭合 · Backspace 退一点 · Esc 取消")
                 : QStringLiteral("单击加点画新块 · 拖顶点调整 · 拖内部整体移动 · Delete 删除");
    QFont f = p.font();
    f.setPointSize(9);
    p.setFont(f);
    const QRect box = p.fontMetrics().boundingRect(tip).adjusted(-10, -5, 10, 5);
    QRect tb = box;
    tb.moveTopLeft(QPoint(r.left() + 8, r.top() + 8));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 150));
    p.drawRoundedRect(tb, 6, 6);
    p.setPen(QColor(255, 255, 255, 235));
    p.drawText(tb, Qt::AlignCenter, tip);
}

void PreviewWidget::mousePressEvent(QMouseEvent* e) {
    if (!editMode_ || !polys_ || image_.isNull()) {
        QWidget::mousePressEvent(e);
        return;
    }
    setFocus();
    const QPoint pos = e->pos();

    if (e->button() == Qt::RightButton) {
        // 右键: 退掉最后一个点
        if (drawing_ && selected_ >= 0) {
            emit maskEditStarted();
            auto& pts = (*polys_)[selected_].points;
            if (!pts.empty()) pts.pop_back();
            if (pts.empty()) {
                polys_->erase(polys_->begin() + selected_);
                selected_ = -1;
                drawing_ = false;
            }
            emit maskEdited();
            update();
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    if (drawing_) {
        const QPointF n = toNorm(pos);
        if (n.x() >= 0 && n.x() <= 1 && n.y() >= 0 && n.y() <= 1) {
            emit maskEditStarted();
            (*polys_)[selected_].points.push_back({float(n.x()), float(n.y())});
            emit maskEdited();
            update();
        }
        return;
    }

    // 先看有没有点中顶点
    int polyIdx = -1;
    const int v = hitVertex(pos, &polyIdx);
    if (v >= 0) {
        selected_ = polyIdx;
        dragVertex_ = v;
        emit maskEdited();
        update();
        return;
    }

    // 点中已有块的内部 → 选中并准备整体拖动
    const int inside = hitPolygon(pos);
    if (inside >= 0) {
        selected_ = inside;
        dragPoly_ = inside;
        dragAnchor_ = pos;
        update();
        return;
    }

    // 否则新建一块
    const QPointF n = toNorm(pos);
    if (n.x() < 0 || n.x() > 1 || n.y() < 0 || n.y() > 1) return;
    emit maskEditStarted();
    ck::MaskPolygon np;
    np.mode = newMode_;
    np.points.push_back({float(n.x()), float(n.y())});
    polys_->push_back(np);
    selected_ = static_cast<int>(polys_->size()) - 1;
    drawing_ = true;
    emit maskEdited();
    update();
}

void PreviewWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!editMode_ || !polys_) {
        QWidget::mouseMoveEvent(e);
        return;
    }
    cursorNorm_ = toNorm(e->pos());
    hasCursor_ = true;

    if (dragVertex_ >= 0 && selected_ >= 0) {
        QPointF n = toNorm(e->pos());
        n.setX(std::clamp(n.x(), 0.0, 1.0));
        n.setY(std::clamp(n.y(), 0.0, 1.0));
        auto& pts = (*polys_)[selected_].points;
        if (dragVertex_ < static_cast<int>(pts.size())) {
            pts[dragVertex_].x = float(n.x());
            pts[dragVertex_].y = float(n.y());
            emit maskEdited();
        }
    } else if (dragPoly_ >= 0) {
        const QRect r = targetRect();
        if (!r.isEmpty()) {
            const QPoint d = e->pos() - dragAnchor_;
            const double dx = double(d.x()) / r.width();
            const double dy = double(d.y()) / r.height();
            for (auto& pt : (*polys_)[dragPoly_].points) {
                pt.x = std::clamp(pt.x + float(dx), 0.0f, 1.0f);
                pt.y = std::clamp(pt.y + float(dy), 0.0f, 1.0f);
            }
            dragAnchor_ = e->pos();
            emit maskEdited();
        }
    }
    update();
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (dragVertex_ >= 0 || dragPoly_ >= 0) {
        dragVertex_ = -1;
        dragPoly_ = -1;
        emit maskDrawFinished();
    }
    QWidget::mouseReleaseEvent(e);
}

void PreviewWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!editMode_ || !drawing_ || !polys_ || selected_ < 0) {
        QWidget::mouseDoubleClickEvent(e);
        return;
    }
    // 双击闭合(双击会先触发一次 press 加点, 所以这里点数已 >= 3)
    auto& poly = (*polys_)[selected_];
    poly.closed = true;
    if (poly.points.size() >= 3) {
        drawing_ = false;
        emit maskEdited();
        emit maskDrawFinished();
    }
    update();
}

void PreviewWidget::keyPressEvent(QKeyEvent* e) {
    if (!editMode_) {
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
        case Qt::Key_Escape:
            cancelDrawing();
            emit maskEdited();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (drawing_ && polys_ && selected_ >= 0 &&
                (*polys_)[selected_].points.size() >= 3) {
                (*polys_)[selected_].closed = true;
                drawing_ = false;
                emit maskEdited();
                emit maskDrawFinished();
                update();
            }
            return;
        case Qt::Key_Backspace:
            if (drawing_ && polys_ && selected_ >= 0) {
                emit maskEditStarted();
                auto& pts = (*polys_)[selected_].points;
                if (!pts.empty()) pts.pop_back();
                if (pts.empty()) {
                    polys_->erase(polys_->begin() + selected_);
                    selected_ = -1;
                    drawing_ = false;
                }
                emit maskEdited();
                update();
            }
            return;
        case Qt::Key_Delete:
            if (!drawing_ && polys_ && selected_ >= 0 &&
                selected_ < static_cast<int>(polys_->size())) {
                emit maskEditStarted();
                polys_->erase(polys_->begin() + selected_);
                selected_ = -1;
                emit maskEdited();
                emit maskDrawFinished();
                update();
            }
            return;
        default:
            break;
    }
    QWidget::keyPressEvent(e);
}

void PreviewWidget::leaveEvent(QEvent* e) {
    hasCursor_ = false;
    update();
    QWidget::leaveEvent(e);
}
