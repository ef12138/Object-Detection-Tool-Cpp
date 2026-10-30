#ifndef SCREENCAPTUREWIDGET_H
#define SCREENCAPTUREWIDGET_H

#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QElapsedTimer>
#include <QSettings>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include "dxgi/dxgi.h"
#include "onnxruntime/onnxruntime.h"
#include "draw/draw.h"
#include "mouse/win32.h"
#include "mouse/mouse_controller.h"
#include "mouse/hotkey_edit.h"

// 追踪结构体
struct TrackedTarget {
    int x;
    int y;
    int width;
    int height;
    int classId;
    float confidence;

    // 追踪相关字段
    int trackingId = -1;      // 追踪ID
    int trackingAge = 0;      // 追踪生命周期
};

namespace Ui {
    class ScreenCaptureWidget;
}

class QAbstractNativeEventFilter;

class ScreenCaptureWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScreenCaptureWidget(QWidget* parent = nullptr);
    ~ScreenCaptureWidget();

    // 坐标转换方法
    QPoint convertToScreenCoordinates(int captureX, int captureY);
    QRect convertToScreenRect(int captureX, int captureY, int width, int height);

private:
    void loadConfig();
    void saveConfig();
    void refreshTheme();          // 按系统配色（或 config 强制值）决定用哪套主题
    void applyTheme(bool dark);   // 落到样式表 + 导航图标 + 标题栏
    void initializeModel();
    void updateModelPathFromComboBox();
    void updateScreenCaptureInfo(); // 更新屏幕截图信息

    // 目标追踪相关函数
    void updateTargetTracking(const std::vector<TrackedTarget>& currentTargets);
    TrackedTarget* findClosestEnemyToCrosshair();

private slots:
    void startCapture();
    void captureFrame();
    void updateDrawWindowOnly(); // 新增：仅刷新绘制窗口
    void onConfidenceThresholdChanged(int value);
    void onNmsThresholdChanged(int value);
    void onFrequencyChanged(int value);  // 新增：频率设置
    void onKpChanged(int value);
    void onKiChanged(int value);
    void onKdChanged(int value);
    void onRangeChanged(int value);
    void onModelSelectionChanged(int index);
    void onHotkeyChanged(int key);
    void onHotkey2Changed(int key);  // 新增第二个快捷键槽函数
    void onBezierRandomFactorChanged(int value);  // 修改：随机系数
    void onBezierStepsChanged(int value);
    void onBezierSpeedChanged(int value);
    void onTargetOffsetXChanged(int value);  // 新增：目标点X偏移
    void onTargetOffsetYChanged(int value);  // 新增：目标点Y偏移
    void onToggleDrawWindowClicked(); // 新增：切换绘制窗口显示

private:
    Ui::ScreenCaptureWidget *ui;
    QTimer* m_inferenceTimer;  // 推理定时器
    QTimer* m_drawTimer;       // 绘制刷新定时器（固定频率）
    dxgi* m_dxgi;
    OnnxRuntime* m_onnxRuntime;
    DrawWindow* m_drawWindow;
    MouseController* m_mouseController;
    HotkeyEdit* m_hotkeyEdit;
    HotkeyEdit* m_hotkeyEdit2; // 新增第二个快捷键编辑框
    int m_frameCount = 0;

    // 主题
    QAbstractNativeEventFilter* m_themeWatcher = nullptr;  // 监听系统浅/深色切换
    bool m_darkTheme = false;

    // 配置参数
    std::string m_modelPath;
    float m_confidenceThreshold;
    float m_nmsThreshold;
    int m_frequency;           // 新增：推理频率（毫秒）
    int m_captureWidth;
    int m_captureHeight;
    int m_hotkey;  // 第一个快捷键虚拟键码
    int m_hotkey2; // 新增第二个快捷键虚拟键码

    // PID参数
    double m_kp;
    double m_ki;
    double m_kd;
    int m_pixelRange;

    // 贝塞尔曲线参数
    double m_bezierRandomFactor;  // 随机系数 0.0-1.0
    int m_bezierSteps;
    int m_bezierSpeed;

    // 目标点偏移参数
    double m_targetOffsetX;  // 新增：X偏移（0.0-1.0）
    double m_targetOffsetY;  // 新增：Y偏移（0.0-1.0）

    // 屏幕和截图信息
    int m_screenWidth;
    int m_screenHeight;
    int m_captureLeft;
    int m_captureTop;

    // 可视化状态
    bool m_drawWindowVisible; // 新增：绘制窗口可见状态

    // 目标追踪相关 - 硬编码参数
    std::vector<TrackedTarget> m_trackedTargets;
    const int m_maxTrackingFrames = 5; // 最大追踪帧数
    const double m_trackingDistanceThreshold = 5.0; // 追踪距离阈值
};

#endif // SCREENCAPTUREWIDGET_H