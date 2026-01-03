// main.cpp
#include "widget.h"
#include <QApplication>
#include <QStyleFactory>
#include <QFontDatabase>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置应用程序信息
    QApplication::setApplicationName("LAN Chat Client");
    QApplication::setOrganizationName("MyChat");
    QApplication::setApplicationVersion("1.0.0");

    // 设置全局样式
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // 设置全局字体（确保中文字体显示正常）
    QFont font("Microsoft YaHei", 9);  // Windows系统字体
    // QFont font("WenQuanYi Micro Hei", 10);  // Linux系统字体
    a.setFont(font);

    // 启用高DPI支持
    a.setAttribute(Qt::AA_EnableHighDpiScaling);
    a.setAttribute(Qt::AA_UseHighDpiPixmaps);

    // 创建并显示主窗口
    Widget w;
    w.show();

    return a.exec();
}
