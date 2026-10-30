cmake_minimum_required(VERSION 3.24)
project(efai2_1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# app.rc 需要 RC 语言（MinGW 下走 windres），project() 之后必须显式启用
if (WIN32)
    enable_language(RC)
endif ()

# 设置Qt路径
set(CMAKE_PREFIX_PATH "D:/All_projects/depend/Qt.6.5.1/6.5.1/mingw_64")

# 设置ONNX Runtime DirectML路径
set(ONNXRUNTIME_ROOT "D:/All_projects/depend/Microsoft.ML.OnnxRuntime.DirectML.1.22.1")
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/build/native/include")
set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_ROOT}/runtimes/win-x64/native")

# 查找OpenCV（如果需要的话）
# set(OPENCV_MINGW_PATH "D:/All_projects/depend/opencv/opencv_clion_build")
# find_package(OpenCV REQUIRED)

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        REQUIRED)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mouse
        ${ONNXRUNTIME_INCLUDE_DIR}
        # ${OPENCV_MINGW_PATH}/include
)

add_executable(efai2_1 WIN32
        main.cpp
        app.rc
        resources.qrc
        ScreenCaptureWidget.cpp
        ScreenCaptureWidget.h
        ScreenCaptureWidget.ui
        dxgi/dxgi.cpp
        dxgi/dxgi.h
        onnxruntime/onnxruntime.cpp
        onnxruntime/onnxruntime.h
        draw/draw.cpp
        draw/draw.h
        mouse/win32.cpp
        mouse/mouse_controller.cpp
        mouse/mouse_controller.h
        mouse/hotkey_edit.cpp
        mouse/hotkey_edit.h
        mouse/pidcontroller.cpp
        mouse/pidcontroller.h
        mouse/bezier_mouse_mover.cpp
        mouse/bezier_mouse_mover.h
)

target_link_libraries(efai2_1
        Qt::Core
        Qt::Gui
        Qt::Widgets
        # ${OpenCV_LIBS}
        "${ONNXRUNTIME_LIB_DIR}/onnxruntime.lib"
)

if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()

    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()

    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
    endif ()

    foreach (QT_LIB Core Gui Widgets)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach (QT_LIB)

    if(WIN32)
        # 复制ONNX Runtime DirectML DLL文件
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${ONNXRUNTIME_LIB_DIR}/onnxruntime.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")

        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${ONNXRUNTIME_LIB_DIR}/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")

        # 对于DirectML版本，可能还需要DirectML.dll
        #find_file(DIRECTML_DLL
                #NAMES DirectML.dll
                #PATHS
                #"${ONNXRUNTIME_LIB_DIR}"
                #"C:/Windows/System32"
                #DOC "DirectML DLL"
                #NO_DEFAULT_PATH)

        if(DIRECTML_DLL)
            message(STATUS "Found DirectML DLL: ${DIRECTML_DLL}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                    "${DIRECTML_DLL}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
        else()
            message(WARNING "DirectML DLL not found, DirectML acceleration may not work")
        endif()

        # 复制其他可能的依赖项
        if(EXISTS "${ONNXRUNTIME_LIB_DIR}/onnxruntime_providers_dml.dll")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                    "${ONNXRUNTIME_LIB_DIR}/onnxruntime_providers_dml.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
        endif()
    endif()
endif ()