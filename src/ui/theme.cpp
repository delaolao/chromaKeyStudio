#include "theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QString>

namespace theme {
namespace {

const Tokens kDark{
    "#06100e", "#091714", "#0e211c", "#0c1c18", "#081a16",
    "#23453c", "#1a332c", "#356455",
    "#edfdf7", "#c9e6dc", "#87a9a0",
    "#34d399", "#10b981", "rgba(52,211,153,0.14)", "#04120e"};

const Tokens kLight{
    "#f2f7f4", "#eaf3ee", "#ffffff", "#f7fcf9", "#ffffff",
    "#d6e4de", "#e3ede8", "#b7d1c7",
    "#11251f", "#29483e", "#5f7a71",
    "#0f9f78", "#0b8a68", "rgba(15,159,120,0.10)", "#ffffff"};

}  // namespace

const Tokens& tokens(bool dark) { return dark ? kDark : kLight; }

bool systemIsDark() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

void apply(QApplication* app, bool dark) {
    const Tokens& t = tokens(dark);
    app->setStyle("Fusion");

    QPalette p;
    p.setColor(QPalette::Window, QColor(t.bg));
    p.setColor(QPalette::WindowText, QColor(t.text));
    p.setColor(QPalette::Base, QColor(t.input));
    p.setColor(QPalette::AlternateBase, QColor(t.bgSoft));
    p.setColor(QPalette::Text, QColor(t.text));
    p.setColor(QPalette::Button, QColor(t.panel));
    p.setColor(QPalette::ButtonText, QColor(t.text));
    p.setColor(QPalette::Highlight, QColor(t.accent));
    p.setColor(QPalette::HighlightedText, QColor(t.onAccent));
    p.setColor(QPalette::ToolTipBase, QColor(t.panel));
    p.setColor(QPalette::ToolTipText, QColor(t.text));
    p.setColor(QPalette::PlaceholderText, QColor(t.muted));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(t.muted));
    app->setPalette(p);
    app->setStyleSheet(qss(dark));
}

QString qss(bool dark) {
    const Tokens& t = tokens(dark);
    QString s = QStringLiteral(
        "QWidget{background-color:%1;color:%2;font-family:'Microsoft YaHei UI','Segoe UI';font-size:9pt;}"
        "QMainWindow,QDialog{background-color:%1;}"
        "QMenuBar{background-color:%1;color:%12;border-bottom:1px solid %7;}"
        "QMenuBar::item{padding:4px 10px;background:transparent;border-radius:4px;}"
        "QMenuBar::item:selected{background:%10;color:%2;}"
        "QMenu{background-color:%3;border:1px solid %6;border-radius:6px;padding:4px;}"
        "QMenu::item{padding:6px 22px 6px 14px;border-radius:4px;}"
        "QMenu::item:selected{background:%10;color:%2;}"
        "QMenu::separator{height:1px;background:%7;margin:4px 8px;}"
        "QStatusBar{background-color:%4;color:%11;border-top:1px solid %7;}"
        "QStatusBar::item{border:none;}"
        "QGroupBox{background-color:%3;border:1px solid %7;border-radius:8px;"
        "margin-top:16px;padding:10px 10px 8px 10px;font-weight:600;color:%9;}"
        "QGroupBox::title{subcontrol-origin:margin;subcontrol-position:top left;"
        "left:10px;top:2px;padding:0 4px;color:%12;}"
        "QLabel{background:transparent;}"
        "QLabel:disabled{color:%11;}"
        "QLabel#hint{color:%11;}"
        "QPushButton{background-color:%4;color:%2;border:1px solid %6;"
        "border-radius:6px;padding:6px 12px;min-height:18px;}"
        "QPushButton:hover{border-color:%8;}"
        "QPushButton:pressed{background-color:%7;}"
        "QPushButton:disabled{color:%11;border-color:%7;background-color:%4;}"
        "QPushButton:checked{background-color:%12;color:%14;border-color:%13;font-weight:600;}"
        "QPushButton#iconbtn{padding:2px;min-width:26px;}"
        "QLineEdit,QComboBox{background-color:%5;color:%2;border:1px solid %6;"
        "border-radius:6px;padding:4px 8px;}"
        "QLineEdit:focus,QComboBox:focus{border-color:%12;}"
        "QLineEdit:disabled,QComboBox:disabled{color:%11;background-color:%4;}"
        "QComboBox::drop-down{border:none;width:18px;}"
        "QComboBox QAbstractItemView{background-color:%3;color:%2;border:1px solid %6;"
        "selection-background-color:%12;selection-color:%14;outline:none;}"
        "QCheckBox{background:transparent;spacing:6px;}"
        "QCheckBox::indicator{width:14px;height:14px;border:1px solid %8;"
        "border-radius:3px;background-color:%5;}"
        "QCheckBox::indicator:checked{background-color:%12;border-color:%12;}"
        "QCheckBox:disabled{color:%11;}"
        "QSlider::groove:horizontal{height:4px;border-radius:2px;background:%7;}"
        "QSlider::sub-page:horizontal{background:%12;border-radius:2px;}"
        "QSlider::handle:horizontal{width:12px;height:12px;margin:-5px 0;"
        "border-radius:6px;background:%12;border:1px solid %13;}"
        "QSlider::handle:horizontal:hover{background:%13;}"
        "QProgressBar{background-color:%4;border:1px solid %6;border-radius:6px;"
        "text-align:center;color:%2;min-height:18px;}"
        "QProgressBar::chunk{background-color:%12;border-radius:5px;}"
        "QScrollArea{background:transparent;border:none;}"
        "QScrollBar:vertical{background:transparent;width:10px;margin:2px;}"
        "QScrollBar::handle:vertical{background:%8;border-radius:5px;min-height:26px;}"
        "QScrollBar::handle:vertical:hover{background:%12;}"
        "QScrollBar::add-line,QScrollBar::sub-line{height:0;}"
        "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}"
        "QToolTip{background-color:%3;color:%2;border:1px solid %6;padding:4px;}");

    // 顺序必须和上面的 %1..%14 一一对应:
    // bg, text, panel, panelAlt, input, border, borderSoft, borderStrong,
    // textSoft, muted, muted, accent, accentStrong, onAccent
    const QString vals[14] = {t.bg,   t.text,  t.panel,       t.panelAlt,
                              t.input, t.border, t.borderSoft, t.borderStrong,
                              t.textSoft, t.muted, t.muted,   t.accent,
                              t.accentStrong, t.onAccent};
    // 先替换两位数占位符, 否则 %1 会把 %12 的前半截吃掉
    for (int i = 14; i >= 10; --i)
        s.replace(QStringLiteral("%") + QString::number(i), vals[i - 1]);
    for (int i = 9; i >= 1; --i)
        s.replace(QStringLiteral("%") + QString::number(i), vals[i - 1]);
    return s;
}

}  // namespace theme
