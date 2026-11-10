// 配色: 与 Python 版同一套 MyPassword 令牌(墨绿/翡翠绿), 跟随系统亮暗。
#pragma once

#include <QString>

class QApplication;

namespace theme {

struct Tokens {
    const char* bg;
    const char* bgSoft;
    const char* panel;
    const char* panelAlt;
    const char* input;
    const char* border;
    const char* borderSoft;
    const char* borderStrong;
    const char* text;
    const char* textSoft;
    const char* muted;
    const char* accent;
    const char* accentStrong;
    const char* accentSoft;
    const char* onAccent;
};

const Tokens& tokens(bool dark);
QString qss(bool dark);
void apply(QApplication* app, bool dark);
bool systemIsDark();

}  // namespace theme
