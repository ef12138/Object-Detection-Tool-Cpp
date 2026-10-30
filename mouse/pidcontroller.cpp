#include "pidcontroller.h"
#include <algorithm>

PIDController::PIDController(double kp, double ki, double kd, double minOutput, double maxOutput)
    : kp_(kp), ki_(ki), kd_(kd),
      setpoint_(0.0), prevError_(0.0), integral_(0.0),
      minOutput_(minOutput), maxOutput_(maxOutput)
{
}

void PIDController::setSetpoint(double setpoint)
{
    setpoint_ = setpoint;
}

void PIDController::reset()
{
    prevError_ = 0.0;
    integral_ = 0.0;
}

double PIDController::compute(double inputValue, double deltaTime)
{
    double error = setpoint_ - inputValue;

    // 比例项
    double proportional = kp_ * error;

    // 积分项
    integral_ += error * deltaTime;
    double integral = ki_ * integral_;

    // 微分项
    double derivative = kd_ * (error - prevError_) / deltaTime;
    prevError_ = error;

    // 总输出
    double output = proportional + integral + derivative;

    // 限制输出范围
    output = std::clamp(output, minOutput_, maxOutput_);

    return output;
}