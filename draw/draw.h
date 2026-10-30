//
// Created by Aerse on 25-11-7.
//

#ifndef DRAW_H
#define DRAW_H

#include <QWidget>
#include <QPainter>
#include <QColor>
#include <QRect>
#include <vector>
#include <string>

// 绘制信息结构体
struct DrawInfo {
    int x;
    int y;
    int width;
    int height;
    std::string text;
    QColor color;
    int thickness;
    float confidence;
};

class DrawWindow : public QWidget {
    Q_OBJECT

public:
    explicit DrawWindow(QWidget* parent = nullptr);
    ~DrawWindow();

    // 添加绘制信息
    void addDrawInfo(const DrawInfo& info);
    // 清空绘制信息
    void clearDrawInfo();
    // 更新绘制
    void updateDraw();

    // 设置性能信息
    void setPerformanceInfo(double captureTime, double inferenceTime, double totalTime, int detectionCount);

    // 重新加载配置
    void reloadConfig();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // 从配置文件加载截图尺寸
    void loadConfig();

    std::vector<DrawInfo> m_drawInfos;
    int m_captureWidth;
    int m_captureHeight;

    // 性能信息
    double m_captureTime;
    double m_inferenceTime;
    double m_totalTime;
    int m_detectionCount;
};

#endif //DRAW_H