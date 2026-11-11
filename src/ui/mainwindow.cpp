#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>

#include "core/keying.h"
#include "aboutdialog.h"
#include "previewwidget.h"
#include "themeswitch.h"
#include "theme.h"

namespace {
const char* kVersion = "0.3.0";
const char* kAppName = "ChromaKey Studio";

QString fmtTime(double s) {
    if (s < 0) s = 0;
    const int m = static_cast<int>(s) / 60;
    const double sec = s - m * 60;
    return QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0'))
        .arg(sec, 4, 'f', 1, QChar('0'));
}

QString toHex(const ck::KeyParams& p) {
    return QStringLiteral("#%1%2%3")
        .arg(p.keyR, 2, 16, QChar('0'))
        .arg(p.keyG, 2, 16, QChar('0'))
        .arg(p.keyB, 2, 16, QChar('0'))
        .toUpper();
}

bool parseHex(const QString& text, uint8_t* r, uint8_t* g, uint8_t* b) {
    QString s = text.trimmed();
    if (s.startsWith('#')) s.remove(0, 1);
    if (s.size() != 6) return false;
    bool ok = false;
    const int v = s.toInt(&ok, 16);
    if (!ok) return false;
    *r = static_cast<uint8_t>((v >> 16) & 0xFF);
    *g = static_cast<uint8_t>((v >> 8) & 0xFF);
    *b = static_cast<uint8_t>(v & 0xFF);
    return true;
}

QSlider* addSlider(QFormLayout* form, const QString& label, int lo, int hi,
                   int value, QLabel** valueLabel) {
    auto* holder = new QWidget;
    auto* h = new QHBoxLayout(holder);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto* sl = new QSlider(Qt::Horizontal);
    sl->setRange(lo, hi);
    sl->setValue(value);
    auto* lb = new QLabel;
    lb->setFixedWidth(38);
    lb->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    h->addWidget(sl, 1);
    h->addWidget(lb);
    form->addRow(label, holder);
    *valueLabel = lb;
    return sl;
}
}  // namespace

// ---------------------------------------------------------------- PlayThread

PlayThread::PlayThread(const QString& path, QMutex* paramMutex, ck::KeyParams* params,
                       const std::vector<ck::MaskPolygon>* masks,
                       const ck::MaskSettings* maskSettings, int outW, int outH,
                       double startT, QObject* parent)
    : QThread(parent), path_(path), paramMutex_(paramMutex), params_(params),
      masks_(masks), maskSettings_(maskSettings), outW_(outW), outH_(outH),
      startT_(startT) {}

void PlayThread::stop() { stop_ = true; }

void PlayThread::run() {
    ck::Reader reader;
    std::string err;
    if (!reader.open(path_.toStdString(), &err)) {
        emit finishedAt(startT_);
        return;
    }
    // 从 startT_ 开始: 先定位取一帧, 再顺序读
    ck::RgbFrame rgb;
    if (!reader.frameAt(startT_, outW_, outH_, rgb, &err)) {
        emit finishedAt(startT_);
        return;
    }
    const ck::MediaInfo& info = reader.info();
    const double dt = 1.0 / std::max(info.fps, 1.0);

    // 掩码是静态的, 播放开始时算一次
    ck::MaskLayers layers;
    if (masks_ && maskSettings_ && !masks_->empty())
        layers = ck::rasterizeMask(*masks_, outW_, outH_, *maskSettings_);

    reader.startSequential(&err);
    // 跳过已播过的帧数, 让顺序流和起点对齐
    double t = startT_;
    ck::Image keyed;
    int skip = static_cast<int>(std::lround(startT_ * info.fps));
    for (int i = 0; i < skip && !stop_; ++i) {
        bool done = false;
        if (reader.nextFrame(rgb, &done, &err) || done) break;
    }

    while (!stop_) {
        bool done = false;
        if (!reader.nextFrame(rgb, &done, &err) || done) break;
        ck::KeyParams p;
        {
            QMutexLocker lock(paramMutex_);
            p = *params_;
        }
        ck::keyImage(rgb.data.data(), rgb.width, rgb.height, rgb.stride, p, keyed);
        if (!layers.empty())
            ck::applyMask(keyed, layers, p.keyR, p.keyG, p.keyB);
        QImage img(keyed.data.data(), keyed.width, keyed.height, keyed.stride,
                   QImage::Format_RGBA8888);
        emit frameReady(img.copy(), t);
        t += dt;
        if (t > info.duration) break;
    }
    emit finishedAt(t);
}

// ---------------------------------------------------------------- ExportThread

ExportThread::ExportThread(const QString& path, ck::ExportOptions opt, QObject* parent)
    : QThread(parent), path_(path), opt_(std::move(opt)) {}

void ExportThread::cancel() { cancel_ = true; }

void ExportThread::run() {
    ck::Reader reader;
    std::string err;
    if (!reader.open(path_.toStdString(), &err)) {
        emit done(false, QString::fromStdString(err));
        return;
    }
    auto result = ck::exportVideo(
        reader, opt_,
        [this](int d, int t) { emit progress(d, t); },
        [this]() { return cancel_.load(); });
    if (!result.ok) {
        emit done(false, QString::fromStdString(result.error));
        return;
    }
    auto check = ck::checkAlpha(opt_.outPath, opt_.format);
    QString msg = QStringLiteral("%1 帧  %2s  %3  %4\n音频: %5\n门禁: %6 (透明 %7%)")
                      .arg(result.frames)
                      .arg(result.seconds, 0, 'f', 2)
                      .arg(QString::fromStdString(result.videoCodec),
                           QString::fromStdString(result.pixFmt))
                      .arg(QString::fromStdString(result.audioInfo))
                      .arg(QString::fromStdString(check.message))
                      .arg(check.transparentRatio * 100.0, 0, 'f', 1);
    emit done(check.ok, msg);
}

// ---------------------------------------------------------------- MainWindow

MainWindow::MainWindow() {
    ck::initFFmpeg();
    setWindowTitle(QStringLiteral("%1 v%2 — 视频色度抠像")
                       .arg(kAppName, kVersion));
    resize(1320, 820);
    setMinimumSize(1000, 640);
    setAcceptDrops(true);
    buildUi();
    buildMenus();
    connectSignals();

    QSettings s(QStringLiteral("ChromaKeyStudio"), QStringLiteral("ChromaKeyStudio"));
    const int mode = s.value(QStringLiteral("ui/theme"), 0).toInt();
    setTheme(mode);
    if (themeSwitch_) {
        QSignalBlocker block(themeSwitch_);
        themeSwitch_->setMode(mode);
    }

    // 参数默认值(与 Python 版一致)
    params_.keyR = 0;
    params_.keyG = 177;
    params_.keyB = 64;
    params_.similarity = 0.50f;
    params_.blend = 0.15f;
    params_.featherSigma = 1.2f;
    params_.despill = true;
    params_.despillMix = 0.85f;
    params_.spillMatte = true;
    params_.neutralize = true;
    editHex_->setText(toHex(params_));
    onParamChanged();
    updateTimeLabel();
}

MainWindow::~MainWindow() {
    if (play_) {
        play_->stop();
        play_->wait(3000);
    }
    if (exporter_) {
        exporter_->cancel();
        exporter_->wait(5000);
    }
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 6);
    root->setSpacing(8);

    // ---- 顶部: 打开视频
    auto* top = new QWidget(central);
    auto* topLay = new QHBoxLayout(top);
    topLay->setContentsMargins(0, 0, 0, 0);
    topLay->setSpacing(10);
    btnOpen_ = new QPushButton(QStringLiteral("打开视频…"), top);
    btnOpen_->setMinimumSize(128, 32);
    btnOpen_->setToolTip(QStringLiteral("选择绿幕/蓝幕视频（Ctrl+O），也可以直接拖进窗口"));
    topLay->addWidget(btnOpen_);
    lbFile_ = new QLabel(QStringLiteral("还没有打开视频 — 点左边按钮, 或把文件拖进来"), top);
    lbFile_->setObjectName(QStringLiteral("hint"));
    lbFile_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    topLay->addWidget(lbFile_, 1);

    // 主题切换: 跟随系统 / 浅色 / 深色(样式同 MyPassword 的快捷开关)
    themeSwitch_ = new ThemeSwitch(top);
    themeSwitch_->setToolTip(
        QStringLiteral("主题：跟随系统 / 浅色 / 深色（点击或按 ← → 切换）"));
    topLay->addWidget(themeSwitch_, 0, Qt::AlignVCenter);
    root->addWidget(top);

    auto* split = new QSplitter(Qt::Horizontal, central);
    split->setChildrenCollapsible(false);
    preview_ = new PreviewWidget(split);
    split->addWidget(preview_);

    // ---- 右侧参数面板
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setFixedWidth(348);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* box = new QWidget;
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(6, 0, 6, 6);
    lay->setSpacing(10);

    // 背景颜色
    auto* gColor = new QGroupBox(QStringLiteral("背景颜色"));
    auto* fColor = new QFormLayout(gColor);
    auto* row = new QHBoxLayout;
    btnGreen_ = new QPushButton(QStringLiteral("绿色"));
    btnBlue_ = new QPushButton(QStringLiteral("蓝色"));
    row->addWidget(btnGreen_);
    row->addWidget(btnBlue_);
    fColor->addRow(row);
    editHex_ = new QLineEdit(QStringLiteral("#00B140"));
    editHex_->setMaxLength(7);
    fColor->addRow(QStringLiteral("十六进制"), editHex_);
    btnAuto_ = new QPushButton(QStringLiteral("自动匹配背景(推荐先点这个)"));
    fColor->addRow(btnAuto_);
    lay->addWidget(gColor);

    // 键控参数
    auto* gKey = new QGroupBox(QStringLiteral("键控参数"));
    auto* fKey = new QFormLayout(gKey);
    slSim_ = addSlider(fKey, QStringLiteral("容差 similarity"), 1, 100, 50, &lbSim_);
    slBlend_ = addSlider(fKey, QStringLiteral("边缘过渡 blend"), 0, 100, 15, &lbBlend_);
    cbFeather_ = new QCheckBox(QStringLiteral("空间羽化(模糊遮罩)"));
    fKey->addRow(cbFeather_);
    slFeather_ = addSlider(fKey, QStringLiteral("羽化半径"), 0, 50, 12, &lbFeather_);
    cbDespill_ = new QCheckBox(QStringLiteral("溢色抑制(去绿边反光)"));
    cbDespill_->setChecked(true);
    fKey->addRow(cbDespill_);
    slDespill_ = addSlider(fKey, QStringLiteral("抑制强度"), 0, 100, 85, &lbDespill_);
    lay->addWidget(gKey);

    // ---- 多边形遮罩(垃圾遮罩)
    auto* gMask = new QGroupBox(QStringLiteral("遮罩（去掉水印/夹具/穿帮）"));
    auto* fMask = new QFormLayout(gMask);
    cbMaskEdit_ = new QCheckBox(QStringLiteral("编辑遮罩"));
    cbMaskEdit_->setToolTip(
        QStringLiteral("进入后可在画面上点多边形：单击加点，双击或回车闭合"));
    fMask->addRow(cbMaskEdit_);
    cmbMaskMode_ = new QComboBox;
    for (auto m : {ck::MaskMode::Transparent, ck::MaskMode::HoldOut,
                   ck::MaskMode::FillKey})
        cmbMaskMode_->addItem(QString::fromUtf8(ck::maskModeLabel(m)),
                              static_cast<int>(m));
    cmbMaskMode_->setToolTip(
        QStringLiteral("新建多边形的模式；选中已有块时修改它"));
    fMask->addRow(QStringLiteral("类型"), cmbMaskMode_);
    slMaskFeather_ = addSlider(fMask, QStringLiteral("羽化"), 0, 50, 15, &lbMaskFeather_);
    slMaskExpand_ = addSlider(fMask, QStringLiteral("外扩/内缩"), -100, 100, 0,
                              &lbMaskExpand_);
    lbMaskFeather_->setFixedWidth(38);
    lbMaskExpand_->setFixedWidth(38);
    auto* maskBtns = new QHBoxLayout;
    btnMaskUndo_ = new QPushButton(QStringLiteral("撤销"));
    btnMaskClear_ = new QPushButton(QStringLiteral("清除全部"));
    maskBtns->addWidget(btnMaskUndo_);
    maskBtns->addWidget(btnMaskClear_);
    fMask->addRow(maskBtns);
    lbMaskInfo_ = new QLabel(QStringLiteral("还没有遮罩"));
    lbMaskInfo_->setObjectName(QStringLiteral("hint"));
    lbMaskInfo_->setWordWrap(true);
    fMask->addRow(lbMaskInfo_);
    lay->addWidget(gMask);
    // 预览
    auto* gView = new QGroupBox(QStringLiteral("预览"));
    auto* fView = new QFormLayout(gView);
    cmbRes_ = new QComboBox;
    // 不带速度标签: 三项里 720p 其实最慢, 标"快/最快"横着看是矛盾的。
    // 说明放进 tooltip, 不额外占面板高度。
    cmbRes_->addItems({QStringLiteral("720p"), QStringLiteral("540p"),
                       QStringLiteral("360p")});
    cmbRes_->setToolTip(
        QStringLiteral("预览分辨率越低越流畅；导出始终使用原分辨率"));
    fView->addRow(QStringLiteral("预览分辨率"), cmbRes_);
    cmbDisplay_ = new QComboBox;
    cmbDisplay_->addItems({QStringLiteral("棋盘格(透明)"), QStringLiteral("黑底"),
                           QStringLiteral("白底"), QStringLiteral("遮罩(alpha)")});
    fView->addRow(QStringLiteral("显示"), cmbDisplay_);
    lay->addWidget(gView);

    // 导出
    auto* gOut = new QGroupBox(QStringLiteral("导出"));
    auto* fOut = new QFormLayout(gOut);
    cmbFmt_ = new QComboBox;
    for (auto f : {ck::OutFormat::WebM, ck::OutFormat::ProRes4444,
                   ck::OutFormat::QuickTimeRGBA, ck::OutFormat::MovPng}) {
        cmbFmt_->addItem(QString::fromUtf8(ck::formatLabel(f)),
                         static_cast<int>(f));
    }
    fOut->addRow(QStringLiteral("格式"), cmbFmt_);
    lbFmtHint_ = new QLabel;
    lbFmtHint_->setObjectName(QStringLiteral("hint"));
    lbFmtHint_->setWordWrap(true);
    fOut->addRow(lbFmtHint_);

    auto* outRow = new QHBoxLayout;
    editOut_ = new QLineEdit;
    editOut_->setPlaceholderText(QStringLiteral("留空则输出到视频旁边"));
    auto* btnBrowse = new QPushButton(QStringLiteral("…"));
    btnBrowse->setObjectName(QStringLiteral("iconbtn"));
    btnBrowse->setFixedWidth(34);
    outRow->addWidget(editOut_, 1);
    outRow->addWidget(btnBrowse);
    fOut->addRow(QStringLiteral("输出文件"), outRow);
    connect(btnBrowse, &QPushButton::clicked, this, [this] {
        const auto fmt = static_cast<ck::OutFormat>(cmbFmt_->currentData().toInt());
        QString start = editOut_->text().trimmed();
        if (start.isEmpty()) start = defaultOutputPath();
        QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出到"), start,
            QString::fromLatin1(ck::formatLabel(fmt)));
        if (!path.isEmpty()) {
            if (!path.endsWith(QString::fromLatin1(ck::formatSuffix(fmt))))
                path += QString::fromLatin1(ck::formatSuffix(fmt));
            editOut_->setText(path);
        }
    });

    cbAudio_ = new QCheckBox(QStringLiteral("保留音轨"));
    cbAudio_->setChecked(true);
    fOut->addRow(cbAudio_);
    btnExport_ = new QPushButton(QStringLiteral("开始导出"));
    btnExport_->setMinimumHeight(34);
    fOut->addRow(btnExport_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    fOut->addRow(progress_);
    btnCancel_ = new QPushButton(QStringLiteral("取消导出"));
    btnCancel_->setEnabled(false);
    fOut->addRow(btnCancel_);
    lay->addWidget(gOut);
    lay->addStretch(1);
    scroll->setWidget(box);

    split->addWidget(scroll);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({980, 340});
    root->addWidget(split, 1);

    // ---- 底部: 播放 + 时间轴
    auto* bar = new QWidget(central);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    btnPlay_ = new QPushButton(QStringLiteral("▶ 播放"));
    btnPlay_->setCheckable(true);
    btnPlay_->setFixedWidth(96);
    h->addWidget(btnPlay_);
    timeline_ = new QSlider(Qt::Horizontal);
    timeline_->setRange(0, 1000);
    timeline_->setEnabled(false);
    h->addWidget(timeline_, 1);
    lbTime_ = new QLabel(QStringLiteral("00:00.0 / 00:00.0"));
    lbTime_->setFixedWidth(130);
    lbTime_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    h->addWidget(lbTime_);
    lbStats_ = new QLabel;
    lbStats_->setObjectName(QStringLiteral("hint"));
    lbStats_->setMinimumWidth(230);
    h->addWidget(lbStats_);
    root->addWidget(bar);

    statusBar()->showMessage(QStringLiteral("就绪 — 点「打开视频…」, 或直接把视频文件拖进窗口"));
    onFormatChanged();
}

void MainWindow::buildMenus() {
    auto* mFile = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto* actOpen = mFile->addAction(QStringLiteral("打开视频…"));
    actOpen->setShortcut(QKeySequence::Open);
    connect(actOpen, &QAction::triggered, this, &MainWindow::openVideo);
    actExport_ = mFile->addAction(QStringLiteral("导出…"));
    actExport_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    actExport_->setEnabled(false);
    connect(actExport_, &QAction::triggered, this, &MainWindow::startExport);
    mFile->addSeparator();
    auto* actQuit = mFile->addAction(QStringLiteral("退出"));
    actQuit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(actQuit, &QAction::triggered, this, &QWidget::close);


    auto* mHelp = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    connect(mHelp->addAction(QStringLiteral("怎么用")), &QAction::triggered, this,
            &MainWindow::showHelp);
    connect(mHelp->addAction(QStringLiteral("关于")), &QAction::triggered, this,
            &MainWindow::showAbout);
}

void MainWindow::connectSignals() {
    connect(btnOpen_, &QPushButton::clicked, this, &MainWindow::openVideo);
    connect(btnGreen_, &QPushButton::clicked, this, [this] {
        editHex_->setText(QStringLiteral("#00B140"));
        onParamChanged();
    });
    connect(btnBlue_, &QPushButton::clicked, this, [this] {
        editHex_->setText(QStringLiteral("#0047BB"));
        onParamChanged();
    });
    connect(btnAuto_, &QPushButton::clicked, this, &MainWindow::applyAutoKey);
    connect(editHex_, &QLineEdit::editingFinished, this, &MainWindow::onParamChanged);
    for (auto* sl : {slSim_, slBlend_, slFeather_, slDespill_})
        connect(sl, &QSlider::valueChanged, this, &MainWindow::onParamChanged);
    connect(cbFeather_, &QCheckBox::toggled, this, &MainWindow::onParamChanged);
    connect(cbDespill_, &QCheckBox::toggled, this, &MainWindow::onParamChanged);
    connect(cmbRes_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onPreviewResChanged);
    connect(cmbDisplay_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onDisplayChanged);
    connect(cmbFmt_, &QComboBox::currentIndexChanged, this, &MainWindow::onFormatChanged);
    connect(btnExport_, &QPushButton::clicked, this, &MainWindow::startExport);
    connect(btnCancel_, &QPushButton::clicked, this, &MainWindow::cancelExport);
    connect(timeline_, &QSlider::valueChanged, this, &MainWindow::onTimelineMoved);
    connect(btnPlay_, &QPushButton::toggled, this, &MainWindow::onPlayToggled);
    connect(themeSwitch_, &ThemeSwitch::modeChanged, this, &MainWindow::setTheme);

    // 遮罩
    connect(cbMaskEdit_, &QCheckBox::toggled, this, &MainWindow::onMaskEditToggled);
    connect(cmbMaskMode_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onMaskModeChanged);
    connect(slMaskFeather_, &QSlider::valueChanged, this,
            &MainWindow::onMaskSettingsChanged);
    connect(slMaskExpand_, &QSlider::valueChanged, this,
            &MainWindow::onMaskSettingsChanged);
    connect(btnMaskUndo_, &QPushButton::clicked, this, &MainWindow::undoMask);
    connect(btnMaskClear_, &QPushButton::clicked, this, &MainWindow::clearMasks);
    connect(preview_, &PreviewWidget::maskEdited, this, &MainWindow::onMaskEdited);
    connect(preview_, &PreviewWidget::maskDrawFinished, this,
            &MainWindow::onMaskDrawFinished);
    connect(preview_, &PreviewWidget::maskEditStarted, this,
            &MainWindow::pushMaskUndo);
}

// ---------------------------------------------------------------- 打开/预览

void MainWindow::openVideo() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择绿幕视频"), QString(),
        QStringLiteral("视频文件 (*.mp4 *.mov *.mkv *.avi *.m4v *.webm);;所有文件 (*)"));
    if (!path.isEmpty()) loadVideo(path);
}

bool MainWindow::loadVideo(const QString& path) {
    if (play_) {
        play_->stop();
        play_->wait(3000);
        delete play_;
        play_ = nullptr;
    }
    std::string err;
    if (!reader_.open(path.toStdString(), &err)) {
        QMessageBox::critical(this, QStringLiteral("无法读取视频"),
                              QString::fromStdString(err));
        return false;
    }
    info_ = reader_.info();
    loaded_ = true;
    currentT_ = 0.0;
    params_.similarity = 0.50f;

    const int maxH = cmbRes_->currentIndex() == 0   ? 720
                     : cmbRes_->currentIndex() == 1 ? 540
                                                    : 360;
    const int maxW = maxH * 16 / 9;
    info_.fit(maxW, maxH, &previewW_, &previewH_);

    lbFile_->setText(QStringLiteral("%1    %2×%3  %4fps  %5s")
                         .arg(QFileInfo(path).fileName())
                         .arg(info_.width)
                         .arg(info_.height)
                         .arg(info_.fps, 0, 'f', 2)
                         .arg(info_.duration, 0, 'f', 1));
    setWindowTitle(QStringLiteral("%1 v%2 — %3")
                       .arg(kAppName, kVersion, QFileInfo(path).fileName()));
    statusBar()->showMessage(
        QStringLiteral("%1  %2x%3  %4fps  %5s  %6/%7  %8")
            .arg(QFileInfo(path).fileName())
            .arg(info_.width)
            .arg(info_.height)
            .arg(info_.fps, 0, 'f', 2)
            .arg(info_.duration, 0, 'f', 1)
            .arg(QString::fromStdString(info_.codec),
                 QString::fromStdString(info_.pixFmt))
            .arg(info_.hasAudio ? QStringLiteral("有音轨") : QStringLiteral("无音轨")),
        8000);

    timeline_->setEnabled(true);
    btnPlay_->setEnabled(true);
    btnExport_->setEnabled(true);
    actExport_->setEnabled(true);

    // 遮罩: 有同名 .ckmask.json 就自动载入
    masks_.clear();
    maskHistory_.clear();
    maskSettings_ = ck::MaskSettings();
    loadMask();
    preview_->attachMask(&masks_);
    previewMaskDirty_ = true;
    updateMaskInfo();

    // 自动匹配一次
    refreshPreview();
    applyAutoKey();
    return true;
}

void MainWindow::applyAutoKey() {
    if (!loaded_) {
        statusBar()->showMessage(QStringLiteral("先打开一个视频"), 4000);
        return;
    }
    auto sug = ck::suggestKey(srcRgb_.data.data(), srcRgb_.width, srcRgb_.height,
                              srcRgb_.stride);
    params_.keyR = sug.r;
    params_.keyG = sug.g;
    params_.keyB = sug.b;
    editHex_->setText(toHex(params_));
    slSim_->blockSignals(true);
    slSim_->setValue(static_cast<int>(sug.similarity * 100 + 0.5f));
    slSim_->blockSignals(false);
    onParamChanged();
    statusBar()->showMessage(QStringLiteral("自动匹配: 键色 %1  容差 %2")
                                 .arg(toHex(params_))
                                 .arg(sug.similarity, 0, 'f', 2),
                             6000);
}

ck::KeyParams MainWindow::collectParams() const {
    ck::KeyParams p = params_;
    uint8_t r, g, b;
    if (parseHex(editHex_->text(), &r, &g, &b)) {
        p.keyR = r;
        p.keyG = g;
        p.keyB = b;
    }
    p.similarity = slSim_->value() / 100.0f;
    p.blend = slBlend_->value() / 100.0f;
    p.featherSigma = cbFeather_->isChecked() ? slFeather_->value() / 10.0f : 0.0f;
    p.despill = cbDespill_->isChecked();
    p.despillMix = slDespill_->value() / 100.0f;
    p.spillMatte = true;
    p.neutralize = true;
    return p;
}

void MainWindow::onParamChanged() {
    params_ = collectParams();
    {
        QMutexLocker lock(&paramMutex_);
    }
    lbSim_->setText(QString::number(params_.similarity, 'f', 2));
    lbBlend_->setText(QString::number(params_.blend, 'f', 2));
    lbFeather_->setText(QString::number(slFeather_->value() / 10.0, 'f', 1));
    lbDespill_->setText(QString::number(params_.despillMix, 'f', 2));
    slFeather_->setEnabled(cbFeather_->isChecked());
    slDespill_->setEnabled(cbDespill_->isChecked());
    if (loaded_) paintKeyed();
}

void MainWindow::refreshPreview() {
    if (!loaded_) return;
    std::string err;
    if (!reader_.frameAt(currentT_, previewW_, previewH_, srcRgb_, &err)) {
        statusBar()->showMessage(QStringLiteral("取帧失败: %1")
                                     .arg(QString::fromStdString(err)), 6000);
        return;
    }
    paintKeyed();
    updateTimeLabel();
}

void MainWindow::paintKeyed() {
    if (!loaded_ || srcRgb_.data.empty()) return;
    ck::keyImage(srcRgb_.data.data(), srcRgb_.width, srcRgb_.height, srcRgb_.stride,
                 params_, keyed_);
    if (!masks_.empty()) {
        if (previewMaskDirty_ || previewMask_.width != keyed_.width ||
            previewMask_.height != keyed_.height) {
            previewMask_ = ck::rasterizeMask(masks_, keyed_.width, keyed_.height,
                                             maskSettings_);
            previewMaskDirty_ = false;
        }
        ck::applyMask(keyed_, previewMask_, params_.keyR, params_.keyG, params_.keyB);
    }
    qimg_ = QImage(keyed_.data.data(), keyed_.width, keyed_.height, keyed_.stride,
                   QImage::Format_RGBA8888)
                .copy();
    preview_->setImage(qimg_);
    updateStats();
}

void MainWindow::updateStats() {
    if (keyed_.data.empty()) return;
    const size_t total = static_cast<size_t>(keyed_.width) * keyed_.height;
    size_t transparent = 0;
    for (int y = 0; y < keyed_.height; ++y) {
        const uint8_t* row = keyed_.row(y);
        for (int x = 0; x < keyed_.width; ++x)
            if (row[x * 4 + 3] < 128) ++transparent;
    }
    lbStats_->setText(QStringLiteral("透明 %1%   预览 %2×%3")
                          .arg(100.0 * transparent / std::max<size_t>(total, 1), 0, 'f',
                               1)
                          .arg(keyed_.width)
                          .arg(keyed_.height));
}

void MainWindow::onPreviewResChanged() {
    if (!loaded_) return;
    const int maxH = cmbRes_->currentIndex() == 0   ? 720
                     : cmbRes_->currentIndex() == 1 ? 540
                                                    : 360;
    info_.fit(maxH * 16 / 9, maxH, &previewW_, &previewH_);
    refreshPreview();
}

void MainWindow::onDisplayChanged() {
    const int idx = cmbDisplay_->currentIndex();
    if (idx == 1) preview_->setMode(PreviewWidget::Solid, QColor(0, 0, 0));
    else if (idx == 2) preview_->setMode(PreviewWidget::Solid, QColor(255, 255, 255));
    else if (idx == 3) preview_->setMode(PreviewWidget::Matte);
    else preview_->setMode(PreviewWidget::Checker);
    if (loaded_) preview_->setImage(qimg_);
}

void MainWindow::updateTimeLabel() {
    lbTime_->setText(QStringLiteral("%1 / %2")
                         .arg(fmtTime(currentT_), fmtTime(loaded_ ? info_.duration : 0)));
}

double MainWindow::sliderToTime(int v) const {
    return loaded_ ? info_.duration * v / 1000.0 : 0.0;
}

int MainWindow::timeToSlider(double t) const {
    if (!loaded_ || info_.duration <= 0) return 0;
    return static_cast<int>(std::clamp(t / info_.duration * 1000.0, 0.0, 1000.0));
}

void MainWindow::onTimelineMoved(int v) {
    if (!loaded_) return;
    if (play_) {
        play_->stop();
        play_->wait(3000);
        delete play_;
        play_ = nullptr;
        btnPlay_->setChecked(false);
    }
    currentT_ = sliderToTime(v);
    refreshPreview();
}

// ---------------------------------------------------------------- 播放

void MainWindow::onPlayToggled(bool on) {
    if (!loaded_) {
        btnPlay_->setChecked(false);
        return;
    }
    if (on) {
        double start = currentT_;
        if (start >= info_.duration - 0.05) start = 0;
        play_ = new PlayThread(QString::fromStdString(info_.path), &paramMutex_, &params_,
                               &masks_, &maskSettings_, previewW_, previewH_, start,
                               this);
        connect(play_, &PlayThread::frameReady, this,
                [this](QImage img, double t) {
                    preview_->setImage(img);
                    currentT_ = t;
                    updateTimeLabel();
                    timeline_->blockSignals(true);
                    timeline_->setValue(timeToSlider(t));
                    timeline_->blockSignals(false);
                });
        connect(play_, &PlayThread::finishedAt, this, [this](double t) {
            currentT_ = std::min(t, info_.duration);
            btnPlay_->setChecked(false);
            refreshPreview();
        });
        play_->start();
        btnPlay_->setText(QStringLiteral("■ 停止"));
    } else {
        if (play_) {
            play_->stop();
            play_->wait(3000);
            delete play_;
            play_ = nullptr;
        }
        btnPlay_->setText(QStringLiteral("▶ 播放"));
        refreshPreview();
    }
}

// ---------------------------------------------------------------- 导出

void MainWindow::onFormatChanged() {
    const auto fmt = static_cast<ck::OutFormat>(cmbFmt_->currentData().toInt());
    switch (fmt) {
        case ck::OutFormat::WebM:
            lbFmtHint_->setText(
                QStringLiteral("浏览器/网页友好, 文件小; 需要能解 VP9-alpha 的播放器\n"
                               "QtAVPlayer / pet_player 需 libvpx 解码——"
                               "补丁见 native/patches（已随项目提供）"));
            break;
        case ck::OutFormat::ProRes4444:
            lbFmtHint_->setText(
                QStringLiteral("剪辑软件通用, 画面最高; 文件很大 (1080p 约 200Mbps)"));
            break;
        case ck::OutFormat::QuickTimeRGBA:
            lbFmtHint_->setText(
                QStringLiteral("单流无损 RGBA (qtrle), QtAVPlayer / pet_player 最稳"));
            break;
        case ck::OutFormat::MovPng:
            lbFmtHint_->setText(
                QStringLiteral("单流无损 RGBA (PNG), 与 qtrle 等价; 图形类素材更小"));
            break;
    }
    editOut_->clear();
}

QString MainWindow::defaultOutputPath() const {
    if (!loaded_) return QString();
    const auto fmt = static_cast<ck::OutFormat>(cmbFmt_->currentData().toInt());
    const QFileInfo fi(QString::fromStdString(info_.path));
    QString tag = QStringLiteral("alpha");
    if (fmt == ck::OutFormat::QuickTimeRGBA) tag = QStringLiteral("alphargba");
    else if (fmt == ck::OutFormat::MovPng) tag = QStringLiteral("alphapng");
    return fi.absolutePath() + QStringLiteral("/") + fi.completeBaseName() +
           QStringLiteral("_") + tag + QString::fromLatin1(ck::formatSuffix(fmt));
}

void MainWindow::startExport() {
    if (!loaded_ || exporting_) return;
    const auto fmt = static_cast<ck::OutFormat>(cmbFmt_->currentData().toInt());
    QString out = editOut_->text().trimmed();
    if (out.isEmpty()) out = defaultOutputPath();
    if (!out.endsWith(QString::fromLatin1(ck::formatSuffix(fmt))))
        out += QString::fromLatin1(ck::formatSuffix(fmt));

    ck::ExportOptions opt;
    opt.outPath = out.toStdString();
    opt.format = fmt;
    opt.key = collectParams();
    opt.key.neutralize = true;
    opt.includeAudio = cbAudio_->isChecked();
    opt.masks = masks_;
    opt.maskSettings = maskSettings_;

    exporting_ = true;
    btnExport_->setEnabled(false);
    btnCancel_->setEnabled(true);
    progress_->setValue(0);
    statusBar()->showMessage(QStringLiteral("正在导出到 %1 …").arg(QFileInfo(out).fileName()));

    exporter_ = new ExportThread(QString::fromStdString(info_.path), opt, this);
    connect(exporter_, &ExportThread::progress, this, [this](int d, int t) {
        const int pct = static_cast<int>(100.0 * d / std::max(t, 1));
        progress_->setValue(pct);
        progress_->setFormat(QStringLiteral("%1%   %2/%3 帧").arg(pct).arg(d).arg(t));
    });
    connect(exporter_, &ExportThread::done, this, [this, out](bool ok, QString msg) {
        exporting_ = false;
        btnExport_->setEnabled(true);
        btnCancel_->setEnabled(false);
        progress_->setValue(ok ? 100 : 0);
        progress_->setFormat(ok ? QStringLiteral("完成") : QString());
        if (ok) {
            statusBar()->showMessage(QStringLiteral("导出完成: %1").arg(QFileInfo(out).fileName()), 10000);
            QMessageBox box(this);
            box.setWindowTitle(QStringLiteral("导出成功"));
            box.setText(msg);
            box.setIcon(QMessageBox::Information);
            auto* openBtn = box.addButton(QStringLiteral("打开所在文件夹"),
                                          QMessageBox::ActionRole);
            box.addButton(QStringLiteral("关闭"), QMessageBox::AcceptRole);
            box.exec();
            if (box.clickedButton() == openBtn)
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(out).absolutePath()));
        } else {
            statusBar()->showMessage(QStringLiteral("导出失败"), 8000);
            QMessageBox::critical(this, QStringLiteral("导出失败"), msg);
        }
        if (exporter_) {
            exporter_->wait(2000);
            delete exporter_;
            exporter_ = nullptr;
        }
    });
    exporter_->start();
}

void MainWindow::cancelExport() {
    if (exporter_) {
        exporter_->cancel();
        statusBar()->showMessage(QStringLiteral("正在取消 …"));
    }
}

// ---------------------------------------------------------------- 其它

void MainWindow::setTheme(int mode) {
    QSettings s(QStringLiteral("ChromaKeyStudio"), QStringLiteral("ChromaKeyStudio"));
    s.setValue(QStringLiteral("ui/theme"), mode);
    bool dark = mode == 2;
    if (mode == 0) dark = theme::systemIsDark();
    theme::apply(qApp, dark);
    if (themeSwitch_ && themeSwitch_->mode() != mode) {
        QSignalBlocker block(themeSwitch_);
        themeSwitch_->setMode(mode);
    }
    if (preview_) preview_->update();
}

void MainWindow::showHelp() {
    QMessageBox::information(
        this, QStringLiteral("怎么用"),
        QStringLiteral(
            "1. 点「打开视频…」（或 Ctrl+O，或把文件拖进窗口）\n"
            "2. 点「自动匹配背景」——多数素材到这一步就干净了\n"
            "3. 还不干净就调「容差」：背景有残留往大调，主体被吃掉往小调\n"
            "4. 边缘发毛就开「空间羽化」；发丝泛绿就保持「溢色抑制」开启\n"
            "5. 背景上有水印 / 夹具 / 接缝去不掉？勾「编辑遮罩」，\n"
            "   在画面上点几个点把那一块圈起来（单击加点，双击或回车闭合）。\n"
            "   类型：强制透明=挖掉；强制保留=保住主体；填充键色=先涂绿再抠。\n"
            "   羽化让边缘自然，外扩/内缩用来吃掉水印的光晕。\n"
            "   遮罩会自动存成同名 .ckmask.json，下次打开自动载入。\n"
            "6. 选格式，点「开始导出」\n\n"
            "空格 = 播放/停止，拖时间轴可以看任意一帧。\n"
            "预览是缩小后的画面，导出用原分辨率。\n\n"
            "给 QtAVPlayer / pet_player 用：MOV 直接可播；\\n"
            "WebM 需要 libvpx 解码，打过 native/patches 里那个补丁的 pet_player 可以。"));
}

void MainWindow::showAbout() {
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString p = u.toLocalFile();
        if (p.endsWith(".mp4", Qt::CaseInsensitive) ||
            p.endsWith(".mov", Qt::CaseInsensitive) ||
            p.endsWith(".mkv", Qt::CaseInsensitive) ||
            p.endsWith(".avi", Qt::CaseInsensitive) ||
            p.endsWith(".webm", Qt::CaseInsensitive)) {
            e->acceptProposedAction();
            loadVideo(p);
            return;
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    saveMask();
    if (play_) {
        play_->stop();
        play_->wait(3000);
    }
    if (exporter_) {
        exporter_->cancel();
        exporter_->wait(5000);
    }
    e->accept();
}

// ---------------------------------------------------------------- 多边形遮罩

void MainWindow::onMaskEditToggled(bool on) {
    if (on && !loaded_) {
        cbMaskEdit_->setChecked(false);
        statusBar()->showMessage(QStringLiteral("先打开一个视频"), 4000);
        return;
    }
    preview_->setMaskEditMode(on);
    if (on) {
        statusBar()->showMessage(
            QStringLiteral("遮罩编辑：单击加点，双击或回车闭合，Esc 取消"), 8000);
    } else {
        previewMaskDirty_ = true;
        paintKeyed();
        saveMask();
    }
    updateMaskInfo();
}

void MainWindow::onMaskModeChanged(int) {
    if (!cmbMaskMode_) return;
    preview_->setNewPolygonMode(
        static_cast<ck::MaskMode>(cmbMaskMode_->currentData().toInt()));
    previewMaskDirty_ = true;
    if (loaded_) paintKeyed();
    saveMask();
}

void MainWindow::onMaskSettingsChanged() {
    maskSettings_.featherPx = slMaskFeather_->value() / 10.0f;
    maskSettings_.expandPx = slMaskExpand_->value() / 10.0f;
    if (lbMaskFeather_)
        lbMaskFeather_->setText(QString::number(maskSettings_.featherPx, 'f', 1));
    if (lbMaskExpand_)
        lbMaskExpand_->setText(QString::number(maskSettings_.expandPx, 'f', 1));
    previewMaskDirty_ = true;
    if (loaded_) paintKeyed();
    QTimer::singleShot(400, this, [this] { saveMask(); });
}

void MainWindow::onMaskEdited() {
    if (preview_) preview_->update();
    updateMaskInfo();
    QTimer::singleShot(400, this, [this] { saveMask(); });
}

void MainWindow::onMaskDrawFinished() {
    previewMaskDirty_ = true;
    if (loaded_) paintKeyed();
    updateMaskInfo();
    saveMask();
}

void MainWindow::pushMaskUndo() {
    maskHistory_.push_back({masks_, maskSettings_});
    if (maskHistory_.size() > 100) maskHistory_.erase(maskHistory_.begin());
    updateMaskInfo();
}

void MainWindow::undoMask() {
    if (maskHistory_.empty()) return;
    masks_ = maskHistory_.back().first;
    maskSettings_ = maskHistory_.back().second;
    maskHistory_.pop_back();

    auto sync = [this] {
        QSignalBlocker b1(slMaskFeather_), b2(slMaskExpand_);
        slMaskFeather_->setValue(int(std::lround(maskSettings_.featherPx * 10)));
        slMaskExpand_->setValue(int(std::lround(maskSettings_.expandPx * 10)));
        lbMaskFeather_->setText(QString::number(maskSettings_.featherPx, 'f', 1));
        lbMaskExpand_->setText(QString::number(maskSettings_.expandPx, 'f', 1));
    };
    sync();

    if (preview_) preview_->setSelectedPolygon(-1);
    previewMaskDirty_ = true;
    if (loaded_) paintKeyed();
    updateMaskInfo();
    saveMask();
}

void MainWindow::clearMasks() {
    if (masks_.empty()) return;
    pushMaskUndo();
    masks_.clear();
    if (preview_) preview_->setSelectedPolygon(-1);
    previewMaskDirty_ = true;
    if (loaded_) paintKeyed();
    updateMaskInfo();
    saveMask();
}

void MainWindow::updateMaskInfo() {
    int blocks = 0, verts = 0;
    for (const auto& p : masks_) {
        if (p.points.size() < 3) continue;
        ++blocks;
        verts += static_cast<int>(p.points.size());
    }
    if (lbMaskInfo_) {
        if (blocks == 0)
            lbMaskInfo_->setText(QStringLiteral("还没有遮罩"));
        else
            lbMaskInfo_->setText(QStringLiteral("%1 块，共 %2 个顶点").arg(blocks).arg(verts));
    }
    if (btnMaskUndo_) btnMaskUndo_->setEnabled(!maskHistory_.empty());
    if (btnMaskClear_) btnMaskClear_->setEnabled(blocks > 0);
}

void MainWindow::saveMask() {
    if (!loaded_) return;
    const std::string path = ck::defaultMaskPath(info_.path);
    if (masks_.empty()) {
        std::remove(path.c_str());
        return;
    }
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return;
    const std::string json = ck::maskToJson(masks_, maskSettings_);
    std::fwrite(json.data(), 1, json.size(), fp);
    std::fclose(fp);
}

void MainWindow::loadMask() {
    if (!loaded_ || !slMaskFeather_) return;
    const std::string path = ck::defaultMaskPath(info_.path);
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, n);
    std::fclose(fp);

    std::vector<ck::MaskPolygon> polys;
    ck::MaskSettings s;
    if (!ck::maskFromJson(text, &polys, &s)) return;
    masks_ = std::move(polys);
    maskSettings_ = s;
    QSignalBlocker b1(slMaskFeather_), b2(slMaskExpand_);
    slMaskFeather_->setValue(int(std::lround(s.featherPx * 10)));
    slMaskExpand_->setValue(int(std::lround(s.expandPx * 10)));
    lbMaskFeather_->setText(QString::number(s.featherPx, 'f', 1));
    lbMaskExpand_->setText(QString::number(s.expandPx, 'f', 1));
}
void MainWindow::setMaskEditEnabledForTest() {
    if (cbMaskEdit_) cbMaskEdit_->setChecked(true);
}