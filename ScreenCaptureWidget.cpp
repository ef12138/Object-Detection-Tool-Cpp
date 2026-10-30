#include "ScreenCaptureWidget.h"
#include "ui_ScreenCaptureWidget.h"
#include <QDebug>
#include <QScreen>
#include <QApplication>
#include <QToolButton>
#include <QStackedWidget>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QLabel>
#include <QIcon>
#include <QScrollArea>
#include <QFrame>
#include <QFontMetrics>
#include <QLineEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGridLayout>
#include <functional>
#include <QKeyEvent>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QStyleHints>
#include <QGuiApplication>
#include <QAbstractNativeEventFilter>
#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif
#include <QDir>
#include <QFile>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// config.ini 一律锚定在 exe 同目录。
// 原来是相对路径 "config.ini"，取的是「当前工作目录」，而工作目录取决于从哪儿启动：
// 双击 exe、从资源管理器启动、从终端带 WorkingDirectory 启动，三者各不相同，
// 于是配置会被写到不同位置，表现为「改完有时候保存了、有时候没保存」。
// 首次运行时如果 exe 目录还没有配置，就从工作目录那份迁移过来，避免丢已有设置。
QString configPath() {
    static const QString path = []() {
        const QString exeDir =
            QCoreApplication::applicationDirPath() + QStringLiteral("/config.ini");
        if (!QFile::exists(exeDir)) {
            const QString legacy = QDir::current().absoluteFilePath(QStringLiteral("config.ini"));
            if (QFile::exists(legacy)) QFile::copy(legacy, exeDir);
        }
        return exeDir;
    }();
    return path;
}

// 导航图标：全部用 QPainter 现画，不引入任何图标资源，也不增加 DLL 依赖。
QPixmap navIcon(int index, const QColor& color) {
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (index) {
    case 0: // 主页
        p.drawPolyline(QPolygonF{QPointF(3.5, 9.5), QPointF(10, 4), QPointF(16.5, 9.5)});
        p.drawPolyline(QPolygonF{QPointF(5.5, 9), QPointF(5.5, 16.5), QPointF(14.5, 16.5), QPointF(14.5, 9)});
        break;
    case 1: // 推理：检测框 + 两个目标点
        p.drawRect(QRectF(3.0, 5.5, 14.0, 9.0));
        p.setBrush(color);
        p.drawEllipse(QPointF(7.5, 10), 1.3, 1.3);
        p.drawEllipse(QPointF(12.5, 10), 1.3, 1.3);
        break;
    case 2: { // PID：带过冲的阶跃响应，与贝塞尔图标区分开
        QPainterPath path;
        path.moveTo(3, 16);
        path.lineTo(7, 16);
        path.lineTo(7, 8);
        path.lineTo(10.5, 6.5);
        path.lineTo(13.5, 9.5);
        path.lineTo(17, 9);
        p.drawPath(path);
        break;
    }
    case 3: { // 贝塞尔曲线：S 形轨迹 + 两端控制点
        QPainterPath path;
        path.moveTo(3, 16);
        path.cubicTo(7, 16, 7, 4, 17, 4);
        p.drawPath(path);
        p.setBrush(color);
        p.drawEllipse(QPointF(3.5, 15.5), 1.3, 1.3);
        p.drawEllipse(QPointF(16.5, 4.5), 1.3, 1.3);
        break;
    }
    case 4: // 瞄准位置：准心
        p.drawEllipse(QPointF(10, 10), 5.6, 5.6);
        p.drawLine(QPointF(10, 2), QPointF(10, 5.5));
        p.drawLine(QPointF(10, 14.5), QPointF(10, 18));
        p.drawLine(QPointF(2, 10), QPointF(5.5, 10));
        p.drawLine(QPointF(14.5, 10), QPointF(18, 10));
        break;
    case 5: // 快捷键：键盘
        p.drawRect(QRectF(2.8, 6.0, 14.4, 8.6));
        p.drawLine(QPointF(5.6, 9.2), QPointF(6.6, 9.2));
        p.drawLine(QPointF(9.5, 9.2), QPointF(10.5, 9.2));
        p.drawLine(QPointF(13.4, 9.2), QPointF(14.4, 9.2));
        p.drawLine(QPointF(5.6, 12.0), QPointF(14.4, 12.0));
        break;
    default: // 关于：圆圈里一个 i
        p.drawEllipse(QPointF(10, 10), 7.0, 7.0);
        p.drawLine(QPointF(10, 9.3), QPointF(10, 13.8));
        p.setBrush(color);
        p.drawEllipse(QPointF(10, 6.5), 1.0, 1.0);
        break;
    }
    return pm;
}

// ── 主题 ────────────────────────────────────────────────────────────────────
// 深浅两套只差颜色：布局、尺寸、文字全部共用同一份 QSS 模板，避免两份样式表各自漂移。
// 令牌按「名字从长到短」替换，否则 @text 会先把 @text2 啃成 "#0000002"。
struct Palette {
    const char* page;         // 页面底
    const char* card;         // 分组卡片
    const char* railBg;       // 侧栏
    const char* railLine;     // 侧栏右边线
    const char* railHover;    // 导航悬停
    const char* railOn;       // 导航选中
    const char* text;         // 主文字
    const char* value;        // 数值（次要文字）
    const char* hint;         // 底部提示
    const char* line;         // 参数行分隔线
    const char* hover;        // 数值框悬停/聚焦底色
    const char* groove;       // 滑轨
    const char* blue;         // 强调色：滑块填充、次按钮文字、单选选中
    const char* hEdge;        // 手柄描边：浅色下白圆不带轮廓就看不见
    const char* ring;         // 单选未选中圆环
    const char* radioFill;    // 单选选中后圆心填充
    const char* fieldBg;      // 热键输入框
    const char* fieldLine;
    const char* btnBg;        // 次按钮
    const char* btnHover;
    const char* btnPress;
    const char* primary;      // 主按钮
    const char* primaryHover;
    const char* primaryPress;
    const char* sel;          // 文本选中底色
    const char* onAccent;     // 强调色上的文字
    const char* iconOff;      // 导航图标：未选中
    const char* iconOn;       // 导航图标：选中
};

Palette lightPalette() {
    return Palette{
        .page         = "#f2f2f7",
        .card         = "#ffffff",
        .railBg       = "#f9f9fb",     // 不透明：标题栏要的正是这个值
        .railLine     = "rgba(60, 60, 67, 0.22)",
        .railHover    = "rgba(120, 120, 128, 0.10)",
        .railOn       = "rgba(120, 120, 128, 0.18)",
        .text         = "#000000",
        .value        = "rgba(60, 60, 67, 0.72)",   // 白底约 4.9:1，够 AA
        .hint         = "rgba(60, 60, 67, 0.72)",
        .line         = "rgba(60, 60, 67, 0.18)",
        .hover        = "rgba(120, 120, 128, 0.12)",
        .groove       = "#e5e5ea",
        .blue         = "#007aff",
        .hEdge        = "#c9c9ce",
        .ring         = "#c7c7cc",
        .radioFill    = "#ffffff",
        .fieldBg      = "rgba(120, 120, 128, 0.12)",
        .fieldLine    = "transparent",
        .btnBg        = "rgba(120, 120, 128, 0.12)",
        .btnHover     = "rgba(120, 120, 128, 0.20)",
        .btnPress     = "rgba(120, 120, 128, 0.28)",
        .primary      = "#007aff",
        .primaryHover = "#1a8cff",
        .primaryPress = "#0063d1",
        .sel          = "#007aff",
        .onAccent     = "#ffffff",
        .iconOff      = "#8e8e93",
        .iconOn       = "#007aff",
    };
}

Palette darkPalette() {
    // 取色对着 WorkBuddy 的深色界面量出来的：
    //   外壳（标题栏 + 侧栏）#1f1f1f / 内容页 #141414 / 卡片 #2c2c2c。
    // 外壳比内容亮一层，和「侧栏与标题栏同色」是同一套逻辑。
    return Palette{
        .page         = "#141414",
        .card         = "#2c2c2c",
        .railBg       = "#1f1f1f",                    // 不透明：标题栏要的正是这个值
        .railLine     = "rgba(255, 255, 255, 0.09)",
        .railHover    = "rgba(255, 255, 255, 0.08)",
        .railOn       = "#313131",
        .text         = "#ffffff",
        .value        = "rgba(235, 235, 245, 0.72)",
        .hint         = "rgba(235, 235, 245, 0.56)",
        .line         = "rgba(255, 255, 255, 0.10)",
        .hover        = "rgba(255, 255, 255, 0.10)",
        .groove       = "#3f3f3f",
        .blue         = "#0a84ff",
        .hEdge        = "rgba(255, 255, 255, 0.28)",   // 白圆在深底上只需一圈微光
        .ring         = "#5a5a60",
        .radioFill    = "#141414",                    // 单选在页面底上，圆心要跟上 page
        .fieldBg      = "#1a1a1a",
        .fieldLine    = "rgba(255, 255, 255, 0.10)",
        .btnBg        = "rgba(118, 118, 128, 0.24)",
        .btnHover     = "rgba(118, 118, 128, 0.32)",
        .btnPress     = "rgba(118, 118, 128, 0.40)",
        .primary      = "#0a84ff",
        .primaryHover = "#3d9dff",
        .primaryPress = "#0070d8",
        .sel          = "#0a84ff",
        .onAccent     = "#ffffff",
        .iconOff      = "#9a9aa0",
        .iconOn       = "#0a84ff",
    };
}

QString modernStyleSheet(bool dark) {
    const Palette p = dark ? darkPalette() : lightPalette();
    QList<QPair<QString, QString>> tokens = {
        {QStringLiteral("@page"),         QString::fromLatin1(p.page)},
        {QStringLiteral("@card"),         QString::fromLatin1(p.card)},
        {QStringLiteral("@railBg"),       QString::fromLatin1(p.railBg)},
        {QStringLiteral("@railLine"),     QString::fromLatin1(p.railLine)},
        {QStringLiteral("@railHover"),    QString::fromLatin1(p.railHover)},
        {QStringLiteral("@railOn"),       QString::fromLatin1(p.railOn)},
        {QStringLiteral("@text"),         QString::fromLatin1(p.text)},
        {QStringLiteral("@value"),        QString::fromLatin1(p.value)},
        {QStringLiteral("@hint"),         QString::fromLatin1(p.hint)},
        {QStringLiteral("@line"),         QString::fromLatin1(p.line)},
        {QStringLiteral("@hover"),        QString::fromLatin1(p.hover)},
        {QStringLiteral("@groove"),       QString::fromLatin1(p.groove)},
        {QStringLiteral("@blue"),         QString::fromLatin1(p.blue)},
        {QStringLiteral("@hEdge"),        QString::fromLatin1(p.hEdge)},
        {QStringLiteral("@ring"),         QString::fromLatin1(p.ring)},
        {QStringLiteral("@radioFill"),    QString::fromLatin1(p.radioFill)},
        {QStringLiteral("@fieldBg"),      QString::fromLatin1(p.fieldBg)},
        {QStringLiteral("@fieldLine"),    QString::fromLatin1(p.fieldLine)},
        {QStringLiteral("@btnBg"),        QString::fromLatin1(p.btnBg)},
        {QStringLiteral("@btnHover"),     QString::fromLatin1(p.btnHover)},
        {QStringLiteral("@btnPress"),     QString::fromLatin1(p.btnPress)},
        {QStringLiteral("@primary"),      QString::fromLatin1(p.primary)},
        {QStringLiteral("@primaryHover"), QString::fromLatin1(p.primaryHover)},
        {QStringLiteral("@primaryPress"), QString::fromLatin1(p.primaryPress)},
        {QStringLiteral("@sel"),          QString::fromLatin1(p.sel)},
        {QStringLiteral("@onAccent"),     QString::fromLatin1(p.onAccent)},
        {QStringLiteral("@iconOff"),      QString::fromLatin1(p.iconOff)},
        {QStringLiteral("@iconOn"),       QString::fromLatin1(p.iconOn)},
    };
    std::stable_sort(tokens.begin(), tokens.end(), [](const QPair<QString, QString>& a,
                                                      const QPair<QString, QString>& b) {
        return a.first.size() > b.first.size();
    });

    QString qss = QStringLiteral(R"(
        ScreenCaptureWidget { background: @page; }
        QWidget { color: @text; font-size: 15px; }

        /* 侧栏 */
        #navRail { background: @railBg; border-right: 1px solid @railLine; }
        #navRail QToolButton { background: transparent; border: none; border-radius: 9px; }
        #navRail QToolButton:hover { background: @railHover; }
        #navRail QToolButton:checked { background: @railOn; }

        /* 页面标题条（topBar）不着色：真正的"顶栏"是系统标题栏，
           由 DWM 染色（见 setCaptionColors），页面里这条只承载标题文字。 */

        QStackedWidget { background: transparent; }
        QLabel { background: transparent; }
        /* 页面标题：17px 500，靠左 */
        QLabel[role="title"] { color: @text; font-size: 17px; font-weight: 500; }
        QLabel[role="hint"] { color: @hint; font-size: 11px; }
        QLabel[role="paramName"] { color: @text; font-size: 15px; }
        QLabel[role="sectionTitle"] { color: @value; font-size: 11px; font-weight: 600; }
        QLabel[role="value"] { color: @value; font-size: 13px; }

        /* 关于页：卡片与其它页同底色 */
        QWidget#aboutCard { background: @card; border-radius: 10px; }

        /* 关于页滚动区：视口与内容都要透明，否则会盖一层默认面板色 */
        QScrollArea#aboutScroll { background: transparent; border: none; }
        QScrollArea#aboutScroll > QWidget > QWidget { background: transparent; }
        QScrollBar:vertical { width: 8px; background: transparent; border: none; margin: 0; }
        QScrollBar::handle:vertical { background: @ring; border-radius: 4px; min-height: 28px; }
        QScrollBar::handle:vertical:hover { background: @iconOff; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

        /* 分组卡片：去标题、去边框，只剩圆角底 */
        QGroupBox {
            background: @card;
            border: none;
            border-radius: 10px;
            margin-top: 0px;
            padding: 0px;
        }
        QGroupBox::title { color: transparent; padding: 0px; margin: 0px; }

        /* 参数行：40px 高，行间 1px 分隔线（最后一行不画） */
        QWidget#paramRow { background: transparent; border-bottom: 1px solid @line; }
        QWidget#paramRow[last="true"] { border-bottom: none; }

        /* 数值：默认次要色纯文本，悬停才显出可编辑 */
        QLineEdit[role="value"] {
            background: transparent;
            border: none;
            border-radius: 6px;
            padding: 0px 2px;
            color: @value;
            font-size: 15px;
            selection-background-color: @sel;
            selection-color: @onAccent;
        }
        QLineEdit[role="value"]:hover { background: @hover; color: @text; }
        QLineEdit[role="value"]:focus { background: @hover; color: @text; }

        /* 滑块：4px 轨道 + 18px 正圆手柄。
           手柄带 1px 描边（浅色下白圆必须有轮廓），描边计入盒子，
           所以 width 是 14 而不是 18 —— 实测公式：
             外径 = width + 2×border + 2（抗锯齿）
             高度 = groove(4) + 2×|margin|，且受 QSlider 高度裁剪。
           改这几个数必须重新截图量像素。 */
        /* 左右各留 9px（= 手柄半径）：否则值为最小值时手柄被裁在轨道起点外，
           左侧会露出一小截蓝色填充。padding 会把轨道整体内缩，两端正好贴住。 */
        QSlider { min-height: 40px; padding: 0px 9px; background: transparent; }
        QSlider::groove:horizontal { height: 4px; background: @groove; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: @blue; border-radius: 2px; }
        QSlider::add-page:horizontal { background: @groove; border-radius: 2px; }
        QSlider::handle:horizontal {
            width: 14px; margin: -7px 0px; border-radius: 8px;
            background: #ffffff; border: 1px solid @hEdge;
        }

        /* 模型尺寸单选：iOS 尺寸圆点，外径 18 */
        QRadioButton { color: @text; font-size: 15px; spacing: 8px; padding: 2px 0; }
        QRadioButton::indicator {
            width: 18px; height: 18px; border-radius: 9px;
            border: 1px solid @ring; background: @radioFill;
        }
        QRadioButton::indicator:hover { border-color: @blue; }
        QRadioButton::indicator:checked {
            width: 10px; height: 10px; border: 4px solid @blue;
            border-radius: 9px; background: @radioFill;
        }

        /* 热键输入框 */
        QLineEdit {
            background: @fieldBg; border: 1px solid @fieldLine;
            border-radius: 8px; padding: 4px 8px; color: @text; font-size: 15px;
        }
        QLineEdit:focus { border-color: @blue; }

        /* 按钮：主操作实心强调色，次操作 tinted */
        QPushButton {
            border-radius: 12px; padding: 0px 12px; font-size: 15px; font-weight: 500;
            background: @btnBg; border: none; color: @blue;
        }
        QPushButton:hover { background: @btnHover; }
        QPushButton:pressed { background: @btnPress; }
        QPushButton#m_startButton { background: @primary; color: @onAccent; font-size: 17px; }
        QPushButton#m_startButton:hover { background: @primaryHover; }
        QPushButton#m_startButton:pressed { background: @primaryPress; }
    )");

    for (const auto& kv : tokens) qss.replace(kv.first, kv.second);
    return qss;
}

// 系统是否偏好深色。Windows 的「应用模式」直接落在注册表里，
// 比 QStyleHints 稳（Qt 在部分版本上对这个键的同步并不及时）。
bool systemPrefersDark() {
#ifdef Q_OS_WIN
    QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    const QVariant v = personalize.value(QStringLiteral("AppsUseLightTheme"));
    if (v.isValid()) return v.toInt() == 0;
#endif
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

// 标题栏是系统画的，不跟着我们的样式表走。深色下要让 DWM 也画深色，
// 否则一块浅色标题栏扣在深色内容上很突兀。动态加载，不引入 dwmapi 链接依赖。
void applyDarkTitleBar(QWidget* w, bool dark) {
#ifdef Q_OS_WIN
    HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    using SetAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto setAttr = reinterpret_cast<SetAttrFn>(
        reinterpret_cast<void*>(::GetProcAddress(dwm, "DwmSetWindowAttribute")));
    if (setAttr) {
        const BOOL on = dark ? TRUE : FALSE;
        const HWND hwnd = reinterpret_cast<HWND>(w->winId());
        // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE（Win10 2004+），更早的系统用 19
        if (FAILED(setAttr(hwnd, 20, &on, sizeof(on))))
            setAttr(hwnd, 19, &on, sizeof(on));
    }
    ::FreeLibrary(dwm);
#else
    Q_UNUSED(w);
    Q_UNUSED(dark);
#endif
}

// 把侧栏那层半透明底色按 alpha 叠到页面底上，算出等效的不透明色。
// 标题栏只接受不透明 COLORREF，而它必须和侧栏看起来完全一样，所以颜色从 railBg
// 现算，不再单独维护一份色值——两份色值迟早会漂移。
QColor blendOver(const QString& top, const QColor& base) {
    static const QRegularExpression rgba(QStringLiteral(
        "^rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*(?:,\\s*([\\d.]+)\\s*)?\\)$"));
    const auto m = rgba.match(top.trimmed());
    if (!m.hasMatch()) return QColor(top);

    const double a = m.captured(4).isEmpty() ? 1.0 : m.captured(4).toDouble();
    const auto mix = [a](int c, int b) { return int(c * a + b * (1.0 - a) + 0.5); };
    return QColor(mix(m.captured(1).toInt(), base.red()),
                  mix(m.captured(2).toInt(), base.green()),
                  mix(m.captured(3).toInt(), base.blue()));
}

// 系统标题栏染色（Win11 22H2 / build 22000+）。
// 35 = DWMWA_CAPTION_COLOR，36 = DWMWA_TEXT_COLOR。
// 老系统上这两个属性不被识别、调用直接失败，返回值就是能力探测结果；
// 失败时保持 applyDarkTitleBar 设好的系统默认深浅色，不会变成怪颜色。
bool setCaptionColors(QWidget* w, const QColor& bg, const QColor& fg) {
#ifdef Q_OS_WIN
    HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return false;
    using SetAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto setAttr = reinterpret_cast<SetAttrFn>(
        reinterpret_cast<void*>(::GetProcAddress(dwm, "DwmSetWindowAttribute")));

    bool ok = false;
    if (setAttr) {
        const HWND hwnd = reinterpret_cast<HWND>(w->winId());
        const COLORREF capBg = RGB(bg.red(), bg.green(), bg.blue());
        const COLORREF capFg = RGB(fg.red(), fg.green(), fg.blue());
        const bool a = SUCCEEDED(setAttr(hwnd, 35, &capBg, sizeof(capBg)));
        const bool b = SUCCEEDED(setAttr(hwnd, 36, &capFg, sizeof(capFg)));
        ok = a && b;
    }
    ::FreeLibrary(dwm);
    return ok;
#else
    Q_UNUSED(w);
    Q_UNUSED(bg);
    Q_UNUSED(fg);
    return false;
#endif
}

// 系统配色变化时 Windows 会广播 WM_SETTINGCHANGE，lParam 是发生变化的区域名。
// 只认 "ImmersiveColorSet"，其它设置变更不触发重设样式表。
class ColorSchemeWatcher : public QAbstractNativeEventFilter {
public:
    explicit ColorSchemeWatcher(std::function<void()> onSchemeChange)
        : m_onChange(std::move(onSchemeChange)) {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override {
        Q_UNUSED(result);
#ifdef Q_OS_WIN
        if (eventType == "windows_generic_MSG") {
            auto* msg = static_cast<MSG*>(message);
            if (msg->message == WM_SETTINGCHANGE && msg->lParam) {
                const auto* area = reinterpret_cast<const wchar_t*>(msg->lParam);
                if (QString::fromWCharArray(area) == QLatin1String("ImmersiveColorSet"))
                    m_onChange();
            }
        }
#else
        Q_UNUSED(eventType);
        Q_UNUSED(message);
#endif
        return false;
    }

private:
    std::function<void()> m_onChange;
};

// 顶栏：与侧栏同色、横跨整宽，两者拼成 L 形外壳（macOS 分栏那种做法）。
// 必须独立成 widget 而不是给标题 QLabel 上色——页面布局有 16px 左右边距，
// 直接给标签上色会在侧栏与顶栏之间留出一条页面底色的竖缝，L 就断了。
QWidget* makeTopBar(const QString& title, QWidget* parent) {
    auto* bar = new QWidget(parent);
    bar->setObjectName(QStringLiteral("topBar"));
    bar->setFixedHeight(44);
    // 普通 QWidget 默认不绘制样式表背景，必须显式打开
    bar->setAttribute(Qt::WA_StyledBackground, true);

    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(16, 0, 16, 0);
    lay->setSpacing(0);

    auto* lbl = new QLabel(title, bar);
    lbl->setProperty("role", "title");
    lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lay->addWidget(lbl);
    return bar;
}

// 检测页：标题 + 模型尺寸 + 启停 + 叠加层（导航第一项，标题「检测」）
QWidget* buildHomePage(Ui::ScreenCaptureWidget* ui) {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(makeTopBar(QStringLiteral("检测"), page));

    auto* body = new QWidget(page);
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(0);
    outer->addWidget(body, 1);

    // 模型尺寸：4 个圆形单选一行排开。
    // comboBox_model 仍保留（不显示），它是现有加载逻辑的索引来源与信号源，
    // 双向同步保证 config 读入后圆形按钮能跟上。
    auto* modelGroup = new QButtonGroup(page);
    const int currentModel = ui->comboBox_model->currentIndex();
    auto* modelGrid = new QGridLayout;
    modelGrid->setContentsMargins(0, 0, 0, 0);
    modelGrid->setHorizontalSpacing(16);
    modelGrid->setVerticalSpacing(0);
    modelGrid->setColumnStretch(4, 1);   // 四个选项靠左紧凑排列，多余宽度留到右侧
    for (int i = 0; i < ui->comboBox_model->count(); ++i) {
        auto* rb = new QRadioButton(ui->comboBox_model->itemText(i), body);
        rb->setCursor(Qt::PointingHandCursor);
        // 固定宽度：QRadioButton 的宽度随内容走，不固定的话四列会因为 sizeHint
        // 差几像素而整体错位，视觉上间距忽大忽小。
        rb->setFixedWidth(58);
        modelGroup->addButton(rb, i);
        modelGrid->addWidget(rb, 0, i);
        if (i == currentModel) rb->setChecked(true);
    }
    lay->addLayout(modelGrid);
    QObject::connect(modelGroup, &QButtonGroup::idClicked, ui->comboBox_model,
                     [ui = ui](int id) { ui->comboBox_model->setCurrentIndex(id); });
    QObject::connect(ui->comboBox_model, &QComboBox::currentIndexChanged, page,
                     [modelGroup](int idx) {
                         if (auto* b = modelGroup->button(idx)) b->setChecked(true);
                     });
    ui->comboBox_model->hide();

    lay->addSpacing(12);

    ui->m_startButton->setFixedHeight(46);
    ui->m_startButton->setCursor(Qt::PointingHandCursor);
    lay->addWidget(ui->m_startButton);

    ui->m_toggleDrawWindowButton->setFixedHeight(40);
    ui->m_toggleDrawWindowButton->setCursor(Qt::PointingHandCursor);
    lay->addSpacing(8);
    lay->addWidget(ui->m_toggleDrawWindowButton);

    lay->addStretch(1);
    return page;
}

// 开源地址：写死在代码里，config.ini 的 [About] Url 仍可覆盖（留空就用这里）
QString projectUrl() {
    return QStringLiteral("https://github.com/ef12138/Object-Detection-Tool-Cpp");
}

// 链接文字：QSS 的 color 管不到富文本里的 <a>（锚点走的是 QPalette::Link），
// 所以颜色必须写进 HTML，换主题时重新拼一遍。
// 显示文字去掉了 scheme 和 host（只留 owner/repo）：卡片里能用的宽度约 200px，
// 整条 https 地址会顶出卡片右边（实测会被裁掉结尾）。
QString aboutLinkHtml(const QString& url, const QColor& color) {
    QString shown = url;
    shown.remove(QStringLiteral("https://"));
    shown.remove(QStringLiteral("http://"));
    shown.remove(QStringLiteral("github.com/"));
    while (shown.endsWith(QLatin1Char('/'))) shown.chop(1);
    return QStringLiteral("<a href=\"%1\" style=\"color:%2; font-size:12px; "
                          "text-decoration:none;\">%3</a>")
        .arg(url.toHtmlEscaped(), color.name(), shown.toHtmlEscaped());
}

// 关于页：作者 / 开源地址 / 免责声明。
// 这一页没有 .ui 控件可复用，整页现搭。没有套 QScrollArea：
// 内容高度约 220px，最小窗口（300）放得下；而 QScrollArea 会按子控件的
// minimumSizeHint 反向撑宽 widget，卡片右边缘会顶出页面留白（实测多 16px）。
QWidget* buildAboutPage() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(makeTopBar(QStringLiteral("关于"), page));

    // 三张卡片（信息 / 免责 / 功能说明）叠起来远超 300px 窗口，必须能滚。
    // 用 widgetResizable，让正文始终贴着视口宽度走；横向条关掉，只留竖向。
    auto* scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("aboutScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outer->addWidget(scroll, 1);

    auto* body = new QWidget;
    body->setObjectName(QStringLiteral("aboutBody"));
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(0);
    scroll->setWidget(body);

    // 信息卡：沿用参数页的行高（40）与分隔线，视觉上与其它页保持同一套语言
    auto* card = new QWidget(body);
    card->setObjectName(QStringLiteral("aboutCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto* cv = new QVBoxLayout(card);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);

    // 两行标签统一宽度，值左端对齐。度量要用 QSS 生效后的 15px，
    // 否则拿到的是默认字体宽度（同参数页的做法）。
    QFont metricFont = card->font();
    metricFont.setPixelSize(15);
    const QFontMetrics fm(metricFont);
    const int nameW = qMax(fm.horizontalAdvance(QStringLiteral("作者")),
                           fm.horizontalAdvance(QStringLiteral("开源地址")));

    auto addRow = [&](const QString& name, QWidget* value) {
        auto* row = new QWidget(card);
        row->setObjectName(QStringLiteral("paramRow"));
        row->setFixedHeight(40);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(16, 0, 16, 0);
        h->setSpacing(12);

        auto* lb = new QLabel(name, row);
        lb->setProperty("role", "paramName");
        lb->setFixedWidth(nameW);
        lb->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        h->addWidget(lb);
        h->addWidget(value, 1);
        cv->addWidget(row);
        return row;
    };

    // 版本号：由 main.cpp 的 setApplicationVersion 提供，避免两处各写一份
    auto* version = new QLabel(QCoreApplication::applicationVersion(), card);
    addRow(QStringLiteral("版本"), version);

    auto* author = new QLabel(QStringLiteral("EF"), card);
    addRow(QStringLiteral("作者"), author);

    // 开源地址：默认用代码里写死的 projectUrl()，config.ini 的 [About] Url 可覆盖
    const QSettings settings(configPath(), QSettings::IniFormat);
    QString url = settings.value(QStringLiteral("About/Url")).toString().trimmed();
    if (url.isEmpty()) url = projectUrl();
    auto* link = new QLabel(card);
    link->setObjectName(QStringLiteral("aboutLink"));
    link->setOpenExternalLinks(true);
    link->setTextFormat(Qt::RichText);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    // 12px：真实仓库地址三四十个字符，15px 会把这一行顶满甚至挤出卡片
    link->setProperty("role", "value");
    link->setProperty("href", url);
    link->setCursor(Qt::PointingHandCursor);
    link->setToolTip(url);
    link->setText(aboutLinkHtml(url, QColor(QString::fromLatin1(lightPalette().blue))));
    addRow(QStringLiteral("开源地址"), link)->setProperty("last", true);

    lay->addWidget(card);
    lay->addSpacing(12);

    // 免责声明卡：标题用 11px 次要色小字，正文同样 11px，可选中复制
    auto* note = new QWidget(body);
    note->setObjectName(QStringLiteral("aboutCard"));
    note->setAttribute(Qt::WA_StyledBackground, true);
    auto* nv = new QVBoxLayout(note);
    nv->setContentsMargins(16, 10, 16, 12);
    nv->setSpacing(6);

    auto* noteTitle = new QLabel(QStringLiteral("免责声明"), note);
    noteTitle->setProperty("role", "sectionTitle");
    nv->addWidget(noteTitle);

    auto* noteText = new QLabel(
        QStringLiteral("本软件仅供计算机视觉技术的个人学习与研究。请勿用于线上对战、商业代练"
                       "等违反平台用户协议或法律法规的场景；因使用本软件产生的一切后果由使用者"
                       "自行承担，作者不承担任何直接或间接责任。"),
        note);
    noteText->setProperty("role", "hint");
    noteText->setWordWrap(true);
    noteText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    nv->addWidget(noteText);

    lay->addWidget(note);
    lay->addSpacing(12);

    // 功能说明卡：各页参数的用途集中写在这里，参数页本身保持干净
    auto* guide = new QWidget(body);
    guide->setObjectName(QStringLiteral("aboutCard"));
    guide->setAttribute(Qt::WA_StyledBackground, true);
    auto* gv = new QVBoxLayout(guide);
    gv->setContentsMargins(16, 12, 16, 14);
    gv->setSpacing(12);

    auto* guideTitle = new QLabel(QStringLiteral("功能说明"), guide);
    guideTitle->setProperty("role", "sectionTitle");
    gv->addWidget(guideTitle);

    auto addGuide = [&](const QString& name, const QString& text) {
        auto* sec = new QWidget(guide);
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(0, 0, 0, 0);
        sv->setSpacing(3);

        auto* t = new QLabel(name, sec);
        t->setProperty("role", "paramName");   // 15px 主色：段名要能一眼扫到
        sv->addWidget(t);

        auto* d = new QLabel(text, sec);
        d->setProperty("role", "hint");
        d->setWordWrap(true);
        d->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sv->addWidget(d);

        gv->addWidget(sec);
    };

    addGuide(QStringLiteral("检测"),
             QStringLiteral("屏幕中央固定截取 640×640 送进模型：256 最快、640 最准；"
                            "选小尺寸会先把这块画面缩小再推理，远处小目标容易糊，"
                            "但帧率明显更高。开始检测会按「推理」页的频率截屏推理、"
                            "144Hz 重绘叠加层，再点一次停止并清空数据。"));
    addGuide(QStringLiteral("推理"),
             QStringLiteral("置信度是「多确信才算目标」，调高漏检变多、调低误检变多；"
                            "NMS 阈值用来压掉重叠的重复框；"
                            "频率是每轮推理后额外等待的毫秒数，0 表示不等待、每帧都推。"));
    addGuide(QStringLiteral("PID"),
             QStringLiteral("每帧把「目标离屏幕中心多少像素」当误差，三项相加＝这一帧该走的距离，"
                            "再被范围（单帧上限）截断。KP 吃掉当前偏差的几成；"
                            "KI 逐帧累积补残差；KD 看偏差缩小的快慢，它要除以帧间隔，"
                            "值小也会放大几十倍，一般别调大。"));
    addGuide(QStringLiteral("贝塞尔曲线"),
             QStringLiteral("一次位移拆成多段曲线走完，轨迹更像人手。平滑度＝拆几段，越大越细；"
                            "随机系数越大控制点偏得越远，越不规律；"
                            "速度是整段位移的总耗时（毫秒），会平摊到每一步。"));
    addGuide(QStringLiteral("瞄准位置"),
             QStringLiteral("命中点在检测框里的相对位置，0.5 / 0.5 就是正中。"
                            "X 越大越靠右，Y 越大越靠上。"));
    addGuide(QStringLiteral("快捷键"),
             QStringLiteral("两个键作用完全相同，任意一个按住就持续瞄准，松开立即停止，"
                            "可按手型设成右键或侧键。改键：先点一下输入框进入监听，"
                            "再按目标键；点到框外就放弃。"));

    lay->addWidget(guide);
    lay->addStretch(1);
    return page;
}

// 数值框的回车提交过滤器。
// 这个窗口里的 QLineEdit 按回车不会发出 editingFinished（实测只有真正失焦才发），
// 所以直接在事件过滤器里接管 Return/Enter，并吃掉该按键避免继续冒泡到父窗口。
class EnterCommitFilter : public QObject {
public:
    EnterCommitFilter(QObject* parent, std::function<void()> commit)
        : QObject(parent), m_commit(std::move(commit)) {}

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                m_commit();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_commit;
};

void buildModernUi(QWidget* self, Ui::ScreenCaptureWidget* ui) {
    // 1. 拆掉 .ui 里原来的纵向堆叠：只删除布局项，控件本身仍在 self 之下
    auto* oldLayout = qobject_cast<QVBoxLayout*>(self->layout());
    if (oldLayout) {
        while (QLayoutItem* item = oldLayout->takeAt(0)) delete item;
        oldLayout->setContentsMargins(0, 0, 0, 0);
        oldLayout->setSpacing(0);
    }

    auto* shell = new QHBoxLayout;
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    if (oldLayout) oldLayout->addLayout(shell);
    else self->setLayout(shell);

    // 2. 左侧导航轨
    auto* rail = new QWidget(self);
    rail->setObjectName("navRail");
    rail->setFixedWidth(52);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(8, 9, 8, 9);
    railLayout->setSpacing(3);

    auto* stack = new QStackedWidget(self);
    // 导航轨不设 tooltip：鼠标停留时不允许弹文字
    for (int i = 0; i < 6; ++i) {
        auto* btn = new QToolButton(rail);
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        btn->setFixedSize(36, 36);
        btn->setIconSize(QSize(20, 20));
        btn->setCursor(Qt::PointingHandCursor);
        QIcon icon;
        // 浅色主题下未选中的图标要用次要文字色（#8e8e93），否则在白侧栏上看不清
        icon.addPixmap(navIcon(i, QColor(142, 142, 147)), QIcon::Normal, QIcon::Off);
        icon.addPixmap(navIcon(i, QColor(0, 122, 255)), QIcon::Normal, QIcon::On);
        btn->setIcon(icon);
        railLayout->addWidget(btn);
        if (i == 0) btn->setChecked(true);
        QObject::connect(btn, &QToolButton::clicked, stack, [stack, i]() { stack->setCurrentIndex(i); });
    }
    railLayout->addStretch(1);

    // 「关于」钉在导航轨最底部：它不属于参数配置，位置上也要和上面六项分开。
    // 图标序号 6 与 stack 里第 7 页对应（applyTheme 按 findChildren 顺序刷图标，
    // 所以这个按钮必须最后创建）。
    auto* aboutBtn = new QToolButton(rail);
    aboutBtn->setCheckable(true);
    aboutBtn->setAutoExclusive(true);
    aboutBtn->setFixedSize(36, 36);
    aboutBtn->setIconSize(QSize(20, 20));
    aboutBtn->setCursor(Qt::PointingHandCursor);
    {
        QIcon icon;
        icon.addPixmap(navIcon(6, QColor(142, 142, 147)), QIcon::Normal, QIcon::Off);
        icon.addPixmap(navIcon(6, QColor(0, 122, 255)), QIcon::Normal, QIcon::On);
        aboutBtn->setIcon(icon);
    }
    railLayout->addWidget(aboutBtn);
    QObject::connect(aboutBtn, &QToolButton::clicked, stack,
                     [stack]() { stack->setCurrentIndex(6); });

    // 3. 分页：五个参数页直接搬用 .ui 里现成的 QGroupBox，objectName 全部保留，
    //    因此所有 connect 与 ui->xxx 指针不受影响。
    //    页面结构 = 44px 标题条（iOS 导航栏）+ 10px 圆角分组卡片。
    auto makePage = [](const QString& title, QGroupBox* card) {
        auto* page = new QWidget;
        auto* outer = new QVBoxLayout(page);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        outer->addWidget(makeTopBar(title, page));

        auto* body = new QWidget(page);
        auto* lay = new QVBoxLayout(body);
        lay->setContentsMargins(16, 12, 16, 12);
        lay->setSpacing(0);
        outer->addWidget(body, 1);

        card->setTitle(QString());   // 标题已移到页头，卡片内不再重复
        lay->addWidget(card);
        lay->addStretch(1);
        return page;
    };

    // 参数行包装：.ui 里每个参数是一个裸 QHBoxLayout，而 QLayout 无法被样式化，
    // 画不出 iOS 列表那条分隔线。所以把每个 layout 收进一个 40px 高的 QWidget，
    // 由 QWidget#paramRow 承担 border-bottom。layout 内的控件由 setLayout 自动 reparent。
    for (QGroupBox* gb : {ui->groupBox_inference, ui->groupBox_pid, ui->groupBox_bezier,
                          ui->groupBox_target_offset, ui->groupBox_hotkey}) {
        auto* vlay = qobject_cast<QVBoxLayout*>(gb->layout());
        if (!vlay) continue;
        QList<QLayoutItem*> items;
        while (QLayoutItem* it = vlay->takeAt(0)) items.append(it);

        QList<QWidget*> rows;
        for (QLayoutItem* it : items) {
            QLayout* inner = it->layout();
            QWidget* only = it->widget();
            if (!inner && !only) { delete it; continue; }   // 丢弃 spacer

            auto* row = new QWidget(gb);
            row->setObjectName(QStringLiteral("paramRow"));
            row->setFixedHeight(40);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(16, 0, 16, 0);
            h->setSpacing(12);

            // 注意：不能直接把 inner setLayout 给 row——layout 已有 parentWidget 时
            // setLayout 会拒绝安装，控件会全部堆在 (0,0)。只能逐个搬控件。
            if (inner) {
                while (QLayoutItem* sub = inner->takeAt(0)) {
                    if (QWidget* w = sub->widget()) h->addWidget(w);
                    delete sub;
                }
                // 不 delete inner：uic 为具名 layout 生成了成员指针，删掉会让 ui-> 悬空。
            } else {
                h->addWidget(only);
            }
            delete it;
            rows.append(row);
        }
        for (int i = 0; i < rows.size(); ++i) {
            rows[i]->setProperty("last", i == rows.size() - 1);
            vlay->addWidget(rows[i]);
        }
        vlay->setContentsMargins(0, 0, 0, 0);
        vlay->setSpacing(0);
    }

    stack->addWidget(buildHomePage(ui));
    stack->addWidget(makePage(QStringLiteral("推理"), ui->groupBox_inference));
    stack->addWidget(makePage(QStringLiteral("PID"), ui->groupBox_pid));
    stack->addWidget(makePage(QStringLiteral("贝塞尔曲线"), ui->groupBox_bezier));
    stack->addWidget(makePage(QStringLiteral("瞄准位置"), ui->groupBox_target_offset));
    stack->addWidget(makePage(QStringLiteral("快捷键"), ui->groupBox_hotkey));
    stack->addWidget(buildAboutPage());   // 必须排第 7（索引 6），与导航最后一项对齐
    stack->setCurrentIndex(0);

    // 4. 参数行三段对齐：
    //    标签宽度按「页」分别计算，而不是六页共用一个宽度。
    //    页内统一 → 同一页所有轨道左端对齐；页间不统一 → KP / Y 这种短标签页面，
    //    文字到轨道不会空出一大截，滑块也因此在短标签页上拿到更长的轨道。
    //    注意：QSS 尚未 polish，widget 当前字体仍是默认值；按样式表里的 15px 重新度量，
    //    否则标签会被 minimumSizeHint 撑开，把同一行的滑块往右推。
    QFont metricFont = self->font();
    metricFont.setPixelSize(15);
    const QFontMetrics fm(metricFont);
    const int valueW = 46;   // "0.000" 在 15px 下约 42px，40 会把首位裁掉

    for (QGroupBox* gb : {ui->groupBox_inference, ui->groupBox_pid, ui->groupBox_bezier,
                          ui->groupBox_target_offset, ui->groupBox_hotkey}) {
        QList<QLabel*> names;
        int pageNameW = 0;
        for (QLabel* lb : gb->findChildren<QLabel*>()) {
            const QString text = lb->text();
            if (!text.endsWith(QLatin1Char(':'))) continue;   // 只认参数名标签
            // 量的是去掉冒号后的文本，否则宽度会多算一个冒号
            pageNameW = qMax(pageNameW, fm.horizontalAdvance(text.chopped(1)));
            names.append(lb);
        }
        for (QLabel* lb : names) {
            lb->setProperty("role", "paramName");
            lb->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
            lb->setFixedWidth(pageNameW);
            lb->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            if (lb->text() == QStringLiteral("范围:")) {
                lb->setToolTip(QStringLiteral("单帧最大移动像素；滑块满值 1000 等于不限幅"));
            }
            // iOS 列表行不用冒号，标签直接顶格
            lb->setText(lb->text().chopped(1));
        }
    }

    // 数值框可编辑：回车或失焦时解析输入并回写滑块。
    // 回写后由滑块自身的 valueChanged 槽完成「改参数 → 存配置 → 重新格式化显示」，
    // 所以这里不需要（也不应该）再去改 m_xxx 成员，避免两套赋值逻辑打架。
    for (QLineEdit* ed : self->findChildren<QLineEdit*>()) {
        const QString obj = ed->objectName();
        if (!obj.startsWith(QStringLiteral("label_")) || !obj.endsWith(QStringLiteral("_value")))
            continue;
        const QString key = obj.mid(6, obj.size() - 12);   // 去掉 "label_" 与 "_value"
        auto* slider = self->findChild<QSlider*>(QStringLiteral("slider_") + key);
        if (!slider) continue;

        // 滑块刻度 → 显示值的换算系数，必须与 onXxxChanged 里的除法严格对应
        double scale = 1.0;
        if (key == QStringLiteral("kd")) {
            scale = 1000.0;
        } else if (key == QStringLiteral("confidence") || key == QStringLiteral("nms") ||
                   key == QStringLiteral("kp") || key == QStringLiteral("ki") ||
                   key == QStringLiteral("bezier_random") ||
                   key.startsWith(QStringLiteral("target_offset"))) {
            scale = 100.0;
        }
        const int decimals = scale >= 1000.0 ? 3 : (scale >= 100.0 ? 2 : 0);

        ed->setProperty("role", "value");
        ed->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        ed->setFixedWidth(valueW);
        ed->setFixedHeight(28);
        ed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ed->setToolTip(QStringLiteral("可直接输入数值，回车或点到别处生效"));
        // 尽力关掉输入法：这些框只填数字。
        // Windows 的 IME 仍可能在控件上显示拼音预编辑串并吞掉回车，但验证器会拒绝
        // 那些字符，所以最终文本一定会回滚成合法数值，只是中间态会出现一闪。
        ed->setAttribute(Qt::WA_InputMethodEnabled, false);
        // 只允许数字 / 小数点 / 负号，防止手滑输入字母或被粘贴进非法内容
        ed->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("-?\\d{0,6}(?:\\.\\d{0,4})?")), ed));

        // 提交：解析输入 → 回写滑块 → 由滑块原有槽函数完成「改参数、存配置、按规范格式重写文本」。
        // 逻辑幂等：输入非法或数值没变时，一律用滑块当前值重新格式化，防止 "abc" 留在框里让人误以为生效。
        const auto commit = [ed, slider, scale, decimals]() {
            bool ok = false;
            const double typed = ed->text().trimmed().toDouble(&ok);
            if (ok) {
                const int target = qBound(slider->minimum(),
                                          int(std::lround(typed * scale)),
                                          slider->maximum());
                if (target != slider->value()) {
                    slider->setValue(target);
                    return;   // 槽函数已重写过文本
                }
            }
            ed->setText(QString::number(slider->value() / scale, 'f', decimals));
        };
        QObject::connect(ed, &QLineEdit::editingFinished, ed, commit);   // 失焦 / Tab 提交
        ed->installEventFilter(new EnterCommitFilter(ed, commit));        // 回车提交
    }

    shell->addWidget(rail);
    shell->addWidget(stack, 1);

    self->setAttribute(Qt::WA_StyledBackground, true);
    // 导航轨 7 项所需高度 = 上下边距 18 + 7×36 + 6×3 间距 = 288，
    // 再留一点余量给「关于」在底部的分隔感，最小高度取 300。
    self->setMinimumSize(412, 300);
    self->resize(412, 300);
}

} // namespace

// 主题切换的总入口：决定用哪套配色，然后落到样式表 + 导航图标 + 标题栏三处。
// 样式表只能换颜色，图标是现画的 pixmap、标题栏是系统画的，这两处必须单独跟。
void ScreenCaptureWidget::applyTheme(bool dark) {
    m_darkTheme = dark;
    setStyleSheet(modernStyleSheet(dark));

    const Palette p = dark ? darkPalette() : lightPalette();
    auto* rail = findChild<QWidget*>(QStringLiteral("navRail"));
    if (rail) {
        const QColor off(QString::fromLatin1(p.iconOff));
        const QColor on(QString::fromLatin1(p.iconOn));
        const auto buttons = rail->findChildren<QToolButton*>();
        for (int i = 0; i < buttons.size(); ++i) {
            QIcon icon;
            icon.addPixmap(navIcon(i, off), QIcon::Normal, QIcon::Off);
            icon.addPixmap(navIcon(i, on), QIcon::Normal, QIcon::On);
            buttons[i]->setIcon(icon);
        }
    }

    applyDarkTitleBar(this, dark);

    // 关于页的链接色写在富文本里，样式表换不动，得跟着主题重拼一次
    if (auto* link = findChild<QLabel*>(QStringLiteral("aboutLink"))) {
        const QString href = link->property("href").toString();
        if (!href.isEmpty())
            link->setText(aboutLinkHtml(href, QColor(QString::fromLatin1(p.blue))));
    }

    // 系统标题栏（最顶上那条，带最小化/最大化/关闭）与侧栏同色。
    // 颜色现算：侧栏底色叠在页面底上，因此换主题时两边一起变，不会各自为政。
    setCaptionColors(this,
                     blendOver(QString::fromLatin1(p.railBg), QColor(QString::fromLatin1(p.page))),
                     QColor(QString::fromLatin1(p.text)));
}

void ScreenCaptureWidget::refreshTheme() {
    // config.ini 里 theme=light|dark 可强制指定，缺省（或 auto）跟随系统
    const QSettings settings(configPath(), QSettings::IniFormat);
    const QString mode =
        settings.value(QStringLiteral("theme"), QStringLiteral("auto")).toString().trimmed().toLower();

    bool dark;
    if (mode == QLatin1String("dark")) {
        dark = true;
    } else if (mode == QLatin1String("light")) {
        dark = false;
    } else {
        dark = systemPrefersDark();
    }
    applyTheme(dark);
}

ScreenCaptureWidget::ScreenCaptureWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::ScreenCaptureWidget),
      m_dxgi(new dxgi()), m_onnxRuntime(new OnnxRuntime()),
      m_mouseController(new MouseController(this)) {

    ui->setupUi(this);

    // 新版界面：左侧导航轨 + 6 个分页（主页 / 推理 / PID / 贝塞尔曲线 / 瞄准位置 / 快捷键）
    buildModernUi(this, ui);

    // 设置窗口置顶
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

    // 从配置文件读取设置
    loadConfig();

    // 主题跟随系统：先按当前系统配色定一次，之后系统切换监听广播
    refreshTheme();
#ifdef Q_OS_WIN
    m_themeWatcher = new ColorSchemeWatcher([this]() { refreshTheme(); });
    qApp->installNativeEventFilter(m_themeWatcher);
#endif
    // Qt 自己也会在配色变化时发信号，两条路都接上，哪条先到都行
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) { refreshTheme(); });

    // 初始化ONNX Runtime
    initializeModel();

    // 更新屏幕截图信息
    updateScreenCaptureInfo();

    // 创建透明绘制窗口
    m_drawWindow = new DrawWindow();

    // 根据配置设置绘制窗口的可见性
    if (m_drawWindowVisible) {
        m_drawWindow->show();
        ui->m_toggleDrawWindowButton->setText("停止绘制");
    } else {
        m_drawWindow->hide();
        ui->m_toggleDrawWindowButton->setText("绘制");
    }

    // 创建两个独立的定时器
    m_inferenceTimer = new QTimer(this);  // 推理定时器
    m_drawTimer = new QTimer(this);       // 绘制刷新定时器

    // 连接信号和槽
    connect(ui->m_startButton, &QPushButton::clicked, this, &ScreenCaptureWidget::startCapture);
    connect(ui->m_toggleDrawWindowButton, &QPushButton::clicked, this, &ScreenCaptureWidget::onToggleDrawWindowClicked);
    connect(m_inferenceTimer, &QTimer::timeout, this, &ScreenCaptureWidget::captureFrame);
    connect(m_drawTimer, &QTimer::timeout, this, &ScreenCaptureWidget::updateDrawWindowOnly);
    connect(ui->slider_confidence, &QSlider::valueChanged, this, &ScreenCaptureWidget::onConfidenceThresholdChanged);
    connect(ui->slider_nms, &QSlider::valueChanged, this, &ScreenCaptureWidget::onNmsThresholdChanged);
    connect(ui->slider_frequency, &QSlider::valueChanged, this, &ScreenCaptureWidget::onFrequencyChanged);  // 新增连接
    connect(ui->slider_kp, &QSlider::valueChanged, this, &ScreenCaptureWidget::onKpChanged);
    connect(ui->slider_ki, &QSlider::valueChanged, this, &ScreenCaptureWidget::onKiChanged);
    connect(ui->slider_kd, &QSlider::valueChanged, this, &ScreenCaptureWidget::onKdChanged);
    connect(ui->slider_range, &QSlider::valueChanged, this, &ScreenCaptureWidget::onRangeChanged);
    connect(ui->comboBox_model, &QComboBox::currentIndexChanged,
            this, &ScreenCaptureWidget::onModelSelectionChanged);
    connect(ui->hotkeyEdit, &HotkeyEdit::hotkeyChanged, this, &ScreenCaptureWidget::onHotkeyChanged);
    connect(ui->hotkeyEdit2, &HotkeyEdit::hotkeyChanged, this, &ScreenCaptureWidget::onHotkey2Changed);
    connect(ui->slider_bezier_random, &QSlider::valueChanged, this, &ScreenCaptureWidget::onBezierRandomFactorChanged);
    connect(ui->slider_bezier_steps, &QSlider::valueChanged, this, &ScreenCaptureWidget::onBezierStepsChanged);
    connect(ui->slider_bezier_speed, &QSlider::valueChanged, this, &ScreenCaptureWidget::onBezierSpeedChanged);
    connect(ui->slider_target_offset_x, &QSlider::valueChanged, this, &ScreenCaptureWidget::onTargetOffsetXChanged);
    connect(ui->slider_target_offset_y, &QSlider::valueChanged, this, &ScreenCaptureWidget::onTargetOffsetYChanged);

    ui->label_confidence_value->setText(QString::number(m_confidenceThreshold, 'f', 2));
    ui->label_nms_value->setText(QString::number(m_nmsThreshold, 'f', 2));
    ui->label_frequency_value->setText(QString::number(m_frequency));  // 新增
    ui->label_kp_value->setText(QString::number(m_kp, 'f', 2));
    ui->label_ki_value->setText(QString::number(m_ki, 'f', 2));
    ui->label_kd_value->setText(QString::number(m_kd, 'f', 3));
    ui->label_range_value->setText(QString::number(m_pixelRange));
    ui->label_bezier_random_value->setText(QString::number(m_bezierRandomFactor, 'f', 2));
    ui->label_bezier_steps_value->setText(QString::number(m_bezierSteps));
    ui->label_bezier_speed_value->setText(QString::number(m_bezierSpeed));
    ui->label_target_offset_x_value->setText(QString::number(m_targetOffsetX, 'f', 2));
    ui->label_target_offset_y_value->setText(QString::number(m_targetOffsetY, 'f', 2));

    // 初始化完成后立即更新MouseController的参数
    if (m_mouseController) {
        m_mouseController->setPIDParameters(m_kp, m_ki, m_kd);
        m_mouseController->setPixelMoveRange(m_pixelRange);
        m_mouseController->setBezierParameters(m_bezierRandomFactor, m_bezierSteps, m_bezierSpeed);
        m_mouseController->setHotkeys(m_hotkey, m_hotkey2);
        m_mouseController->setTargetOffset(m_targetOffsetX, m_targetOffsetY);
    }

    m_dxgi->initialize();
}

ScreenCaptureWidget::~ScreenCaptureWidget() {
    if (m_themeWatcher) {
        qApp->removeNativeEventFilter(m_themeWatcher);
        delete m_themeWatcher;
        m_themeWatcher = nullptr;
    }
    delete ui;
    if (m_drawWindow) {
        m_drawWindow->close();
        delete m_drawWindow;
    }
}

void ScreenCaptureWidget::updateScreenCaptureInfo() {
    // 使用Qt API获取逻辑分辨率
    QScreen *primaryScreen = QApplication::primaryScreen();
    if (primaryScreen) {
        QRect screenGeometry = primaryScreen->geometry();
        m_screenWidth = screenGeometry.width();
        m_screenHeight = screenGeometry.height();

        // 计算截图在屏幕中心的位置
        m_captureLeft = (m_screenWidth - m_captureWidth) / 2;
        m_captureTop = (m_screenHeight - m_captureHeight) / 2;
    }
    qDebug() << "Screen capture info updated - Screen:" << m_screenWidth << "x" << m_screenHeight
             << "Capture area:" << m_captureLeft << "," << m_captureTop << "to"
             << m_captureLeft + m_captureWidth << "," << m_captureTop + m_captureHeight;
}

QPoint ScreenCaptureWidget::convertToScreenCoordinates(int captureX, int captureY) {
    return QPoint(m_captureLeft + captureX, m_captureTop + captureY);
}

QRect ScreenCaptureWidget::convertToScreenRect(int captureX, int captureY, int width, int height) {
    return QRect(m_captureLeft + captureX, m_captureTop + captureY, width, height);
}

void ScreenCaptureWidget::loadConfig() {
    QSettings settings(configPath(), QSettings::IniFormat);

    // 读取推理设置
    m_modelPath = settings.value("Inference/ModelPath", "./models/lqm640.onnx").toString().toStdString();
    m_confidenceThreshold = settings.value("Inference/ConfidenceThreshold", 0.7).toFloat();
    m_nmsThreshold = settings.value("Inference/NmsThreshold", 0.45).toFloat();
    m_frequency = settings.value("Inference/Frequency", 0).toInt();  // 新增：频率设置

    // 读取截图尺寸（初始值，后面会根据模型输入尺寸更新）
    m_captureWidth = settings.value("Capture/Width", 640).toInt();
    m_captureHeight = settings.value("Capture/Height", 640).toInt();

    // 读取PID参数
    m_kp = settings.value("PID/Kp", 1.0).toDouble();
    m_ki = settings.value("PID/Ki", 0.1).toDouble();
    m_kd = settings.value("PID/Kd", 0.05).toDouble();
    m_pixelRange = settings.value("PID/Range", 15).toInt();

    // 读取贝塞尔曲线参数
    m_bezierRandomFactor = settings.value("Bezier/RandomFactor", 0.3).toDouble();  // 随机系数
    m_bezierSteps = settings.value("Bezier/Steps", 8).toInt();
    m_bezierSpeed = settings.value("Bezier/Speed", 1).toInt();

    // 读取目标点偏移参数
    m_targetOffsetX = settings.value("TargetOffset/X", 0.5).toDouble();
    m_targetOffsetY = settings.value("TargetOffset/Y", 0.5).toDouble();

    // 读取快捷键设置（默认右键和鼠标侧键1）
    m_hotkey = settings.value("Hotkey/Key", VK_RBUTTON).toInt();
    m_hotkey2 = settings.value("Hotkey/Key2", VK_XBUTTON1).toInt();

    // 读取绘制窗口可见状态
    m_drawWindowVisible = settings.value("DrawWindow/Visible", true).toBool();

    // 更新滑动条位置
    ui->slider_confidence->setValue(static_cast<int>(m_confidenceThreshold * 100));
    ui->slider_nms->setValue(static_cast<int>(m_nmsThreshold * 100));
    ui->slider_frequency->setValue(m_frequency);  // 新增
    ui->slider_kp->setValue(static_cast<int>(m_kp * 100));
    ui->slider_ki->setValue(static_cast<int>(m_ki * 100));
    ui->slider_kd->setValue(static_cast<int>(m_kd * 1000));
    ui->slider_range->setValue(m_pixelRange);
    ui->slider_bezier_random->setValue(static_cast<int>(m_bezierRandomFactor * 100));
    ui->slider_bezier_steps->setValue(m_bezierSteps);
    ui->slider_bezier_speed->setValue(m_bezierSpeed);
    ui->slider_target_offset_x->setValue(static_cast<int>(m_targetOffsetX * 100));
    ui->slider_target_offset_y->setValue(static_cast<int>(m_targetOffsetY * 100));

    // 设置快捷键
    ui->hotkeyEdit->setHotkey(m_hotkey);
    ui->hotkeyEdit2->setHotkey(m_hotkey2);

    // 根据当前模型路径设置下拉菜单
    updateModelPathFromComboBox();
}

void ScreenCaptureWidget::updateModelPathFromComboBox() {
    // 从模型路径中提取尺寸并设置下拉菜单
    QString modelPath = QString::fromStdString(m_modelPath);
    if (modelPath.contains("256")) {
        ui->comboBox_model->setCurrentIndex(0);
    } else if (modelPath.contains("320")) {
        ui->comboBox_model->setCurrentIndex(1);
    } else if (modelPath.contains("416")) {
        ui->comboBox_model->setCurrentIndex(2);
    } else if (modelPath.contains("640")) {
        ui->comboBox_model->setCurrentIndex(3);
    }
}

void ScreenCaptureWidget::saveConfig() {
    QSettings settings(configPath(), QSettings::IniFormat);

    // 保存更新后的截图尺寸
    settings.setValue("Capture/Width", m_captureWidth);
    settings.setValue("Capture/Height", m_captureHeight);

    // 保存推理设置，置信度和IOU保留两位小数
    settings.setValue("Inference/ModelPath", QString::fromStdString(m_modelPath));
    settings.setValue("Inference/ConfidenceThreshold", QString::number(m_confidenceThreshold, 'f', 2));
    settings.setValue("Inference/NmsThreshold", QString::number(m_nmsThreshold, 'f', 2));
    settings.setValue("Inference/Frequency", m_frequency);  // 新增：保存频率设置

    // 保存PID参数
    settings.setValue("PID/Kp", m_kp);
    settings.setValue("PID/Ki", m_ki);
    settings.setValue("PID/Kd", m_kd);
    settings.setValue("PID/Range", m_pixelRange);

    // 保存贝塞尔曲线参数
    settings.setValue("Bezier/RandomFactor", m_bezierRandomFactor);  // 随机系数
    settings.setValue("Bezier/Steps", m_bezierSteps);
    settings.setValue("Bezier/Speed", m_bezierSpeed);

    // 保存目标点偏移参数
    settings.setValue("TargetOffset/X", m_targetOffsetX);
    settings.setValue("TargetOffset/Y", m_targetOffsetY);

    // 保存快捷键设置
    settings.setValue("Hotkey/Key", m_hotkey);
    settings.setValue("Hotkey/Key2", m_hotkey2);

    // 保存绘制窗口可见状态
    settings.setValue("DrawWindow/Visible", m_drawWindowVisible);

    // 每次改参数（滑块拖动 / 数值框提交）都会走到这里，QSettings 默认只在析构时
    // 才真正写盘，进程被任务管理器结束或崩溃就白改了。显式 sync 保证即时落盘。
    settings.sync();
}

void ScreenCaptureWidget::initializeModel() {
    // 设置类别名称
    std::vector<std::string> classNames = {"敌人", "友军", "热能", "盾构"};
    m_onnxRuntime->setClassNames(classNames);
    m_onnxRuntime->setConfidenceThreshold(m_confidenceThreshold);
    m_onnxRuntime->setNmsThreshold(m_nmsThreshold);

    // 加载模型
    bool success = m_onnxRuntime->initialize(m_modelPath, true); // 使用GPU
    if (!success) {
        qDebug() << "Failed to initialize ONNX Runtime model";
        return;
    }

    // 获取模型输入尺寸并更新截图设置
    m_captureWidth = m_onnxRuntime->getInputWidth();
    m_captureHeight = m_onnxRuntime->getInputHeight();

    // 设置截图尺寸
    m_dxgi->setCaptureSize(m_captureWidth, m_captureHeight);

    // 更新屏幕截图信息
    updateScreenCaptureInfo();

    // 保存更新后的配置
    saveConfig();

    qDebug() << "Model initialized with input size:" << m_captureWidth << "x" << m_captureHeight;
}

void ScreenCaptureWidget::onConfidenceThresholdChanged(int value) {
    m_confidenceThreshold = value / 100.0f;
    ui->label_confidence_value->setText(QString::number(m_confidenceThreshold, 'f', 2));

    if (m_onnxRuntime) {
        m_onnxRuntime->setConfidenceThreshold(m_confidenceThreshold);
    }

    saveConfig();
    qDebug() << "Confidence threshold changed to:" << m_confidenceThreshold;
}

void ScreenCaptureWidget::onNmsThresholdChanged(int value) {
    m_nmsThreshold = value / 100.0f;
    ui->label_nms_value->setText(QString::number(m_nmsThreshold, 'f', 2));

    if (m_onnxRuntime) {
        m_onnxRuntime->setNmsThreshold(m_nmsThreshold);
    }

    saveConfig();
    qDebug() << "NMS threshold changed to:" << m_nmsThreshold;
}

void ScreenCaptureWidget::onFrequencyChanged(int value) {
    m_frequency = value;
    ui->label_frequency_value->setText(QString::number(m_frequency));

    // 如果定时器正在运行，更新定时器间隔
    if (m_inferenceTimer->isActive()) {
        m_inferenceTimer->setInterval(m_frequency);
    }

    saveConfig();
    qDebug() << "Frequency changed to:" << m_frequency << "ms";
}

void ScreenCaptureWidget::onKpChanged(int value) {
    m_kp = value / 100.0;
    ui->label_kp_value->setText(QString::number(m_kp, 'f', 2));

    if (m_mouseController) {
        m_mouseController->setPIDParameters(m_kp, m_ki, m_kd);
    }

    saveConfig();
    qDebug() << "KP changed to:" << m_kp;
}

void ScreenCaptureWidget::onKiChanged(int value) {
    m_ki = value / 100.0;
    ui->label_ki_value->setText(QString::number(m_ki, 'f', 2));

    if (m_mouseController) {
        m_mouseController->setPIDParameters(m_kp, m_ki, m_kd);
    }

    saveConfig();
    qDebug() << "KI changed to:" << m_ki;
}

void ScreenCaptureWidget::onKdChanged(int value) {
    m_kd = value / 1000.0;
    ui->label_kd_value->setText(QString::number(m_kd, 'f', 3));

    if (m_mouseController) {
        m_mouseController->setPIDParameters(m_kp, m_ki, m_kd);
    }

    saveConfig();
    qDebug() << "KD changed to:" << m_kd;
}

void ScreenCaptureWidget::onRangeChanged(int value) {
    m_pixelRange = value;
    ui->label_range_value->setText(QString::number(m_pixelRange));

    if (m_mouseController) {
        m_mouseController->setPixelMoveRange(m_pixelRange);
    }

    saveConfig();
    qDebug() << "Pixel range changed to:" << m_pixelRange;
}

void ScreenCaptureWidget::onHotkeyChanged(int key) {
    m_hotkey = key;

    if (m_mouseController) {
        m_mouseController->setHotkeys(m_hotkey, m_hotkey2);
    }

    saveConfig();
    qDebug() << "Hotkey 1 changed to:" << key;
}

void ScreenCaptureWidget::onHotkey2Changed(int key) {
    m_hotkey2 = key;

    if (m_mouseController) {
        m_mouseController->setHotkeys(m_hotkey, m_hotkey2);
    }

    saveConfig();
    qDebug() << "Hotkey 2 changed to:" << key;
}

void ScreenCaptureWidget::onBezierRandomFactorChanged(int value) {
    m_bezierRandomFactor = value / 100.0;
    ui->label_bezier_random_value->setText(QString::number(m_bezierRandomFactor, 'f', 2));

    if (m_mouseController) {
        m_mouseController->setBezierParameters(m_bezierRandomFactor, m_bezierSteps, m_bezierSpeed);
    }

    saveConfig();
    qDebug() << "Bezier random factor changed to:" << m_bezierRandomFactor;
}

void ScreenCaptureWidget::onBezierStepsChanged(int value) {
    m_bezierSteps = value;
    ui->label_bezier_steps_value->setText(QString::number(m_bezierSteps));

    if (m_mouseController) {
        m_mouseController->setBezierParameters(m_bezierRandomFactor, m_bezierSteps, m_bezierSpeed);
    }

    saveConfig();
    qDebug() << "Bezier steps changed to:" << m_bezierSteps;
}

void ScreenCaptureWidget::onBezierSpeedChanged(int value) {
    m_bezierSpeed = value;
    ui->label_bezier_speed_value->setText(QString::number(m_bezierSpeed));

    if (m_mouseController) {
        m_mouseController->setBezierParameters(m_bezierRandomFactor, m_bezierSteps, m_bezierSpeed);
    }

    saveConfig();
    qDebug() << "Bezier speed changed to:" << m_bezierSpeed;
}

void ScreenCaptureWidget::onTargetOffsetXChanged(int value) {
    m_targetOffsetX = value / 100.0;
    ui->label_target_offset_x_value->setText(QString::number(m_targetOffsetX, 'f', 2));

    if (m_mouseController) {
        m_mouseController->setTargetOffset(m_targetOffsetX, m_targetOffsetY);
    }

    saveConfig();
    qDebug() << "Target offset X changed to:" << m_targetOffsetX;
}

void ScreenCaptureWidget::onTargetOffsetYChanged(int value) {
    m_targetOffsetY = value / 100.0;
    ui->label_target_offset_y_value->setText(QString::number(m_targetOffsetY, 'f', 2));

    if (m_mouseController) {
        m_mouseController->setTargetOffset(m_targetOffsetX, m_targetOffsetY);
    }

    saveConfig();
    qDebug() << "Target offset Y changed to:" << m_targetOffsetY;
}

void ScreenCaptureWidget::onToggleDrawWindowClicked() {
    if (m_drawWindow) {
        if (m_drawWindowVisible) {
            m_drawWindow->hide();
            m_drawWindowVisible = false;
            ui->m_toggleDrawWindowButton->setText("绘制");
            qDebug() << "Draw window hidden";
        } else {
            m_drawWindow->show();
            m_drawWindowVisible = true;
            ui->m_toggleDrawWindowButton->setText("停止绘制");
            qDebug() << "Draw window shown";
        }

        // 保存配置
        saveConfig();
    }
}

void ScreenCaptureWidget::onModelSelectionChanged(int index) {
    // 根据选择的模型尺寸更新模型路径
    QString size = ui->comboBox_model->itemText(index);
    m_modelPath = QString("./models/lqm%1.onnx").arg(size).toStdString();

    qDebug() << "Model selection changed to:" << m_modelPath.c_str();

    // 停止当前的捕获（如果正在运行）
    if (m_inferenceTimer->isActive()) {
        m_inferenceTimer->stop();
        m_drawTimer->stop();
        ui->m_startButton->setText("开始检测");
        // 停止时清空绘制窗口
        if (m_drawWindow) {
            m_drawWindow->clearDrawInfo();
            m_drawWindow->setPerformanceInfo(0, 0, 0, 0);
            m_drawWindow->updateDraw();
        }
    }

    // 重新初始化模型
    initializeModel();

    // 重新初始化绘制窗口
    if (m_drawWindow) {
        m_drawWindow->reloadConfig();
        qDebug() << "DrawWindow reloaded config after model change";
    }
}

void ScreenCaptureWidget::updateDrawWindowOnly() {
    // 这个函数只负责刷新绘制窗口，不进行推理
    if (m_drawWindow && m_drawWindowVisible) {
        m_drawWindow->updateDraw();
    }
}

// 目标追踪函数 - 使用硬编码参数
void ScreenCaptureWidget::updateTargetTracking(const std::vector<TrackedTarget>& currentTargets) {
    // 更新现有追踪目标的生命周期
    for (auto& trackedTarget : m_trackedTargets) {
        trackedTarget.trackingAge++;
    }

    // 移除过期的追踪目标（使用硬编码的最大追踪帧数）
    m_trackedTargets.erase(
        std::remove_if(m_trackedTargets.begin(), m_trackedTargets.end(),
            [this](const TrackedTarget& target) {
                return target.trackingAge > m_maxTrackingFrames; // 硬编码：10帧
            }),
        m_trackedTargets.end()
    );

    // 关联当前检测到的目标与追踪目标
    for (const auto& currentTarget : currentTargets) {
        bool foundMatch = false;
        double minDistance = std::numeric_limits<double>::max();
        int bestMatchIndex = -1;

        // 寻找最近的追踪目标
        for (size_t i = 0; i < m_trackedTargets.size(); ++i) {
            const auto& trackedTarget = m_trackedTargets[i];

            // 计算两个目标中心点之间的距离
            double currentCenterX = currentTarget.x + currentTarget.width / 2.0;
            double currentCenterY = currentTarget.y + currentTarget.height / 2.0;
            double trackedCenterX = trackedTarget.x + trackedTarget.width / 2.0;
            double trackedCenterY = trackedTarget.y + trackedTarget.height / 2.0;

            double distance = std::sqrt(
                std::pow(currentCenterX - trackedCenterX, 2) +
                std::pow(currentCenterY - trackedCenterY, 2)
            );

            // 使用硬编码的距离阈值：50像素
            if (distance < minDistance && distance < m_trackingDistanceThreshold) {
                minDistance = distance;
                bestMatchIndex = i;
            }
        }

        if (bestMatchIndex != -1) {
            // 更新现有追踪目标
            m_trackedTargets[bestMatchIndex] = currentTarget;
            m_trackedTargets[bestMatchIndex].trackingAge = 0; // 重置生命周期
            m_trackedTargets[bestMatchIndex].trackingId = m_trackedTargets[bestMatchIndex].trackingId; // 保持ID不变
            foundMatch = true;
        }

        if (!foundMatch) {
            // 添加新的追踪目标
            TrackedTarget newTarget = currentTarget;
            newTarget.trackingId = static_cast<int>(m_trackedTargets.size() + 1);
            newTarget.trackingAge = 0;
            m_trackedTargets.push_back(newTarget);
        }
    }
}

// 寻找距离准心最近的敌人目标（优先考虑追踪目标）
TrackedTarget* ScreenCaptureWidget::findClosestEnemyToCrosshair() {
    if (m_trackedTargets.empty()) {
        return nullptr;
    }

    // 获取屏幕中心坐标（准心位置）
    int screenCenterX = m_screenWidth / 2;
    int screenCenterY = m_screenHeight / 2;

    TrackedTarget* closestEnemy = nullptr;
    double minDistance = std::numeric_limits<double>::max();

    for (auto& target : m_trackedTargets) {
        // 只考虑敌人目标（classId == 0）
        if (target.classId == 0) {
            // 计算目标中心点坐标
            double targetCenterX = target.x + target.width / 2.0;
            double targetCenterY = target.y + target.height / 2.0;

            // 计算与准心的距离
            double distance = std::sqrt(
                std::pow(targetCenterX - screenCenterX, 2) +
                std::pow(targetCenterY - screenCenterY, 2)
            );

            if (distance < minDistance) {
                minDistance = distance;
                closestEnemy = &target;
            }
        }
    }

    return closestEnemy;
}

void ScreenCaptureWidget::startCapture() {
    if (m_inferenceTimer->isActive()) {
        // 停止两个定时器
        m_inferenceTimer->stop();
        m_drawTimer->stop();
        ui->m_startButton->setText("开始检测");

        // 停止时清空绘制窗口
        if (m_drawWindow) {
            m_drawWindow->clearDrawInfo();
            m_drawWindow->setPerformanceInfo(0, 0, 0, 0);
            m_drawWindow->updateDraw();
        }
    } else {
        // 启动推理定时器 - 使用配置的频率
        m_inferenceTimer->start(m_frequency);

        // 启动绘制刷新定时器 - 固定 144Hz
        m_drawTimer->start(1000 / 144);

        ui->m_startButton->setText("停止检测");
        m_frameCount = 0;
    }
}

void ScreenCaptureWidget::captureFrame() {
    // 先停止定时器，确保不会重叠执行
    m_inferenceTimer->stop();

    QElapsedTimer timer;
    timer.start();

    // 捕获屏幕
    QImage img = m_dxgi->captureCenterScreen();
    qint64 captureTime = timer.restart();

    if (img.isNull()) {
        // 如果图像为空，重新启动推理定时器
        m_inferenceTimer->start(m_frequency);
        return;
    }

    // 执行推理
    auto detections = m_onnxRuntime->inference(img);
    qint64 inferenceTime = timer.elapsed();

    // 计算时间（秒）
    double captureTimeSeconds = captureTime / 1000.0;
    double inferenceTimeSeconds = inferenceTime / 1000.0;
    double totalTimeSeconds = (captureTime + inferenceTime) / 1000.0;

    // 更新绘制数据（不立即刷新，由绘制定时器负责刷新）
    if (m_drawWindow && m_drawWindowVisible) {
        m_drawWindow->clearDrawInfo();

        // 为不同类别设置不同颜色
        QVector<QColor> colors = {Qt::red, Qt::green, Qt::blue, Qt::yellow};

        // 添加新的绘制信息（使用屏幕坐标）
        for (const auto& detection : detections) {
            DrawInfo info;

            // 转换为屏幕坐标
            QPoint screenPos = convertToScreenCoordinates(detection.boundingBox.x(), detection.boundingBox.y());
            info.x = screenPos.x();
            info.y = screenPos.y();
            info.width = detection.boundingBox.width();
            info.height = detection.boundingBox.height();
            info.text = detection.className;  // 只显示类别名称，不显示置信度百分比
            info.color = colors[detection.classId % colors.size()];
            info.thickness = 3;
            info.confidence = detection.confidence;  // 设置置信度用于血条显示

            m_drawWindow->addDrawInfo(info);
        }

        // 设置性能信息
        m_drawWindow->setPerformanceInfo(captureTimeSeconds, inferenceTimeSeconds, totalTimeSeconds, detections.size());
    }

    // 鼠标控制逻辑
    if (!detections.empty()) {
        std::vector<TrackedTarget> trackTargets;

        for (const auto& detection : detections) {
            // 只选择敌人（根据您的类别，classId=0是"敌人"）
            if (detection.classId == 0) {
                TrackedTarget target;

                // 转换为屏幕坐标
                QPoint screenPos = convertToScreenCoordinates(
                    detection.boundingBox.x(), detection.boundingBox.y());
                target.x = screenPos.x();
                target.y = screenPos.y();
                target.width = detection.boundingBox.width();
                target.height = detection.boundingBox.height();
                target.classId = detection.classId;
                target.confidence = detection.confidence;
                trackTargets.push_back(target);
            }
        }

        // 更新目标追踪（使用硬编码参数）
        updateTargetTracking(trackTargets);

        // 寻找距离准心最近的敌人（优先考虑追踪目标）
        TrackedTarget* closestEnemy = findClosestEnemyToCrosshair();

        if (closestEnemy != nullptr) {
            // 找到最近的敌人
            std::vector<DetectionTarget> singleTarget;
            DetectionTarget mouseTarget;
            mouseTarget.x = closestEnemy->x;
            mouseTarget.y = closestEnemy->y;
            mouseTarget.width = closestEnemy->width;
            mouseTarget.height = closestEnemy->height;
            mouseTarget.classId = closestEnemy->classId;
            mouseTarget.confidence = closestEnemy->confidence;
            singleTarget.push_back(mouseTarget);

            m_mouseController->setTargetPositions(singleTarget);
            m_mouseController->checkAndMoveMouse();

            qDebug() << "x and y:" << closestEnemy->x << "," << closestEnemy->y;
        } else {
            // 没有检测到敌人目标时清空目标列表
            m_mouseController->setTargetPositions({});
            qDebug() << "No enemy targets detected";
        }
    } else {
        // 没有检测目标时清空目标列表
        m_mouseController->setTargetPositions({});
        m_trackedTargets.clear(); // 清空追踪目标
        qDebug() << "No targets detected";
    }

    // 使用配置的频率重新启动推理定时器
    m_inferenceTimer->start(m_frequency);
}