#ifndef HOTKEY_EDIT_H
#define HOTKEY_EDIT_H

#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <windows.h>

class HotkeyEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit HotkeyEdit(QWidget* parent = nullptr);
    ~HotkeyEdit();

    // 获取当前设置的快捷键
    int getHotkey() const { return m_hotkey; }

    // 设置快捷键
    void setHotkey(int key);

    signals:
        void hotkeyChanged(int key);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void updateDisplayText();
    QString keyToDisplayText(int key);
    void startListening();
    void stopListening();

    int m_hotkey;
    bool m_listening;
};

#endif // HOTKEY_EDIT_H