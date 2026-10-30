#ifndef DXGI_H
#define DXGI_H

#include <QImage>

class dxgi {
public:
    dxgi();
    ~dxgi();

    bool initialize();
    QImage captureCenterScreen();  // 移除参数，使用内部尺寸

    // 添加设置截图尺寸的方法
    void setCaptureSize(int width, int height);
    int getCaptureWidth() const { return m_captureWidth; }
    int getCaptureHeight() const { return m_captureHeight; }

private:
    void* m_pDXGIFactory;
    void* m_pAdapter;
    void* m_pOutput;
    void* m_pDuplicatedOutput;

    // 添加截图尺寸成员变量
    int m_captureWidth;
    int m_captureHeight;
};

#endif // DXGI_H