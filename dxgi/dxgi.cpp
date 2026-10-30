#include "dxgi.h"
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <memory>
#include <QApplication>
#include <QScreen>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

// 添加全局D3D11设备指针
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;

// 在构造函数中初始化默认截图尺寸
dxgi::dxgi() : m_pDXGIFactory(nullptr), m_pAdapter(nullptr), m_pOutput(nullptr),
               m_pDuplicatedOutput(nullptr), m_captureWidth(640), m_captureHeight(640) {}

dxgi::~dxgi() {
    if (m_pDuplicatedOutput) {
        reinterpret_cast<IDXGIOutputDuplication*>(m_pDuplicatedOutput)->Release();
    }
    if (m_pOutput) {
        reinterpret_cast<IDXGIOutput*>(m_pOutput)->Release();
    }
    if (m_pAdapter) {
        reinterpret_cast<IDXGIAdapter1*>(m_pAdapter)->Release();
    }
    if (m_pDXGIFactory) {
        reinterpret_cast<IDXGIFactory1*>(m_pDXGIFactory)->Release();
    }

    // 释放D3D11设备
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
    }
}

// 添加设置截图尺寸的方法实现
void dxgi::setCaptureSize(int width, int height) {
    m_captureWidth = width;
    m_captureHeight = height;
}

bool dxgi::initialize() {
    // 创建D3D11设备
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext
    );

    if (FAILED(hr)) {
        return false;
    }

    HRESULT hr_factory = CreateDXGIFactory1(__uuidof(IDXGIFactory1), &m_pDXGIFactory);
    if (FAILED(hr_factory)) {
        return false;
    }

    hr_factory = reinterpret_cast<IDXGIFactory1*>(m_pDXGIFactory)->EnumAdapters1(0, reinterpret_cast<IDXGIAdapter1**>(&m_pAdapter));
    if (FAILED(hr_factory)) {
        return false;
    }

    hr_factory = reinterpret_cast<IDXGIAdapter1*>(m_pAdapter)->EnumOutputs(0, reinterpret_cast<IDXGIOutput**>(&m_pOutput));
    if (FAILED(hr_factory)) {
        return false;
    }

    IDXGIOutput1* output1 = nullptr;
    hr_factory = reinterpret_cast<IDXGIOutput*>(m_pOutput)->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
    if (FAILED(hr_factory)) {
        return false;
    }

    hr_factory = output1->DuplicateOutput(reinterpret_cast<IUnknown*>(g_pd3dDevice), reinterpret_cast<IDXGIOutputDuplication**>(&m_pDuplicatedOutput));
    output1->Release();

    return SUCCEEDED(hr_factory);
}

// 修改捕获函数，确保获取最新帧
QImage dxgi::captureCenterScreen() {
    if (!m_pDuplicatedOutput) {
        return QImage();
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    IDXGIResource* desktopResource = nullptr;

    // 首先释放之前可能未释放的帧
    reinterpret_cast<IDXGIOutputDuplication*>(m_pDuplicatedOutput)->ReleaseFrame();

    // 获取最新帧，使用0超时立即返回
    HRESULT hr = reinterpret_cast<IDXGIOutputDuplication*>(m_pDuplicatedOutput)->AcquireNextFrame(0, &frameInfo, &desktopResource);

    if (FAILED(hr)) {
        // 如果没有新帧可用，返回空图像
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return QImage();
        }
        return QImage();
    }

    ID3D11Texture2D* tex = nullptr;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    if (FAILED(hr)) {
        reinterpret_cast<IDXGIOutputDuplication*>(m_pDuplicatedOutput)->ReleaseFrame();
        desktopResource->Release();
        return QImage();
    }

    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    // 获取逻辑分辨率和DPI缩放比例
    QScreen *primaryScreen = QApplication::primaryScreen();
    QRect logicalGeometry;
    qreal dpiScale = 1.0;

    if (primaryScreen) {
        logicalGeometry = primaryScreen->geometry();
        dpiScale = primaryScreen->devicePixelRatio();
    } else {
        // 备用方案：使用物理分辨率
        logicalGeometry = QRect(0, 0, desc.Width, desc.Height);
    }

    // 计算物理到逻辑的缩放比例
    qreal physicalToLogicalScaleX = static_cast<qreal>(logicalGeometry.width()) / desc.Width;
    qreal physicalToLogicalScaleY = static_cast<qreal>(logicalGeometry.height()) / desc.Height;

    // 将逻辑截图尺寸转换为物理尺寸
    int physicalCaptureWidth = static_cast<int>(m_captureWidth / physicalToLogicalScaleX);
    int physicalCaptureHeight = static_cast<int>(m_captureHeight / physicalToLogicalScaleY);

    // 使用成员变量替代参数
    int centerX = desc.Width / 2;
    int centerY = desc.Height / 2;
    int left = centerX - physicalCaptureWidth / 2;
    int top = centerY - physicalCaptureHeight / 2;

    // 确保捕获区域在屏幕范围内
    left = std::max(0, left);
    top = std::max(0, top);
    int right = std::min(static_cast<int>(desc.Width), left + physicalCaptureWidth);
    int bottom = std::min(static_cast<int>(desc.Height), top + physicalCaptureHeight);

    int actualWidth = right - left;
    int actualHeight = bottom - top;

    if (actualWidth <= 0 || actualHeight <= 0) {
        tex->Release();
        reinterpret_cast<IDXGIOutputDuplication*>(m_pDuplicatedOutput)->ReleaseFrame();
        desktopResource->Release();
        return QImage();
    }

    D3D11_BOX box = {
        static_cast<UINT>(left), static_cast<UINT>(top), 0,
        static_cast<UINT>(right), static_cast<UINT>(bottom), 1
    };

    ID3D11Texture2D* texCopy = nullptr;
    D3D11_TEXTURE2D_DESC descCopy = {};
    descCopy.Width = actualWidth;
    descCopy.Height = actualHeight;
    descCopy.MipLevels = 1;
    descCopy.ArraySize = 1;
    descCopy.Format = desc.Format;
    descCopy.SampleDesc.Count = 1;
    descCopy.Usage = D3D11_USAGE_STAGING;
    descCopy.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    // 创建临时纹理用于CPU读取
    hr = g_pd3dDevice->CreateTexture2D(&descCopy, nullptr, &texCopy);
    if (FAILED(hr)) {
        tex->Release();
        reinterpret_cast<IDXGIOutputDuplication*>(m_pDuplicatedOutput)->ReleaseFrame();
        desktopResource->Release();
        return QImage();
    }

    // 复制纹理数据
    g_pd3dDeviceContext->CopySubresourceRegion(texCopy, 0, 0, 0, 0, tex, 0, &box);

    // 映射纹理到CPU内存
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = g_pd3dDeviceContext->Map(texCopy, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        texCopy->Release();
        tex->Release();
        reinterpret_cast<IDXGIOutputDuplication*>(m_pDuplicatedOutput)->ReleaseFrame();
        desktopResource->Release();
        return QImage();
    }

    // 创建QImage并转换BGRA到RGBA
    QImage image(actualWidth, actualHeight, QImage::Format_RGBA8888);
    for (int y = 0; y < actualHeight; ++y) {
        uchar* srcLine = static_cast<uchar*>(mappedResource.pData) + y * mappedResource.RowPitch;
        uchar* dstLine = image.scanLine(y);

        for (int x = 0; x < actualWidth; ++x) {
            // BGRA -> RGBA 转换
            dstLine[x * 4 + 0] = srcLine[x * 4 + 2]; // R
            dstLine[x * 4 + 1] = srcLine[x * 4 + 1]; // G
            dstLine[x * 4 + 2] = srcLine[x * 4 + 0]; // B
            dstLine[x * 4 + 3] = srcLine[x * 4 + 3]; // A
        }
    }

    // 解除映射
    g_pd3dDeviceContext->Unmap(texCopy, 0);

    // 清理资源
    texCopy->Release();
    tex->Release();
    reinterpret_cast<IDXGIOutputDuplication*>(m_pDuplicatedOutput)->ReleaseFrame();
    desktopResource->Release();

    // 如果捕获的物理尺寸与逻辑尺寸不同，需要缩放图像
    if (actualWidth != m_captureWidth || actualHeight != m_captureHeight) {
        image = image.scaled(m_captureWidth, m_captureHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    return image;
}