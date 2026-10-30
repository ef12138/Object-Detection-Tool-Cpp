#include "onnxruntime.h"
#include <QDebug>
#include <QPainter>
#include <codecvt>
#include <locale>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <QSettings>

OnnxRuntime::OnnxRuntime()
    : m_env(ORT_LOGGING_LEVEL_WARNING, "YOLO_Inference") {
}

OnnxRuntime::~OnnxRuntime() {
    if (m_session) {
        m_session.release();
    }
}

bool OnnxRuntime::initialize(const std::string& modelPath, bool useGPU) {
    try {
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        if (useGPU) {
            // 使用 DirectML 执行提供器
            try {
                // 直接在这里读取配置文件
                QSettings config("config.ini", QSettings::IniFormat);
                int gpuDeviceId = config.value("Inference/GpuDeviceId", 0).toInt();

                std::unordered_map<std::string, std::string> dml_provider_options = {
                    {"device_id", std::to_string(gpuDeviceId)},  // 使用从配置文件读取的 GPU 设备ID
                    // 可以添加其他 DirectML 特定选项
                    // {"device_type", "default"}, // default, d3d12, d3d11
                };

                sessionOptions.AppendExecutionProvider("DML", dml_provider_options);
                qDebug() << "Using DirectML for GPU inference on device:" << gpuDeviceId;

            } catch (const Ort::Exception& dmlError) {
                qDebug() << "DirectML initialization failed:" << dmlError.what();
                qDebug() << "Falling back to CPU inference";
                // DirectML 失败时回退到 CPU
            }
        } else {
            qDebug() << "Using CPU for inference";
        }


        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::wstring wideModelPath = converter.from_bytes(modelPath);
        m_session = Ort::Session(m_env, wideModelPath.c_str(), sessionOptions);

        Ort::AllocatorWithDefaultOptions allocator;

        // 输入信息
        size_t numInputNodes = m_session.GetInputCount();
        m_inputNames.clear();
        m_inputNamesChar.clear();

        for (size_t i = 0; i < numInputNodes; i++) {
            Ort::AllocatedStringPtr inputNamePtr = m_session.GetInputNameAllocated(i, allocator);
            std::string inputName = inputNamePtr.get();
            m_inputNames.push_back(inputName);

            auto typeInfo = m_session.GetInputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto inputShape = tensorInfo.GetShape();

            // 动态获取输入尺寸
            if (inputShape.size() >= 4) {
                m_inputHeight = static_cast<int>(inputShape[2]);
                m_inputWidth = static_cast<int>(inputShape[3]);
                m_inputChannels = static_cast<int>(inputShape[1]);
            }

            qDebug() << "Input" << i << "name:" << inputName.c_str();
            qDebug() << "Input shape:";
            for (auto dim : inputShape) {
                qDebug() << dim;
            }
            qDebug() << "Input size:" << m_inputWidth << "x" << m_inputHeight << "x" << m_inputChannels;
        }

        // 输出信息
        size_t numOutputNodes = m_session.GetOutputCount();
        m_outputNames.clear();
        m_outputNamesChar.clear();

        for (size_t i = 0; i < numOutputNodes; i++) {
            Ort::AllocatedStringPtr outputNamePtr = m_session.GetOutputNameAllocated(i, allocator);
            std::string outputName = outputNamePtr.get();
            m_outputNames.push_back(outputName);

            auto typeInfo = m_session.GetOutputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto outputShape = tensorInfo.GetShape();

            // 动态获取输出形状并计算类别数和检测框数
            if (outputShape.size() == 3) {
                // 输出形状为 [1, num_classes + 4, num_boxes]
                m_numBoxes = static_cast<int>(outputShape[2]);
                m_numClasses = static_cast<int>(outputShape[1]) - 4; // 减去4个坐标通道

                qDebug() << "Output shape: [1," << outputShape[1] << "," << outputShape[2] << "]";
                qDebug() << "Detected num_classes:" << m_numClasses << ", num_boxes:" << m_numBoxes;
            }

            qDebug() << "Output" << i << "name:" << outputName.c_str();
            qDebug() << "Output shape:";
            for (auto dim : outputShape) {
                qDebug() << dim;
            }
        }

        for (const auto& name : m_inputNames) {
            m_inputNamesChar.push_back(name.c_str());
        }
        for (const auto& name : m_outputNames) {
            m_outputNamesChar.push_back(name.c_str());
        }

        // 如果未设置类别名称，使用默认名称或根据类别数生成
        if (m_classNames.empty()) {
            m_classNames.resize(m_numClasses);
            for (int i = 0; i < m_numClasses; ++i) {
                m_classNames[i] = "Class_" + std::to_string(i);
            }
            qDebug() << "Using default class names, count:" << m_numClasses;
        } else if (m_classNames.size() != m_numClasses) {
            qDebug() << "Warning: Provided class names count (" << m_classNames.size()
                     << ") doesn't match model num_classes (" << m_numClasses << ")";
        }

        qDebug() << "ONNX Runtime initialized successfully";
        qDebug() << "Model info - Input:" << m_inputWidth << "x" << m_inputHeight
                 << ", Classes:" << m_numClasses << ", Boxes:" << m_numBoxes;
        return true;

    } catch (const Ort::Exception& e) {
        qDebug() << "ONNX Runtime initialization failed:" << e.what();
        return false;
    }
}

std::vector<DetectionResult> OnnxRuntime::inference(const QImage& image) {
    if (!m_session) {
        qDebug() << "Session not initialized";
        return {};
    }

    if (image.isNull() || image.width() == 0 || image.height() == 0) {
        qDebug() << "Invalid input image";
        return {};
    }

    try {
        float ratio;
        int pasteX, pasteY;

        // 预处理图像 - 使用动态获取的输入尺寸
        QImage processedImage = preprocessImage(image, ratio, pasteX, pasteY);

        // 准备输入张量（通道优先 CHW）
        std::vector<float> inputTensor = prepareInputTensor(processedImage);

        // 创建输入张量 [1, 3, H, W] - 使用动态尺寸
        std::vector<int64_t> inputShape = {1, m_inputChannels, m_inputHeight, m_inputWidth};
        size_t inputTensorSize = inputTensor.size();

        auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensorValue = Ort::Value::CreateTensor<float>(
            memoryInfo, inputTensor.data(), inputTensorSize,
            inputShape.data(), inputShape.size()
        );

        if (m_inputNamesChar.empty() || m_outputNamesChar.empty()) {
            qDebug() << "Input or output names are empty";
            return {};
        }

        // 运行推理
        auto outputTensors = m_session.Run(
            Ort::RunOptions{nullptr},
            m_inputNamesChar.data(), &inputTensorValue, 1,
            m_outputNamesChar.data(), m_outputNamesChar.size()
        );

        if (outputTensors.size() == 0) {
            qDebug() << "No output tensors received";
            return {};
        }

        if (!outputTensors[0].IsTensor()) {
            qDebug() << "Output is not a tensor";
            return {};
        }

        // 获取输出数据
        float* outputData = outputTensors[0].GetTensorMutableData<float>();
        auto outputShape = outputTensors[0].GetTensorTypeAndShapeInfo().GetShape();
        size_t outputSize = outputTensors[0].GetTensorTypeAndShapeInfo().GetElementCount();

        if (outputSize == 0) {
            qDebug() << "Empty output tensor";
            return {};
        }

        std::vector<float> output(outputData, outputData + outputSize);

        // 后处理 - 使用动态获取的参数
        auto results = postprocessOutput(output, image.width(), image.height(), ratio, pasteX, pasteY);

        return results;

    } catch (const Ort::Exception& e) {
        qDebug() << "Inference failed:" << e.what();
        return {};
    } catch (const std::exception& e) {
        qDebug() << "Standard exception during inference:" << e.what();
        return {};
    }
}

QImage OnnxRuntime::preprocessImage(const QImage& inputImage, float& ratio, int& pasteX, int& pasteY) {
    // 转换为RGB888格式
    QImage rgbImage = inputImage.convertToFormat(QImage::Format_RGB888);

    // 计算缩放比例（保持宽高比）- 使用动态输入尺寸
    float ratio_h = static_cast<float>(m_inputHeight) / rgbImage.height();
    float ratio_w = static_cast<float>(m_inputWidth) / rgbImage.width();
    ratio = std::min(ratio_h, ratio_w);

    int new_w = static_cast<int>(rgbImage.width() * ratio);
    int new_h = static_cast<int>(rgbImage.height() * ratio);

    // 缩放图像
    QImage resizedImage = rgbImage.scaled(new_w, new_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // 创建灰色画布 (RGB 114,114,114) - 使用动态输入尺寸
    QImage canvas(m_inputWidth, m_inputHeight, QImage::Format_RGB888);
    canvas.fill(QColor(114, 114, 114));

    // 计算粘贴位置（居中）
    pasteX = (m_inputWidth - new_w) / 2;
    pasteY = (m_inputHeight - new_h) / 2;

    // 将缩放后的图像粘贴到画布上
    QPainter painter(&canvas);
    painter.drawImage(pasteX, pasteY, resizedImage);
    painter.end();

    return canvas;
}

std::vector<float> OnnxRuntime::prepareInputTensor(const QImage& image) {
    std::vector<float> inputTensor;
    inputTensor.reserve(m_inputChannels * m_inputHeight * m_inputWidth);

    // 通道优先 (CHW) 格式：先所有R通道，再所有G通道，最后所有B通道
    for (int c = 0; c < 3; ++c) { // 假设总是3通道
        for (int y = 0; y < m_inputHeight; ++y) {
            const uchar* scanLine = image.constScanLine(y);
            for (int x = 0; x < m_inputWidth; ++x) {
                // 归一化到[0,1]，通道优先存储
                float pixel = scanLine[x * 3 + c] / 255.0f;
                inputTensor.push_back(pixel);
            }
        }
    }
    return inputTensor;
}

std::vector<DetectionResult> OnnxRuntime::postprocessOutput(
    const std::vector<float>& output, int originalWidth, int originalHeight,
    float ratio, int pasteX, int pasteY) {

    std::vector<DetectionResult> results;

    // 使用动态获取的输出形状参数
    const int num_detections = m_numBoxes;
    const int num_classes = m_numClasses;

    std::vector<QRect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    // sigmoid 函数
    auto sigmoid = [](float x) -> float {
        return 1.0f / (1.0f + std::exp(-x));
    };

    for (int i = 0; i < num_detections; ++i) {
        // 获取边界框坐标 (cx, cy, w, h) - 通道优先格式
        float cx = output[0 * num_detections + i]; // 第0通道：cx
        float cy = output[1 * num_detections + i]; // 第1通道：cy
        float w = output[2 * num_detections + i];  // 第2通道：w
        float h = output[3 * num_detections + i];  // 第3通道：h

        // 获取类别分数并应用 sigmoid
        int max_class_id = 0;
        float max_score = 0.0f;
        for (int c = 0; c < num_classes; ++c) {
            float score = output[(4 + c) * num_detections + i]; // 第4-(4+num_classes-1)通道：类别分数
            float sigmoid_score = sigmoid(score); // 应用 sigmoid 函数
            if (sigmoid_score > max_score) {
                max_score = sigmoid_score;
                max_class_id = c;
            }
        }

        // 直接使用最大类别分数作为置信度
        float confidence = max_score;

        // 判断置信度是否满足阈值
        if (confidence >= m_confidenceThreshold) {
            // 坐标转换：将网络输出的像素坐标转换为原始图像坐标
            int left = static_cast<int>((cx - 0.5f * w - pasteX) / ratio);
            int top = static_cast<int>((cy - 0.5f * h - pasteY) / ratio);
            int width = static_cast<int>(w / ratio);
            int height = static_cast<int>(h / ratio);

            // 边界检查
            left = std::max(0, std::min(left, originalWidth - 1));
            top = std::max(0, std::min(top, originalHeight - 1));
            width = std::max(1, std::min(width, originalWidth - left));
            height = std::max(1, std::min(height, originalHeight - top));

            boxes.push_back(QRect(left, top, width, height));
            confidences.push_back(confidence);
            class_ids.push_back(max_class_id);
        }
    }

    // 经典NMS处理
    std::vector<int> indices;
    if (!boxes.empty()) {
        // 创建索引并按照置信度降序排序
        std::vector<int> order(boxes.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&](int i, int j) { return confidences[i] > confidences[j]; });

        // 创建抑制标记数组
        std::vector<bool> suppressed(boxes.size(), false);

        for (size_t i = 0; i < order.size(); ++i) {
            int idx_i = order[i];

            // 如果当前框已被抑制，跳过
            if (suppressed[idx_i]) continue;

            // 保留当前框
            indices.push_back(idx_i);

            // 检查后续所有框是否需要被抑制
            for (size_t j = i + 1; j < order.size(); ++j) {
                int idx_j = order[j];

                // 如果框已被抑制或类别不同，跳过
                if (suppressed[idx_j] || class_ids[idx_i] != class_ids[idx_j]) continue;

                // 计算IoU
                QRect intersection = boxes[idx_i].intersected(boxes[idx_j]);
                float inter_area = intersection.width() * intersection.height();
                float area_i = boxes[idx_i].width() * boxes[idx_i].height();
                float area_j = boxes[idx_j].width() * boxes[idx_j].height();
                float iou = inter_area / (area_i + area_j - inter_area);

                // 如果IoU超过阈值，抑制该框
                if (iou > m_nmsThreshold) {
                    suppressed[idx_j] = true;
                }
            }
        }
    }

    // 创建最终结果
    for (int idx : indices) {
        DetectionResult result;
        result.classId = class_ids[idx];
        result.confidence = confidences[idx];
        result.boundingBox = boxes[idx];
        result.className = (class_ids[idx] < m_classNames.size()) ?
                          m_classNames[class_ids[idx]] : "Unknown";
        results.push_back(result);
    }

    qDebug() << "Postprocessing completed. Detections:" << results.size();
    return results;
}