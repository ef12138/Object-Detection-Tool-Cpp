#include "hotkey_edit.h"
#include <QDebug>
#include <QContextMenuEvent>
#include <QInputMethodEvent>

HotkeyEdit::HotkeyEdit(QWidget* parent)
    : QLineEdit(parent), m_hotkey(VK_RBUTTON), m_listening(false)
{
    setReadOnly(true);
    setPlaceholderText("点击设置快捷键");

    // 这个控件的文字只是「当前热键」的展示，没有可编辑内容，也不该出现选择高亮。
    // 之前点击会随机全选：mousePressEvent 被吞掉了，但基类的 mouseReleaseEvent 仍然
    // 走「单击选中整行文字」那套逻辑，于是文字整块变蓝。这里直接掐掉选择能力，
    // 任何来源（单击、双击、拖选、Ctrl+A）产生的选区都立即清掉。
    setDragEnabled(false);
    setAcceptDrops(false);
    connect(this, &QLineEdit::selectionChanged, this, [this]() {
        if (hasSelectedText()) deselect();
    });

    updateDisplayText();
}

HotkeyEdit::~HotkeyEdit()
{
}

void HotkeyEdit::setHotkey(int key)
{
    if (m_hotkey != key) {
        m_hotkey = key;
        updateDisplayText();
        emit hotkeyChanged(key);
    }
}

void HotkeyEdit::startListening()
{
    if (m_listening) return;

    m_listening = true;
    setStyleSheet("QLineEdit { background-color: #FFFACD; color: #000000; }"); // 淡黄色背景表示正在监听
    setText("正在监听，按下键盘或鼠标按键...");

    // 确保获得焦点
    setFocus();
    grabKeyboard(); // 捕获所有键盘输入
}

void HotkeyEdit::stopListening()
{
    if (!m_listening) return;

    m_listening = false;
    setStyleSheet("");
    updateDisplayText();
    releaseKeyboard(); // 释放键盘捕获
}

void HotkeyEdit::inputMethodEvent(QInputMethodEvent* event)
{
    if (m_listening) {
        // 输入法（尤其是中文）会把按键吞成文本提交，keyPressEvent 一次都不来。
        // 这里把提交的字符还原成热键：ASCII 字母/数字的 Unicode 码点与虚拟键值相同，
        // 所以 'K' -> 0x4B(VK_K)、'3' -> 0x33(VK_3)，与 keyToDisplayText 的约定一致。
        // 有些输入法先给组合串（preedit），提交串要等选词才来；两处都取一次。
        QString commit = event->commitString();
        if (commit.isEmpty()) commit = event->preeditString();
        if (!commit.isEmpty()) {
            const QChar ch = commit.at(0).toUpper();
            if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
                (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) {
                setHotkey(ch.unicode());
                stopListening();
            }
        }
        event->accept();
        return;
    }
    QLineEdit::inputMethodEvent(event);
}

void HotkeyEdit::keyPressEvent(QKeyEvent* event)
{
    if (m_listening) {
        int key = event->nativeVirtualKey();

        // 调试信息
        qDebug() << "Key pressed - Native:" << key << "Qt key:" << event->key() << "Text:" << event->text();

        // 过滤掉修饰键
        if (key != VK_SHIFT && key != VK_CONTROL && key != VK_MENU &&
            key != VK_LSHIFT && key != VK_RSHIFT &&
            key != VK_LCONTROL && key != VK_RCONTROL &&
            key != VK_LMENU && key != VK_RMENU) {

            // 对于字母键，使用大写形式
            if (event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
                key = event->key(); // 使用Qt的键值，它已经是ASCII大写字母
            }

            // 对于数字键，也使用Qt键值
            if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) {
                key = event->key();
            }

            qDebug() << "Setting hotkey to:" << key;
            setHotkey(key);
            stopListening();
        }

        event->accept();
        return;
    }

    QLineEdit::keyPressEvent(event);
}

void HotkeyEdit::keyReleaseEvent(QKeyEvent* event)
{
    if (m_listening) {
        event->accept();
        return;
    }

    QLineEdit::keyReleaseEvent(event);
}

void HotkeyEdit::mousePressEvent(QMouseEvent* event)
{
    // 当点击编辑框时，立即进入监听模式
    if (!m_listening) {
        startListening();
        event->accept();
        return;
    }

    // 如果已经在监听模式，处理鼠标按键
    if (m_listening) {
        Qt::MouseButton button = event->button();
        int key = 0;

        switch (button) {
        case Qt::LeftButton: key = VK_LBUTTON; break;
        case Qt::RightButton: key = VK_RBUTTON; break;
        case Qt::MiddleButton: key = VK_MBUTTON; break;
        case Qt::XButton1: key = VK_XBUTTON1; break;
        case Qt::XButton2: key = VK_XBUTTON2; break;
        default: break;
        }

        if (key != 0) {
            qDebug() << "Setting mouse hotkey to:" << key;
            setHotkey(key);
            stopListening();
        }

        event->accept();
        return;
    }

    QLineEdit::mousePressEvent(event);
}

void HotkeyEdit::mouseReleaseEvent(QMouseEvent* event)
{
    // 不交给基类：QLineEdit 的 release 会按单击/双击做选区，
    // 这是「点一下左键文字就全变蓝」的直接来源。
    event->accept();
}

void HotkeyEdit::mouseDoubleClickEvent(QMouseEvent* event)
{
    // 双击默认按词选中，同样不要
    event->accept();
}

void HotkeyEdit::focusInEvent(QFocusEvent* event)
{
    // 进入监听只由鼠标点击触发（见 mousePressEvent），这里不再自动开启：
    // 切页面 / 切窗口时 QStackedWidget 会把焦点恢复到这个控件，若在 focusIn 里开监听，
    // 就会出现「设好热键、点别的页再回来，框里又变成正在监听」。
    // Tab 到达也不开监听——这个交互只用鼠标点。
    QLineEdit::focusInEvent(event);
}

void HotkeyEdit::focusOutEvent(QFocusEvent* event)
{
    if (m_listening) {
        stopListening();
    }
    QLineEdit::focusOutEvent(event);
}

void HotkeyEdit::contextMenuEvent(QContextMenuEvent* event)
{
    // 禁用右键菜单
    event->accept();
}

void HotkeyEdit::updateDisplayText()
{
    setText(keyToDisplayText(m_hotkey));
}

QString HotkeyEdit::keyToDisplayText(int key)
{
    switch (key) {
    case VK_LBUTTON: return "鼠标左键";
    case VK_RBUTTON: return "鼠标右键";
    case VK_MBUTTON: return "鼠标中键";
    case VK_XBUTTON1: return "鼠标侧键1";
    case VK_XBUTTON2: return "鼠标侧键2";
    case VK_BACK: return "Backspace";
    case VK_TAB: return "Tab";
    case VK_RETURN: return "Enter";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_SPACE: return "空格";
    case VK_ESCAPE: return "Esc";
    case VK_PRIOR: return "Page Up";
    case VK_NEXT: return "Page Down";
    case VK_END: return "End";
    case VK_HOME: return "Home";
    case VK_LEFT: return "左箭头";
    case VK_UP: return "上箭头";
    case VK_RIGHT: return "右箭头";
    case VK_DOWN: return "下箭头";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_F1: return "F1";
    case VK_F2: return "F2";
    case VK_F3: return "F3";
    case VK_F4: return "F4";
    case VK_F5: return "F5";
    case VK_F6: return "F6";
    case VK_F7: return "F7";
    case VK_F8: return "F8";
    case VK_F9: return "F9";
    case VK_F10: return "F10";
    case VK_F11: return "F11";
    case VK_F12: return "F12";
    }
    
    // 对于字母和数字键，直接显示字符
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
        return QString(QChar(key));
    }
    
    return QString("键 0x%1").arg(key, 0, 16);
}