#ifndef ONNXRUNTIME_H
#define ONNXRUNTIME_H

#include <QImage>
#include <vector>
#include <string>
#include <memory>
#include <onnxruntime_cxx_api.h>
//#include <opencv2/opencv.hpp>

struct DetectionResult {
    int classId;
    float confidence;
    QRect boundingBox;  // x, y, width, height
    std::string className;
};

class OnnxRuntime {
public:
    OnnxRuntime();
    ~OnnxRuntime();

    bool initialize(const std::string& modelPath, bool useGPU = true);
    std::vector<DetectionResult> inference(const QImage& image);
    void setConfidenceThreshold(float threshold) { m_confidenceThreshold = threshold; }
    void setClassNames(const std::vector<std::string>& names) { m_classNames = names; }
    void setNmsThreshold(float threshold) { m_nmsThreshold = threshold; }
    void setGpuDeviceId(int deviceId) { m_gpuDeviceId = deviceId; }  // 新增设置GPU设备ID方法

    // 新增方法：获取模型信息
    int getInputWidth() const { return m_inputWidth; }
    int getInputHeight() const { return m_inputHeight; }
    int getNumClasses() const { return m_numClasses; }
    int getNumBoxes() const { return m_numBoxes; }
    int getGpuDeviceId() const { return m_gpuDeviceId; }  // 新增获取GPU设备ID方法

private:
    QImage preprocessImage(const QImage& inputImage, float& ratio, int& pasteX, int& pasteY);
    std::vector<float> prepareInputTensor(const QImage& image);
    std::vector<DetectionResult> postprocessOutput(const std::vector<float>& output,
                                                   int originalWidth, int originalHeight,
                                                   float ratio, int pasteX, int pasteY);

    Ort::Env m_env;
    Ort::Session m_session{nullptr};

    std::vector<std::string> m_inputNames;
    std::vector<std::string> m_outputNames;
    std::vector<const char*> m_inputNamesChar;
    std::vector<const char*> m_outputNamesChar;

    std::vector<std::string> m_classNames;
    float m_confidenceThreshold = 0.25f;
    float m_nmsThreshold = 0.45f;
    int m_gpuDeviceId = 0;  // 新增GPU设备ID成员变量，默认值为0

    // 动态获取的模型参数
    int m_inputWidth = 640;
    int m_inputHeight = 640;
    int m_inputChannels = 3;
    int m_numClasses = 80;     // 默认值，从输出形状动态获取
    int m_numBoxes = 8400;     // 默认值，从输出形状动态获取
};

#endif // ONNXRUNTIME_H