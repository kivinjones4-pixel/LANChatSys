// widget.h
#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTcpSocket>
#include <QListWidgetItem>
#include <QTimer>  // 添加这行

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    // 连接相关
    void onConnectClicked();
    void onDisconnectClicked();

    // 消息相关
    void onSendClicked();
    void onMessageReturnPressed();

    // 网络事件
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

    // 界面事件
    void onUserListItemClicked(QListWidgetItem *item);
    void onClearChatClicked();
    void onSettingsClicked();

    // 工具函数
    void updateConnectionStatus();
    void appendMessage(const QString &sender, const QString &message, bool isSelf = false);
    void appendSystemMessage(const QString &message);
    void updateUserList();
    void startAutoConnect();

private:
    Ui::Widget *ui;
    QTcpSocket *tcpSocket;
    QString username;
    QString currentChatTarget;
    bool isConnected;
    QString serverAddress;
    quint16 serverPort;

    // 初始化函数
    void setupUI();
    void setupConnections();
    void setupDefaultValues();

    // 网络函数
    void connectToServer();
    void disconnectFromServer();
    void sendMessage(const QString &message);
    void sendCommand(const QString &command);

    // 工具函数
    QString getTimestamp();
    QString formatMessage(const QString &rawMessage);
    void saveSettings();
    void loadSettings();
    void showNotification(const QString &title, const QString &message);
};

#endif // WIDGET_H
