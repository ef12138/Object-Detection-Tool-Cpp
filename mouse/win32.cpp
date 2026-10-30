//
// Created by Aerse on 25-11-9.
//

#include "win32.h"
#include <windows.h>
#include <cmath>
#include <QApplication>
#include <QScreen>

// 静态成员初始化
bool win32::s_initialized = false;
int win32::s_screenWidth = 0;
int win32::s_screenHeight = 0;

bool win32::initialize() {
    if (s_initialized) {
        return true;
    }

    // 使用Qt API获取主显示器逻辑分辨率
    QScreen *primaryScreen = QApplication::primaryScreen();
    if (primaryScreen) {
        QRect screenGeometry = primaryScreen->geometry();
        s_screenWidth = screenGeometry.width();
        s_screenHeight = screenGeometry.height();
    }

    if (s_screenWidth <= 0 || s_screenHeight <= 0) {
        return false;
    }

    s_initialized = true;
    return true;
}

SIZE win32::getScreenResolution() {
    if (!initialize()) {
        SIZE errorSize = {0, 0};
        return errorSize;
    }

    SIZE size = {s_screenWidth, s_screenHeight};
    return size;
}

bool win32::moveMouse(int x, int y) {
    if (!initialize()) {
        return false;
    }

    // 确保坐标在屏幕范围内
    x = std::max(0, std::min(x, s_screenWidth - 1));
    y = std::max(0, std::min(y, s_screenHeight - 1));

    // 将像素坐标转换为绝对坐标（0-65535）
    int absoluteX = static_cast<int>((static_cast<double>(x) / s_screenWidth) * 65535);
    int absoluteY = static_cast<int>((static_cast<double>(y) / s_screenHeight) * 65535);

    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    input.mi.dx = absoluteX;
    input.mi.dy = absoluteY;

    UINT result = SendInput(1, &input, sizeof(INPUT));
    return (result == 1);
}

bool win32::moveMouseRelative(int deltaX, int deltaY) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx = deltaX;
    input.mi.dy = deltaY;

    UINT result = SendInput(1, &input, sizeof(INPUT));
    return (result == 1);
}