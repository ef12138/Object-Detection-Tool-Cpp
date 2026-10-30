#ifndef MOUSE_CONTROLLER_H
#define MOUSE_CONTROLLER_H

#include <QObject>
#include <windows.h>
#include <vector>
#include <utility> // for std::pair
#include "pidcontroller.h" // 引入 PID 控制器头文件
#include "bezier_mouse_mover.h" // 引入贝塞尔鼠标移动

// 检测结果结构体，用于传递目标信息
struct DetectionTarget {
    int x;
    int y;
    int width;
    int height;
    int classId;
    float confidence;
};

class MouseController : public QObject
{
    Q_OBJECT

public:
    explicit MouseController(QObject* parent = nullptr);
    ~MouseController();

    // 设置多个目标坐标（推理后的绘制坐标）
    void setTargetPositions(const std::vector<DetectionTarget>& targets);

    // 检查快捷键是否按下并执行鼠标移动（不再需要参数）
    void checkAndMoveMouse();

    // 设置像素移动范围（自动设置正负值）
    void setPixelMoveRange(int range);

    // 设置PID参数
    void setPIDParameters(double kp, double ki, double kd);

    // 设置贝塞尔曲线参数（现在只传递随机系数、步数和速度）
    void setBezierParameters(double randomFactor, int steps, int speed);

    // 设置双快捷键
    void setHotkeys(int hotkey1, int hotkey2);

    // 设置目标点偏移
    void setTargetOffset(double offsetX, double offsetY);

private:
    std::vector<DetectionTarget> m_targets;
    bool m_hasTargets;

    // PID 控制器实例
    PIDController pidX_;
    PIDController pidY_;

    // 贝塞尔鼠标移动实例
    BezierMouseMover m_bezierMover;

    // 屏幕中心点（可以缓存，如果屏幕分辨率不变）
    int screenCenterX_;
    int screenCenterY_;

    // 上一次计算时间（用于计算 deltaTime）
    DWORD lastComputeTime_;

    // 像素移动范围（自动设置正负值）
    int pixelMoveRange_;

    // PID参数
    double kp_;
    double ki_;
    double kd_;

    // 贝塞尔曲线参数
    double m_bezierRandomFactor;  // 随机系数 0.0-1.0
    int m_bezierSteps;

    // 目标点偏移参数
    double m_targetOffsetX;
    double m_targetOffsetY;

    // 快捷键
    int m_hotkey1;
    int m_hotkey2;

    // 计算两个点之间的距离
    double calculateDistance(int x1, int y1, int x2, int y2);

    // 选择离屏幕中心最近的目标
    DetectionTarget selectClosestTarget();

    // 初始化 PID 控制器
    void initializePIDControllers();

    // 更新 PID 控制器的输出限制
    void updatePIDOutputLimits();
};

#endif // MOUSE_CONTROLLER_H