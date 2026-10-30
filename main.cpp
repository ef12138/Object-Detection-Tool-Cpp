#include <QApplication>
#include <QIcon>
#include "ScreenCaptureWidget.h"
#include "mouse/win32.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    // 关于页要从这里取版本号；名字与 .ui 里的窗口标题保持一致
    a.setApplicationName(QStringLiteral("Object Detection Tool"));
    a.setApplicationVersion(QStringLiteral("2.2"));

    // 窗口标题栏 / 任务栏 / Alt+Tab 用的图标。
    // 各尺寸分别放进 QIcon，让系统按 DPI 取最合适的一张，而不是拿 128px 硬缩到 16px。
    // exe 文件本身的图标由 app.rc 编进资源段，两者共用同一张 logo.ico。
    QIcon appIcon;
    for (int s : {16, 24, 32, 48, 64, 128})
        appIcon.addFile(QStringLiteral(":/logo/%1.png").arg(s), QSize(s, s));
    a.setWindowIcon(appIcon);

    ScreenCaptureWidget widget;
    widget.show();

    return QApplication::exec();
}
