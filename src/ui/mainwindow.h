#pragma once

#include <QImage>
#include <QMainWindow>
#include <QMutex>
#include <QThread>
#include <atomic>
#include <memory>

#include "core/media.h"

class QLabel;
class QSlider;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QProgressBar;
class PreviewWidget;

// 播放线程: 自己持有一个 Reader, 顺序解码 + 键控, 避免和 UI 线程抢同一个解码器
class PlayThread : public QThread {
    Q_OBJECT
public:
    PlayThread(const QString& path, QMutex* paramMutex, ck::KeyParams* params,
               int outW, int outH, double startT, QObject* parent = nullptr);
    void stop();

signals:
    void frameReady(QImage image, double t);
    void finishedAt(double t);

protected:
    void run() override;

private:
    QString path_;
    QMutex* paramMutex_;
    ck::KeyParams* params_;
    int outW_, outH_;
    double startT_;
    std::atomic<bool> stop_{false};
};

// 导出线程
class ExportThread : public QThread {
    Q_OBJECT
public:
    ExportThread(const QString& path, ck::ExportOptions opt, QObject* parent = nullptr);
    void cancel();

signals:
    void progress(int done, int total);
    void done(bool ok, QString message);

protected:
    void run() override;

private:
    QString path_;
    ck::ExportOptions opt_;
    std::atomic<bool> cancel_{false};
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    bool loadVideo(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private slots:
    void openVideo();
    void applyAutoKey();
    void onParamChanged();
    void onTimelineMoved(int v);
    void onPlayToggled(bool on);
    void onPreviewResChanged();
    void onDisplayChanged();
    void onFormatChanged();
    void startExport();
    void cancelExport();
    void showAbout();
    void showHelp();
    void setTheme(int mode);  // 0=system 1=light 2=dark

private:
    void buildUi();
    void buildMenus();
    void connectSignals();
    void refreshPreview();
    void paintKeyed();
    void updateTimeLabel();
    void updateStats();
    ck::KeyParams collectParams() const;
    QString defaultOutputPath() const;
    double sliderToTime(int v) const;
    int timeToSlider(double t) const;

    ck::Reader reader_;
    ck::MediaInfo info_;
    bool loaded_ = false;
    ck::RgbFrame srcRgb_;
    ck::Image keyed_;
    QImage qimg_;
    double currentT_ = 0.0;
    int previewW_ = 1280, previewH_ = 720;

    QMutex paramMutex_;
    ck::KeyParams params_;

    PlayThread* play_ = nullptr;
    ExportThread* exporter_ = nullptr;
    bool exporting_ = false;

    // 控件
    QPushButton* btnOpen_ = nullptr;
    QLabel* lbFile_ = nullptr;
    PreviewWidget* preview_ = nullptr;
    QPushButton* btnGreen_ = nullptr;
    QPushButton* btnBlue_ = nullptr;
    QPushButton* btnAuto_ = nullptr;
    QLineEdit* editHex_ = nullptr;
    QSlider* slSim_ = nullptr;
    QSlider* slBlend_ = nullptr;
    QSlider* slFeather_ = nullptr;
    QSlider* slDespill_ = nullptr;
    QLabel* lbSim_ = nullptr;
    QLabel* lbBlend_ = nullptr;
    QLabel* lbFeather_ = nullptr;
    QLabel* lbDespill_ = nullptr;
    QCheckBox* cbFeather_ = nullptr;
    QCheckBox* cbDespill_ = nullptr;
    QComboBox* cmbRes_ = nullptr;
    QComboBox* cmbDisplay_ = nullptr;
    QComboBox* cmbFmt_ = nullptr;
    QLabel* lbFmtHint_ = nullptr;
    QLineEdit* editOut_ = nullptr;
    QCheckBox* cbAudio_ = nullptr;
    QPushButton* btnExport_ = nullptr;
    QPushButton* btnCancel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* btnPlay_ = nullptr;
    QSlider* timeline_ = nullptr;
    QLabel* lbTime_ = nullptr;
    QLabel* lbStats_ = nullptr;
    QAction* actExport_ = nullptr;
    class ThemeSwitch* themeSwitch_ = nullptr;
};
