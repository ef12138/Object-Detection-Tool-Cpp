//
// Created by Aerse on 25-11-7.
//

#include "draw.h"
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QSettings>
#include <QFile>
#include <QDebug>
#include <windows.h>

DrawWindow::DrawWindow(QWidget* parent)
    : QWidget(parent), m_captureTime(0), m_inferenceTime(0), m_totalTime(0), m_detectionCount(0) {

    // 初始化默认的截图尺寸
    m_captureWidth = 640;
    m_captureHeight = 640;

    // 从配置文件读取截图尺寸
    loadConfig();

    // 设置窗口属性为透明
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents); // 鼠标事件穿透
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);

    // 关键：设置分层窗口，这样DXGI就不会捕获到它
    SetWindowLong((HWND)winId(), GWL_EXSTYLE,
                  GetWindowLong((HWND)winId(), GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT);

    // 设置窗口不参与截图 - 这是关键设置
    SetWindowDisplayAffinity((HWND)winId(), WDA_EXCLUDEFROMCAPTURE);

    // 设置窗口覆盖整个屏幕 - 使用 Qt6 的新 API
    QScreen *primaryScreen = QApplication::primaryScreen();
    if (primaryScreen) {
        QRect screenGeometry = primaryScreen->geometry();
        setGeometry(screenGeometry);
    }
    // 设置样式表确保透明
    setStyleSheet("background: transparent;");
}

DrawWindow::~DrawWindow() {
}

void DrawWindow::loadConfig() {
    // 检查配置文件是否存在
    if (!QFile::exists("config.ini")) {
        qWarning("Configuration file config.ini does not exist, using default size 640x640");
        return;
    }

    QSettings settings("config.ini", QSettings::IniFormat);

    // 读取 Capture 部分的配置
    settings.beginGroup("Capture");
    m_captureWidth = settings.value("Width", 640).toInt();
    m_captureHeight = settings.value("Height", 640).toInt();
    settings.endGroup();

    // 验证读取的值
    if (m_captureWidth <= 0 || m_captureHeight <= 0) {
        qWarning("The screenshot size in the configuration file is invalid, using the default size 640x640");
        m_captureWidth = 640;
        m_captureHeight = 640;
    }

    qDebug() << "DrawWindow loaded config - Width:" << m_captureWidth << "Height:" << m_captureHeight;
}

void DrawWindow::reloadConfig() {
    loadConfig();
    update(); // 重新绘制
}

void DrawWindow::addDrawInfo(const DrawInfo& info) {
    m_drawInfos.push_back(info);
}

void DrawWindow::clearDrawInfo() {
    m_drawInfos.clear();
}

void DrawWindow::updateDraw() {
    update(); // 触发重绘
}

void DrawWindow::setPerformanceInfo(double captureTime, double inferenceTime, double totalTime, int detectionCount) {
    m_captureTime = captureTime;
    m_inferenceTime = inferenceTime;
    m_totalTime = totalTime;
    m_detectionCount = detectionCount;
}

void DrawWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 绘制性能信息（在左上角）- 字体调小
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 8, QFont::Bold));

    QString performanceText = QString("Capture: %1s | Inference: %2s | Total: %3s | Detections: %4")
                             .arg(m_captureTime, 0, 'f', 3)
                             .arg(m_inferenceTime, 0, 'f', 3)
                             .arg(m_totalTime, 0, 'f', 3)
                             .arg(m_detectionCount);

    // 绘制文本背景（提高可读性）
    QFontMetrics metrics(painter.font());
    QRect textRect = metrics.boundingRect(performanceText);
    textRect.moveTo(10, 10);
    textRect.adjust(-5, -5, 5, 5);

    painter.fillRect(textRect, QColor(0, 0, 0, 180)); // 半透明黑色背景
    painter.drawText(10, 10 + metrics.ascent(), performanceText); // 调整文字垂直位置

    // 定义浅白色
    QColor lightWhite(210, 210, 210);

    // 绘制所有检测信息（现在已经是屏幕坐标）
    for (const auto& info : m_drawInfos) {
        int screenX = info.x;
        int screenY = info.y;
        int screenRight = screenX + info.width;
        int screenBottom = screenY + info.height;

        // 设置画笔 - 线条调细
        QPen pen(info.color);
        pen.setWidth(1);
        painter.setPen(pen);

        // 动态计算角长度 - 基于检测框大小的百分比，但有最小和最大限制
        int minDimension = qMin(info.width, info.height);

        // 计算角长度：取检测框较小边长的15%，但限制在5到20像素之间
        int cornerLength = qBound(5, minDimension / 7, 20);

        // 绘制左上角 L 形
        painter.drawLine(screenX, screenY, screenX + cornerLength, screenY); // 上横线
        painter.drawLine(screenX, screenY, screenX, screenY + cornerLength); // 左竖线

        // 绘制右上角 L 形
        painter.drawLine(screenRight - cornerLength, screenY, screenRight, screenY); // 上横线
        painter.drawLine(screenRight, screenY, screenRight, screenY + cornerLength); // 右竖线

        // 绘制左下角 L 形
        painter.drawLine(screenX, screenBottom - cornerLength, screenX, screenBottom); // 左竖线
        painter.drawLine(screenX, screenBottom, screenX + cornerLength, screenBottom); // 下横线

        // 绘制右下角 L 形
        painter.drawLine(screenRight - cornerLength, screenBottom, screenRight, screenBottom); // 下横线
        painter.drawLine(screenRight, screenBottom - cornerLength, screenRight, screenBottom); // 右竖线

        /* 绘制血条和文字
        if (!info.text.empty()) {
            QFont font = painter.font();
            font.setPointSize(8);
            font.setBold(true);
            painter.setFont(font);

            // 计算血条位置（在矩形框上方）
            int healthBarWidth = info.width;
            int healthBarHeight = 6; // 血条高度
            int healthBarX = screenX;
            int healthBarY = screenY - healthBarHeight - 2; // 在框上方留2像素间隔

            // 绘制血条背景（灰色）
            painter.fillRect(healthBarX, healthBarY, healthBarWidth, healthBarHeight, QColor(100, 100, 100, 150));

            // 绘制血条前景（浅白色，根据置信度填充）
            int filledWidth = static_cast<int>(healthBarWidth * info.confidence);
            painter.fillRect(healthBarX, healthBarY, filledWidth, healthBarHeight, lightWhite);

            // 绘制文字（在血条上方，浅白色，无背景）
            QString qtext = QString::fromStdString(info.text);
            int textX = screenX;
            int textY = healthBarY - 2; // 血条上方留2像素间隔

            painter.setPen(lightWhite); // 使用浅白色
            painter.drawText(textX, textY, qtext);
        }
        */
    }
}