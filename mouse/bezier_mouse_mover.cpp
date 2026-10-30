#include "bezier_mouse_mover.h"
#include "win32.h"
#include <QDebug>
#include <thread>
#include <chrono>
#include <random>

BezierMouseMover::BezierMouseMover()
    : m_moveDelayMs(10),  // 现在表示总延迟10ms
      m_randomEngine(std::random_device{}()),
      m_randomDist(0.0, 1.0)
{
}

BezierMouseMover::~BezierMouseMover()
{
}

void BezierMouseMover::setMoveSpeed(int totalTimeMs)
{
    if (totalTimeMs >= 0) {
        m_moveDelayMs = totalTimeMs;  // 现在这个参数表示总延迟
    }
}

std::pair<double, double> BezierMouseMover::quadraticBezier(double t,
                                                           double p0x, double p0y,
                                                           double p1x, double p1y,
                                                           double p2x, double p2y)
{
    double u = 1 - t;
    double tt = t * t;
    double uu = u * u;

    double x = uu * p0x + 2 * u * t * p1x + tt * p2x;
    double y = uu * p0y + 2 * u * t * p1y + tt * p2y;

    return std::make_pair(x, y);
}

std::pair<double, double> BezierMouseMover::cubicBezier(double t,
                                                       double p0x, double p0y,
                                                       double p1x, double p1y,
                                                       double p2x, double p2y,
                                                       double p3x, double p3y)
{
    double u = 1 - t;
    double tt = t * t;
    double ttt = tt * t;
    double uu = u * u;
    double uuu = uu * u;

    double x = uuu * p0x + 3 * uu * t * p1x + 3 * u * tt * p2x + ttt * p3x;
    double y = uuu * p0y + 3 * uu * t * p1y + 3 * u * tt * p2y + ttt * p3y;

    return std::make_pair(x, y);
}

void BezierMouseMover::performMouseMove(int deltaX, int deltaY)
{
    if (deltaX != 0 || deltaY != 0) {
        win32::moveMouseRelative(deltaX, deltaY);
    }
}

void BezierMouseMover::moveWithBezier(int deltaX, int deltaY, int steps, double randomFactor)
{
    if (steps <= 0 || (deltaX == 0 && deltaY == 0)) {
        return;
    }

    // 确保随机系数在合理范围内
    if (randomFactor < 0.0) randomFactor = 0.0;
    if (randomFactor > 1.0) randomFactor = 1.0;

    double p1, p2;

    if (randomFactor == 0.0) {
        // 随机系数为0，使用直线（P1=0.0, P2=1.0）
        p1 = 0.0;
        p2 = 1.0;
    } else {
        // 生成随机的P1和P2
        // P1在[0, randomFactor]范围内随机
        // P2在[1-randomFactor, 1]范围内随机
        p1 = m_randomDist(m_randomEngine) * randomFactor;
        p2 = 1.0 - m_randomDist(m_randomEngine) * randomFactor;

        // 确保P1 < P2
        if (p1 >= p2) {
            std::swap(p1, p2);
        }

        // 确保最小值限制
        if (p1 < 0.01) p1 = 0.01;
        if (p2 > 0.99) p2 = 0.99;
    }

    // 使用三次贝塞尔曲线
    // P0: 起点 (0, 0)
    // P1: 控制点1 (deltaX * p1, deltaY * p1)
    // P2: 控制点2 (deltaX * p2, deltaY * p2)
    // P3: 终点 (deltaX, deltaY)

    double p0x = 0.0, p0y = 0.0;
    double p1x = deltaX * p1, p1y = deltaY * p1;
    double p2x = deltaX * p2, p2y = deltaY * p2;
    double p3x = deltaX, p3y = deltaY;

    std::vector<std::pair<double, double>> points;
    points.reserve(steps + 1);

    // 生成贝塞尔曲线上的点
    for (int i = 0; i <= steps; ++i) {
        double t = static_cast<double>(i) / steps;
        auto point = cubicBezier(t, p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y);
        points.push_back(point);
    }

    // 计算每一步的延迟时间：总延迟除以步数
    int stepDelayMs = (steps > 0 && m_moveDelayMs > 0) ?
                     m_moveDelayMs / steps : 0;

    // 执行移动
    int prevX = 0, prevY = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        int currentX = static_cast<int>(points[i].first);
        int currentY = static_cast<int>(points[i].second);

        int moveX = currentX - prevX;
        int moveY = currentY - prevY;

        performMouseMove(moveX, moveY);

        prevX = currentX;
        prevY = currentY;

        // 添加延迟以控制总移动时间
        if (stepDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(stepDelayMs));
        }
    }

    qDebug() << "Bezier mouse move - Delta:" << deltaX << "," << deltaY
             << "Steps:" << steps << "Random factor:" << randomFactor
             << "P1:" << p1 << "P2:" << p2
             << "Total time:" << m_moveDelayMs << "ms";
}