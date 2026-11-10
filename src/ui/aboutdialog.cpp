#include "aboutdialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "theme.h"

namespace {
const char* kRepoUrl = "https://github.com/delaolao/chromaKeyStudio";

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

    // ---- GitHub 胶囊链接
    auto* linkRow = new QHBoxLayout;
    linkRow->setContentsMargins(0, 6, 0, 0);
    auto* pill = new QLabel(QStringLiteral(
        "<a href=\"%1\" style=\"color:%2;text-decoration:none\">"
        "★&nbsp;&nbsp;delaolao / chromaKeyStudio</a>")
        .arg(QString::fromLatin1(kRepoUrl), QString::fromLatin1(t.onAccent)));
    pill->setObjectName(QStringLiteral("githubPill"));
    pill->setTextFormat(Qt::RichText);
    pill->setOpenExternalLinks(true);
    pill->setCursor(Qt::PointingHandCursor);
    pill->setAlignment(Qt::AlignCenter);
    pill->setFixedHeight(26);
    pill->setMinimumWidth(210);
    linkRow->addWidget(pill);
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
        "QLabel#aboutBuild { color:%3; font-size:11px; }"
        "QLabel#githubPill { background:%2; color:%4; border-radius:13px;"
        "                    padding:0 14px; font-weight:600; }"
        "QLabel#githubPill a { color:%4; text-decoration:none; }")
                      .arg(QString::fromLatin1(t.text), accent, muted,
                           QString::fromLatin1(t.onAccent)));
}
