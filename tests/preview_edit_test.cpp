// 预览控件的遮罩交互测试(无头): 用合成鼠标事件走一遍
//  单击加点 -> 双击闭合 -> 拖顶点 -> 删除
// 需要 offscreen 平台: 运行时加 -platform offscreen
#include <QApplication>
#include <QFont>
#include <QMouseEvent>
#include <QPoint>
#include <QTest>

#include <cstdio>
#include <vector>

#include "core/mask.h"
#include "ui/previewwidget.h"

namespace {

int g_failed = 0;
void check(bool cond, const char* what) {
    std::printf("  [%s] %s\n", cond ? "ok" : "FAIL", what);
    if (!cond) ++g_failed;
}

void sendClick(QWidget* w, const QPoint& p, Qt::MouseButton b = Qt::LeftButton) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(p), w->mapToGlobal(QPointF(p)), b,
                      b, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &press);
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(p), w->mapToGlobal(QPointF(p)), b,
                    Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &rel);
}

void sendDouble(QWidget* w, const QPoint& p) {
    QMouseEvent ev(QEvent::MouseButtonDblClick, QPointF(p), w->mapToGlobal(QPointF(p)),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

void sendPress(QWidget* w, const QPoint& p) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(p), w->mapToGlobal(QPointF(p)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &press);
}

void sendRelease(QWidget* w, const QPoint& p) {
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(p), w->mapToGlobal(QPointF(p)),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &rel);
}

void sendMove(QWidget* w, const QPoint& from, const QPoint& to) {
    QMouseEvent ev(QEvent::MouseMove, QPointF(to), w->mapToGlobal(QPointF(to)),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    std::vector<ck::MaskPolygon> masks;
    PreviewWidget w;
    w.resize(800, 450);
    w.attachMask(&masks);

    // 100x100 的纯色图, 让 targetRect 稳定
    QImage img(100, 100, QImage::Format_RGBA8888);
    img.fill(QColor(10, 20, 30, 255));
    w.setImage(img);
    w.show();
    QCoreApplication::processEvents();

    w.setMaskEditMode(true);
    check(w.maskEditMode(), "进入编辑模式");

    const QRect r = w.rect();
    // 目标矩形是居中的正方形(800x450 -> 450x450, x 从 175 开始)
    const int side = 450;
    const int x0 = (r.width() - side) / 2;
    const int y0 = (r.height() - side) / 2;

    // ---- 画一个三角形
    sendClick(&w, QPoint(x0 + 50, y0 + 50));
    check(masks.size() == 1, "第一次单击创建了一块");
    check(masks.size() == 1 && masks[0].points.size() == 1, "第一个顶点已加入");

    sendClick(&w, QPoint(x0 + 400, y0 + 50));
    sendClick(&w, QPoint(x0 + 225, y0 + 400));
    check(masks.size() == 1 && masks[0].points.size() == 3, "三个顶点");

    // 双击闭合(双击前会先有一次 press, 实际会加点, 这里直接发 dblclick)
    sendDouble(&w, QPoint(x0 + 225, y0 + 400));
    check(masks.size() == 1 && masks[0].closed, "双击后闭合");

    // ---- 拖顶点: 第一个顶点向右移
    const QPoint v0(x0 + 50, y0 + 50);
    sendPress(&w, v0);                              // 按住顶点
    sendMove(&w, v0, QPoint(x0 + 120, y0 + 60));    // 拖
    sendRelease(&w, QPoint(x0 + 120, y0 + 60));     // 松开
    const double nx = masks[0].points[0].x;
    check(nx > 0.13 && nx < 0.30, "顶点被拖动(归一化 x 变化)");

    // ---- Delete 删除选中块
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QCoreApplication::sendEvent(&w, &del);
    check(masks.empty(), "Delete 删除了整块");

    // ---- Esc 取消未闭合的多边形
    sendClick(&w, QPoint(x0 + 100, y0 + 100));
    sendClick(&w, QPoint(x0 + 200, y0 + 100));
    check(masks.size() == 1 && masks[0].points.size() == 2, "半成品有 2 个点");
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&w, &esc);
    check(masks.empty(), "Esc 丢弃了不足 3 点的多边形");

    // ---- 强制保留模式
    w.setMaskEditMode(false);
    w.setNewPolygonMode(ck::MaskMode::HoldOut);
    w.setMaskEditMode(true);
    sendClick(&w, QPoint(x0 + 100, y0 + 100));
    sendClick(&w, QPoint(x0 + 300, y0 + 100));
    sendClick(&w, QPoint(x0 + 300, y0 + 300));
    sendDouble(&w, QPoint(x0 + 300, y0 + 300));
    check(masks.size() == 1 && masks[0].mode == ck::MaskMode::HoldOut,
          "新块使用当前选择的模式");

    // 存一张叠加效果图(仅用于人工核对)
    {
        std::vector<ck::MaskPolygon> demo;
        ck::MaskPolygon p;
        p.mode = ck::MaskMode::Transparent;
        p.points = {{0.05f, 0.05f}, {0.95f, 0.05f}, {0.95f, 0.22f}, {0.5f, 0.3f},
                    {0.05f, 0.22f}};
        demo.push_back(p);
        ck::MaskPolygon q;
        q.mode = ck::MaskMode::HoldOut;
        q.points = {{0.62f, 0.55f}, {0.9f, 0.55f}, {0.9f, 0.85f}, {0.62f, 0.85f}};
        demo.push_back(q);
        w.attachMask(&demo);
        w.setMaskEditMode(true);
        w.setSelectedPolygon(0);
        QCoreApplication::processEvents();
        w.grab().save(QStringLiteral("overlay_preview.png"));
        std::printf("  已保存 overlay_preview.png\n");
    }

    std::printf("\n%s\n", g_failed == 0 ? "结论: 全部通过" : "结论: 有失败项");
    return g_failed == 0 ? 0 : 1;
}
