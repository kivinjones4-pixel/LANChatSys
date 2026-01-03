# p2pChat-Client.pro
QT       += core gui
QT       += network        # 添加网络模块
QT       += widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
# 你可以在这里添加你的源文件
SOURCES += \
    main.cpp \
    widget.cpp \

HEADERS += \
    widget.h \

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

# 图标（可选）
# RC_ICONS = resources/icon.ico

# 安装路径（可选）
target.path = $$[QT_INSTALL_BINS]
INSTALLS += target
