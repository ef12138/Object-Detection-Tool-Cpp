#ifndef BEZIER_MOUSE_MOVER_H
#define BEZIER_MOUSE_MOVER_H

#include <vector>
#include <utility>
#include <random>

class BezierMouseMover
{
public:
    BezierMouseMover();
    ~BezierMouseMover();

    // 使用贝塞尔曲线平滑移动鼠标
    // randomFactor: 0.0-1.0, 0表示直线，越大曲线越弯曲
    void moveWithBezier(int deltaX, int deltaY, int steps = 10, double randomFactor = 0.3);

    // 设置移动速度（现在表示总延迟，单位毫秒）
    void setMoveSpeed(int totalTimeMs);

private:
    // 计算二次贝塞尔曲线点
    std::pair<double, double> quadraticBezier(double t,
                                             double p0x, double p0y,
                                             double p1x, double p1y,
                                             double p2x, double p2y);

    // 计算三次贝塞尔曲线点
    std::pair<double, double> cubicBezier(double t,
                                         double p0x, double p0y,
                                         double p1x, double p1y,
                                         double p2x, double p2y,
                                         double p3x, double p3y);

    // 执行单步鼠标移动
    void performMouseMove(int deltaX, int deltaY);

    int m_moveDelayMs;  // 现在这个变量表示总延迟（毫秒）

    // 随机数生成器
    std::mt19937 m_randomEngine;
    std::uniform_real_distribution<double> m_randomDist;
};

#endif // BEZIER_MOUSE_MOVER_H