#pragma once

#include <QImage>
#include <QWidget>

#include <vector>

#include "core/mask.h"

// 预览控件: 棋盘格/纯色底上画 RGBA 图像, 并支持多边形遮罩的编辑。
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    enum Mode { Checker, Solid, Matte };

    explicit PreviewWidget(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void clearImage();
    void setMode(Mode m, const QColor& solid = QColor(30, 90, 200));
    void setPlaceholder(const QString& text);

    // ---- 遮罩编辑
    // 不持有数据: 直接编辑 MainWindow 里的那份, 改完发 maskEdited()
    void attachMask(std::vector<ck::MaskPolygon>* polys);
    void setMaskEditMode(bool on);
    bool maskEditMode() const { return editMode_; }
    void setNewPolygonMode(ck::MaskMode m);
    int selectedPolygon() const { return selected_; }
    void setSelectedPolygon(int i);
    void cancelDrawing();

signals:
    void maskEdited();        // 多边形或选中项变了
    void maskDrawFinished();  // 画完一块(用于自动落盘)
    void maskEditStarted();   // 即将发生修改(用于撤销快照)

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QRect targetRect() const;
    QPointF toNorm(const QPoint& widgetPos) const;
    QPoint toWidget(const ck::MaskPoint& p) const;
    int hitVertex(const QPoint& pos, int* polyIndex) const;
    int hitPolygon(const QPoint& pos) const;
    void drawOverlay(QPainter& p);

    QImage image_;
    Mode mode_ = Checker;
    QColor solid_{30, 90, 200};
    QString placeholder_;
    QPixmap tile_;
    QImage matteImage_;

    std::vector<ck::MaskPolygon>* polys_ = nullptr;
    bool editMode_ = false;
    ck::MaskMode newMode_ = ck::MaskMode::Transparent;
    int selected_ = -1;    // 选中/正在编辑的多边形下标
    bool drawing_ = false; // 正在拉新多边形
    int dragVertex_ = -1;  // 正在拖的顶点
    int dragPoly_ = -1;    // 正在整体拖动的多边形
    QPoint dragAnchor_;
    QPointF cursorNorm_{-1, -1};
    bool hasCursor_ = false;
};
