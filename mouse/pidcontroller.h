#ifndef PIDCONTROLLER_H
#define PIDCONTROLLER_H

class PIDController {
public:
    PIDController(double kp, double ki, double kd, double minOutput, double maxOutput);

    // 设置目标值
    void setSetpoint(double setpoint);

    // 计算控制输出
    double compute(double inputValue, double deltaTime /* 时间间隔，单位：秒 */);

    // 重置控制器状态
    void reset();

private:
    double kp_; // 比例增益
    double ki_; // 积分增益
    double kd_; // 微分增益

    double setpoint_; // 目标值
    double prevError_; // 上一次的误差
    double integral_; // 积分累积

    double minOutput_; // 输出最小值
    double maxOutput_; // 输出最大值
};

#endif // PIDCONTROLLER_H