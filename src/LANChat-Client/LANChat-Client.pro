# LANChat-Client.pro
QT       += core gui
QT       += network        # 网络模块
QT       += widgets
QT       += core5compat
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
# 添加资源文件（用于图标）
# RESOURCES += resources.qrc

SOURCES += \
    main.cpp \
    widget.cpp

HEADERS += \
    widget.h

FORMS += \
    widget.ui

# 默认规则
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/.obj
MOC_DIR = $$PWD/build/.moc
RCC_DIR = $$PWD/build/.rcc
UI_DIR = $$PWD/build/.ui

# 调试信息
CONFIG += debug
CONFIG -= app_bundle

# 编译选项
QMAKE_CXXFLAGS += -std=c++17 -Wall -Wextra

# 目标
TARGET = LANChat-Client
TEMPLATE = app

# Windows特定设置
win32 {
    # RC_ICONS = resources/icon.ico
    LIBS += -lws2_32
}

# Linux特定设置
unix:!macx {
    QMAKE_LFLAGS += -no-pie
}

# 不要添加这个，因为它会干扰Qt的包含路径
# INCLUDEPATH += $$PWD
