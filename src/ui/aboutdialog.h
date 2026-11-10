// 「关于」对话框: 自绘而不是 QMessageBox, 因为需要一个胶囊样式的 GitHub 链接。
#pragma once

#include <QDialog>

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};
