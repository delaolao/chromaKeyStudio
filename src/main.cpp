// ChromaKey Studio — 原生版入口
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QPushButton>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <string>

#include "core/media.h"
#include "ui/aboutdialog.h"
#include "ui/mainwindow.h"

namespace {

// 打包后的无界面自检: 验证 ffmpeg 静态链接、解码、键控、导出、门禁
// 用法: ChromaKeyStudio.exe --selftest <输入视频>
int runSelftest(const QString& input) {
    ck::initFFmpeg();
    QString log;
    auto add = [&log](const QString& s) { log += s + QLatin1Char('\n'); };
    bool ok = true;

    add(QStringLiteral("ChromaKey Studio 原生版 自检"));
    add(QStringLiteral("Qt %1").arg(QString::fromLatin1(qVersion())));

    ck::Reader reader;
    std::string err;
    if (!reader.open(input.toStdString(), &err)) {
        add(QStringLiteral("[x] 打不开输入: %1").arg(QString::fromStdString(err)));
        ok = false;
    } else {
        const auto& info = reader.info();
        add(QStringLiteral("[v] 输入 %1  %2x%3  %4fps  %5s  %6/%7")
                .arg(QString::fromStdString(info.path))
                .arg(info.width).arg(info.height)
                .arg(info.fps, 0, 'f', 2).arg(info.duration, 0, 'f', 2)
                .arg(QString::fromStdString(info.codec),
                     QString::fromStdString(info.pixFmt)));

        // 预览取帧
        ck::RgbFrame rgb;
        if (reader.frameAt(0.2, 1280, 720, rgb, &err)) {
            ck::KeyParams p;
            p.keyR = 0; p.keyG = 177; p.keyB = 64;
            p.similarity = 0.30f; p.blend = 0.15f;
            p.despill = true; p.despillMix = 0.85f;
            p.spillMatte = true; p.neutralize = true;
            ck::Image keyed;
            ck::keyImage(rgb.data.data(), rgb.width, rgb.height, rgb.stride, p, keyed);
            size_t transparent = 0;
            for (int y = 0; y < keyed.height; ++y) {
                const uint8_t* row = keyed.row(y);
                for (int x = 0; x < keyed.width; ++x)
                    if (row[x * 4 + 3] < 128) ++transparent;
            }
            const double pct = 100.0 * transparent /
                               (static_cast<double>(keyed.width) * keyed.height);
            add(QStringLiteral("[v] 取帧+键控正常, 透明像素 %1%").arg(pct, 0, 'f', 1));
            if (pct < 5.0) { add(QStringLiteral("[x] 抠像结果异常")); ok = false; }
        } else {
            add(QStringLiteral("[x] 取帧失败: %1").arg(QString::fromStdString(err)));
            ok = false;
        }

        // 三种格式各导一小段
        for (auto fmt : {ck::OutFormat::QuickTimeRGBA, ck::OutFormat::MovPng,
                         ck::OutFormat::ProRes4444, ck::OutFormat::WebM}) {
            ck::ExportOptions opt;
            opt.format = fmt;
            opt.outPath = (QCoreApplication::applicationDirPath() + "/selftest_" +
                           QString::number(static_cast<int>(fmt)) +
                           QLatin1String(ck::formatSuffix(fmt)))
                              .toStdString();
            opt.key.keyR = 0; opt.key.keyG = 177; opt.key.keyB = 64;
            opt.key.similarity = 0.30f; opt.key.blend = 0.15f;
            opt.key.despill = true; opt.key.despillMix = 0.85f;
            opt.key.spillMatte = true; opt.key.neutralize = true;
            opt.includeAudio = true;
            auto res = ck::exportVideo(reader, opt, nullptr, nullptr);
            if (!res.ok) {
                add(QStringLiteral("[x] %1 导出失败: %2")
                        .arg(QString::fromLatin1(ck::formatLabel(fmt)),
                             QString::fromStdString(res.error)));
                ok = false;
                continue;
            }
            auto check = ck::checkAlpha(opt.outPath, fmt);
            add(QStringLiteral("[%1] %2: %3 帧, %4, 音频 %5, 门禁 %6")
                    .arg(check.ok ? QStringLiteral("v") : QStringLiteral("x"),
                         QString::fromLatin1(ck::formatLabel(fmt)))
                    .arg(res.frames)
                    .arg(QString::fromStdString(res.pixFmt),
                         QString::fromStdString(res.audioInfo),
                         QString::fromStdString(check.message)));
            QFile::remove(QString::fromStdString(opt.outPath));
            if (!check.ok) ok = false;
        }
    }

    add(QString());
    add(ok ? QStringLiteral("结论: 全部通过，可以正常使用。")
           : QStringLiteral("结论: 有问题，请把本文件发给开发者。"));
    QFile f(QCoreApplication::applicationDirPath() + QStringLiteral("/selftest.log"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(log.toUtf8());
        f.close();
    }
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ChromaKey Studio"));
    app.setApplicationVersion(QStringLiteral("0.2.1"));
    app.setOrganizationName(QStringLiteral("ChromaKeyStudio"));
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    const QStringList args = QCoreApplication::arguments();
    if (args.size() >= 3 && args[1] == QLatin1String("--selftest"))
        return runSelftest(args[2]);

    MainWindow w;
    w.show();

    // 调试/验收用: --open <视频> --screenshot <png> 可以任意组合
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == QLatin1String("--open"))
            w.loadVideo(args[i + 1]);
        else if (args[i] == QLatin1String("--screenshot")) {
            const QString out = args[i + 1];
            QTimer::singleShot(2500, [&w, out] {
                w.grab().save(out);
                QCoreApplication::quit();
            });
        } else if (args[i] == QLatin1String("--about-shot")) {
            const QString out = args[i + 1];
            const bool focusPill = args.contains(QLatin1String("--focus"));
            auto* dlg = new AboutDialog(&w);
            dlg->show();
            if (focusPill) {
                if (auto* pill = dlg->findChild<QPushButton*>(QStringLiteral("githubPill")))
                    pill->setFocus(Qt::OtherFocusReason);
            }
            QTimer::singleShot(1200, [dlg, out] {
                dlg->grab().save(out);
                QCoreApplication::quit();
            });
        }
    }
    return app.exec();
}
