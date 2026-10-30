#include "mouse_controller.h"
#include "win32.h"
#include <QDebug>
#include <cmath>
#include <windows.h>
#include <QApplication>
#include <QScreen>

MouseController::MouseController(QObject* parent)
    : QObject(parent),
      m_hasTargets(false),
      kp_(1.0), ki_(0.1), kd_(0.05),
      pidX_(kp_, ki_, kd_, -15.0, 15.0), // 默认范围 -15 到 +15 像素
      pidY_(kp_, ki_, kd_, -15.0, 15.0), // 默认范围 -15 到 +15 像素
      m_bezierMover(),
      screenCenterX_(0),
      screenCenterY_(0),
      lastComputeTime_(0),
      pixelMoveRange_(15), // 默认像素移动范围
      m_bezierRandomFactor(0.3),  // 默认随机系数：0.3
      m_bezierSteps(8),   // 默认贝塞尔移动步数
      m_targetOffsetX(0.5), // 默认X偏移：中心
      m_targetOffsetY(0.5), // 默认Y偏移：中心
      m_hotkey1(VK_RBUTTON), // 默认第一个快捷键：右键
      m_hotkey2(VK_XBUTTON1) // 默认第二个快捷键：鼠标侧键1
{
    initializePIDControllers();
}

MouseController::~MouseController()
{
}

void MouseController::setHotkeys(int hotkey1, int hotkey2)
{
    m_hotkey1 = hotkey1;
    m_hotkey2 = hotkey2;
    qDebug() << "Hotkeys updated - Hotkey1:" << m_hotkey1 << "Hotkey2:" << m_hotkey2;
}

void MouseController::setTargetOffset(double offsetX, double offsetY)
{
    m_targetOffsetX = offsetX;
    m_targetOffsetY = offsetY;
    qDebug() << "Target offset updated - X:" << m_targetOffsetX << "Y:" << m_targetOffsetY;
}

void MouseController::setPixelMoveRange(int range)
{
    if (range > 0) {
        pixelMoveRange_ = range;
        updatePIDOutputLimits();
        qDebug() << "Pixel move range updated to:" << range;
    }
}

void MouseController::setBezierParameters(double randomFactor, int steps, int speed)
{
    bool changed = false;

    if (randomFactor >= 0.0 && randomFactor <= 1.0) {
        m_bezierRandomFactor = randomFactor;
        changed = true;
    }

    if (steps > 0 && steps <= 50) {
        m_bezierSteps = steps;
        changed = true;
    }

    if (speed >= 0 && speed <= 100) {
        m_bezierMover.setMoveSpeed(speed);
        changed = true;
    }

    if (changed) {
        qDebug() << "Bezier parameters updated - Random factor:" << m_bezierRandomFactor
                 << "Steps:" << m_bezierSteps << "Speed:" << speed;
    }
}

void MouseController::setPIDParameters(double kp, double ki, double kd)
{
    // 即使参数相同也强制更新，确保PID控制器正确初始化
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;

    // 重新创建 PID 控制器实例以更新参数
    pidX_ = PIDController(kp_, ki_, kd_, -static_cast<double>(pixelMoveRange_), static_cast<double>(pixelMoveRange_));
    pidY_ = PIDController(kp_, ki_, kd_, -static_cast<double>(pixelMoveRange_), static_cast<double>(pixelMoveRange_));

    // 重新设置目标值
    pidX_.setSetpoint(0.0);
    pidY_.setSetpoint(0.0);

    // 重置控制器状态
    pidX_.reset();
    pidY_.reset();

    qDebug() << "PID parameters updated - KP:" << kp_ << "KI:" << ki_ << "KD:" << kd_ << "Range:" << pixelMoveRange_;
}

void MouseController::updatePIDOutputLimits()
{
    // 使用正的范围值，自动设置负的范围
    double range = static_cast<double>(pixelMoveRange_);

    // 重新创建 PID 控制器实例以更新输出限制
    pidX_ = PIDController(kp_, ki_, kd_, -range, range);
    pidY_ = PIDController(kp_, ki_, kd_, -range, range);

    // 重新设置目标值
    pidX_.setSetpoint(0.0);
    pidY_.setSetpoint(0.0);

    // 重置控制器状态
    pidX_.reset();
    pidY_.reset();
}

void MouseController::initializePIDControllers()
{
    // 使用Qt API获取逻辑分辨率并设置屏幕中心
    QScreen *primaryScreen = QApplication::primaryScreen();
    if (primaryScreen) {
        QRect screenGeometry = primaryScreen->geometry();
        screenCenterX_ = screenGeometry.width() / 2;
        screenCenterY_ = screenGeometry.height() / 2;
    }

    // 设置 PID 控制器的目标值为 0，即希望偏移量为 0
    pidX_.setSetpoint(0.0);
    pidY_.setSetpoint(0.0);

    // 记录初始时间
    lastComputeTime_ = GetTickCount();
}

void MouseController::setTargetPositions(const std::vector<DetectionTarget>& targets)
{
    m_targets = targets;
    m_hasTargets = !targets.empty();

    if (m_hasTargets) {
        qDebug() << "Target positions set, count:" << targets.size();
        // 重新初始化 PID 控制器以更新屏幕中心（如果需要）
        initializePIDControllers();
    }
}

double MouseController::calculateDistance(int x1, int y1, int x2, int y2)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

DetectionTarget MouseController::selectClosestTarget()
{
    if (m_targets.empty()) {
        return DetectionTarget{0, 0, 0, 0, -1, 0.0f};
    }

    DetectionTarget closestTarget = m_targets[0];

    // 使用偏移量计算目标点
    int targetCenterX = m_targets[0].x + static_cast<int>(m_targets[0].width * m_targetOffsetX);
    int targetCenterY = m_targets[0].y + static_cast<int>(m_targets[0].height * (1.0 - m_targetOffsetY)); // Y方向反向

    double minDistance = calculateDistance(targetCenterX, targetCenterY, screenCenterX_, screenCenterY_);

    for (size_t i = 1; i < m_targets.size(); ++i) {
        const auto& target = m_targets[i];

        // 使用偏移量计算目标点
        int targetCenterX = target.x + static_cast<int>(target.width * m_targetOffsetX);
        int targetCenterY = target.y + static_cast<int>(target.height * (1.0 - m_targetOffsetY)); // Y方向反向

        double distance = calculateDistance(targetCenterX, targetCenterY, screenCenterX_, screenCenterY_);

        if (distance < minDistance) {
            minDistance = distance;
            closestTarget = target;
        }
    }

    qDebug() << "Selected closest target at distance:" << minDistance;
    return closestTarget;
}

void MouseController::checkAndMoveMouse()
{
    if (!m_hasTargets) {
        return;
    }

    // 检查两个快捷键是否按下（任意一个按下都会触发）
    bool hotkey1Pressed = (GetAsyncKeyState(m_hotkey1) & 0x8000);
    bool hotkey2Pressed = (GetAsyncKeyState(m_hotkey2) & 0x8000);

    if (hotkey1Pressed || hotkey2Pressed) {
        // 选择离屏幕中心最近的目标
        DetectionTarget closestTarget = selectClosestTarget();

        // 使用Qt API获取逻辑分辨率
        QScreen *primaryScreen = QApplication::primaryScreen();
        int screenWidth = 0;
        int screenHeight = 0;
        if (primaryScreen) {
            QRect screenGeometry = primaryScreen->geometry();
            screenWidth = screenGeometry.width();
            screenHeight = screenGeometry.height();
        }

        // 计算屏幕中心点
        int screenCenterX = screenWidth / 2;
        int screenCenterY = screenHeight / 2;

        // 使用偏移量计算目标中心点
        int targetCenterX = closestTarget.x + static_cast<int>(closestTarget.width * m_targetOffsetX);
        int targetCenterY = closestTarget.y + static_cast<int>(closestTarget.height * (1.0 - m_targetOffsetY)); // Y方向反向

        // 计算目标中心点与屏幕中心点的偏移量
        int targetDeltaX = targetCenterX - screenCenterX;
        int targetDeltaY = targetCenterY - screenCenterY;

        qDebug() << "Target DeltaX:" << targetDeltaX << "Target DeltaY:" << targetDeltaY
                 << "Target class:" << closestTarget.classId << "Confidence:" << closestTarget.confidence;

        // 获取当前时间并计算 deltaTime
        DWORD currentTime = GetTickCount();
        double deltaTime = (currentTime - lastComputeTime_) / 1000.0; // 转换为秒
        lastComputeTime_ = currentTime;

        // 防止 deltaTime 过大或过小
        if (deltaTime <= 0.0 || deltaTime > 1.0) { // 假设最大 deltaTime 为 1 秒
            deltaTime = 0.016; // 假设 60 FPS，约 16 ms
        }

        // 使用 PID 控制器计算控制输出
        double controlX = pidX_.compute(static_cast<double>(targetDeltaX), deltaTime);
        double controlY = pidY_.compute(static_cast<double>(targetDeltaY), deltaTime);

        // 将控制输出转换为整数偏移量
        int adjustedDeltaX = -static_cast<int>(controlX);
        int adjustedDeltaY = -static_cast<int>(controlY);

        qDebug() << "PID Adjusted DeltaX:" << adjustedDeltaX << "Adjusted DeltaY:" << adjustedDeltaY
                 << "Move range:" << pixelMoveRange_;

        // 使用贝塞尔曲线移动鼠标（现在传递随机系数）
        if (adjustedDeltaX != 0 || adjustedDeltaY != 0) {
            m_bezierMover.moveWithBezier(adjustedDeltaX, adjustedDeltaY, m_bezierSteps, m_bezierRandomFactor);
        }
    }
}