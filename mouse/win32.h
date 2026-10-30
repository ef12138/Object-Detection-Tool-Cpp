//
// Created by Aerse on 25-11-9.
//

#ifndef WIN32_H
#define WIN32_H

#include <windows.h>

class win32 {
public:
    /**
     * @brief 移动鼠标到指定屏幕坐标
     * @param x 目标X坐标（屏幕坐标）
     * @param y 目标Y坐标（屏幕坐标）
     * @return 操作是否成功
     */
    static bool moveMouse(int x, int y);

    /**
     * @brief 相对移动鼠标
     * @param deltaX X轴相对移动量
     * @param deltaY Y轴相对移动量
     * @return 操作是否成功
     */
    static bool moveMouseRelative(int deltaX, int deltaY);

    /**
     * @brief 获取当前屏幕分辨率
     * @return 包含宽度和高度的SIZE结构体
     */
    static SIZE getScreenResolution();

private:
    static bool initialize();
    static bool s_initialized;
    static int s_screenWidth;
    static int s_screenHeight;
};

#endif //WIN32_H
