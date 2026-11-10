#include "aboutdialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSvgRenderer>
#include <QUrl>
#include <QVBoxLayout>

#include "theme.h"

namespace {
const char* kRepoUrl = "https://github.com/delaolao/chromaKeyStudio";

// GitHub octocat 标记(SVG path, 16x16 视图框)
const char* kOctocatPath =
    "M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49"
    "-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82"
    ".72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15"
    "-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82a7.4 7.4 0 0 1 2-.27c.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82"
    ".44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2"
    " 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8Z";

QPixmap octocatPixmap(const QColor& color, int logicalSize) {
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    const int px = qMax(1, static_cast<int>(logicalSize * dpr));
    const QString svg = QStringLiteral(
                            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' "
                            "width='16' height='16'><path fill='%1' d='%2'/></svg>")
                            .arg(color.name(), QString::fromLatin1(kOctocatPath));
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&p);
    p.end();
    pm.setDevicePixelRatio(dpr);
    return pm;
}

// 参考开源项目页脚那种小圆角胶囊: 描边 + 图标 + 文字, hover 时变主题绿。
// 背景/描边自己画, 不走 QSS —— QPushButton 的 border-radius 在 Fusion 下不稳定。
class GitHubPill : public QPushButton {
public:
    GitHubPill(const QColor& text, const QColor& accent, const QColor& border,
               const QColor& fill, const QColor& fillHover, QWidget* parent = nullptr)
        : QPushButton(parent),
          text_(text),
          accent_(accent),
          border_(border),
          fill_(fill),
          fillHover_(fillHover) {
        setObjectName(QStringLiteral("githubPill"));
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("在浏览器中打开 GitHub 仓库"));
        setFlat(true);
        setAttribute(Qt::WA_Hover, true);
        // 键盘可达: Tab 能进来, 回车/空格能触发
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(QStringLiteral("GitHub 仓库"));
        setAutoDefault(false);
        setDefault(false);
        QFont f = font();
        f.setPixelSize(11);
        setFont(f);

        const QFontMetrics fm(f);
        const int textW = fm.horizontalAdvance(QStringLiteral("GitHub"));
        setFixedSize(9 + kIcon + 6 + textW + 12, 26);
        icon_ = octocatPixmap(text_, kIcon);
        iconHover_ = octocatPixmap(accent_, kIcon);
        connect(this, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl(QString::fromLatin1(kRepoUrl)));
        });
    }

protected:
    void enterEvent(QEnterEvent* e) override {
        hovered_ = true;
        update();
        QPushButton::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        hovered_ = false;
        update();
        QPushButton::leaveEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            click();
            return;
        }
        QPushButton::keyPressEvent(e);   // 空格由基类处理
    }
    void focusInEvent(QFocusEvent* e) override {
        update();
        QPushButton::focusInEvent(e);
    }
    void focusOutEvent(QFocusEvent* e) override {
        update();
        QPushButton::focusOutEvent(e);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
        p.fillPath(path, hovered_ ? fillHover_ : fill_);
        p.setPen(QPen(hovered_ ? accent_ : border_, 1));
        p.drawPath(path);

        p.drawPixmap(9, (height() - kIcon) / 2, hovered_ ? iconHover_ : icon_);

        QFont f = font();
        p.setFont(f);
        p.setPen(hovered_ ? accent_ : text_);
        const QRect textRect(9 + kIcon + 6, 0, width() - (9 + kIcon + 6) - 12, height());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("GitHub"));

        // 键盘焦点环: 在胶囊外侧再画一圈, 不改变布局
        if (hasFocus()) {
            QColor ring = accent_;
            ring.setAlpha(150);
            p.setPen(QPen(ring, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0),
                              (r.height() - 3) / 2.0, (r.height() - 3) / 2.0);
        }
    }

private:
    static constexpr int kIcon = 13;
    QColor text_, accent_, border_, fill_, fillHover_;
    QPixmap icon_, iconHover_;
    bool hovered_ = false;
};

QLabel* bodyLabel(const QString& html) {
    auto* lb = new QLabel(html);
    lb->setTextFormat(Qt::RichText);
    lb->setTextInteractionFlags(Qt::TextBrowserInteraction);
    lb->setOpenExternalLinks(true);
    lb->setWordWrap(true);
    return lb;
}
}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("关于 ChromaKey Studio"));
    setModal(true);
    setMinimumWidth(470);

    const auto& t = theme::tokens(theme::systemIsDark());
    const QString accent = QString::fromLatin1(t.accent);
    const QString muted = QString::fromLatin1(t.textSoft);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(10);

    // ---- 标题
    auto* title = new QLabel(QStringLiteral("ChromaKey Studio"));
    title->setObjectName(QStringLiteral("aboutTitle"));
    root->addWidget(title);

    auto* ver = new QLabel(QStringLiteral("v%1").arg(qApp->applicationVersion()));
    ver->setObjectName(QStringLiteral("aboutVersion"));
    root->addWidget(ver);

    auto* tag = new QLabel(QStringLiteral("绿幕抠像 → 带 Alpha 通道的视频"));
    tag->setObjectName(QStringLiteral("aboutTag"));
    root->addWidget(tag);

    // ---- 支持格式
    root->addSpacing(4);
    root->addWidget(bodyLabel(QStringLiteral(
        "<div style='color:%1;font-weight:600'>支持输出格式</div>"
        "<div style='color:%2;line-height:150%%'>"
        "· <b>WebM (VP9 + Alpha)</b> — 浏览器 / 网页，体积最小<br>"
        "· <b>MOV + PNG (RGBA)</b> — 单流无损，桌面播放器最稳<br>"
        "· <b>MOV + QuickTime RGBA</b> — 单流无损，实拍类更小<br>"
        "· <b>ProRes 4444 (MOV + Alpha)</b> — 剪辑软件通用，体积大"
        "</div>")
        .arg(QString::fromLatin1(t.text), muted)));

    root->addWidget(bodyLabel(QStringLiteral(
        "<div style='color:%1;line-height:150%%'>"
        "<b>不支持 MP4 / H.264</b>：这类格式的规范里没有 Alpha 通道。</div>")
        .arg(muted)));

    // ---- 开源协议
    root->addSpacing(4);
    root->addWidget(bodyLabel(QStringLiteral(
        "<div style='color:%1;font-weight:600'>开源协议</div>"
        "<div style='color:%2;line-height:150%%'>"
        "本程序以 <b>GPL-3.0</b> 发布，源码见下方仓库。<br>"
        "随附组件：Qt 6（LGPL-3.0）· FFmpeg（GPL-3.0，含 ProRes 编码器）<br>"
        "· libvpx（BSD-3-Clause）· libopus（BSD-3-Clause）<br>"
        "完整协议文本见安装目录 <b>LICENSE</b>。</div>")
        .arg(QString::fromLatin1(t.text), muted)));

    // ---- GitHub 胶囊
    auto* linkRow = new QHBoxLayout;
    linkRow->setContentsMargins(0, 6, 0, 0);
    linkRow->addWidget(new GitHubPill(QColor(muted), QColor(accent),
                                      QColor(QString::fromLatin1(t.border)),
                                      QColor(QString::fromLatin1(t.panelAlt)),
                                      QColor(QString::fromLatin1(t.accentSoft)), this));
    linkRow->addStretch(1);
    root->addLayout(linkRow);

    // ---- 构建信息
    auto* build = new QLabel(QStringLiteral("界面 Qt %1 · 键控 C++/SIMD · 编解码 FFmpeg（静态链接）")
                                 .arg(QString::fromLatin1(qVersion())));
    build->setObjectName(QStringLiteral("aboutBuild"));
    root->addWidget(build);

    // ---- 关闭
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    // 只作用于本对话框的样式, 不影响主窗口
    setStyleSheet(QStringLiteral(
        "QLabel#aboutTitle { font-size:19px; font-weight:700; color:%1; }"
        "QLabel#aboutVersion { color:%2; font-weight:600; }"
        "QLabel#aboutTag { color:%3; }"
        "QLabel#aboutBuild { color:%3; font-size:11px; }")
                      .arg(QString::fromLatin1(t.text), accent, muted));
}
