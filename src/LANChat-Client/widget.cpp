// widget.cpp
#include "widget.h"
#include "ui_widget.h"
#include <QMessageBox>
#include <QDateTime>
#include <QHostAddress>
#include <QInputDialog>
#include <QSettings>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
// #include <QSoundEffect>
#include <QDesktopServices>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTimer>          // 添加这行
#include <QIcon>           // 添加这行
#include <QTime>           // 添加这行
#include <QRegularExpression>  // 添加这行（用于正则表达式）
#include <QDialog>         // 添加这行
#include <QVBoxLayout>     // 添加这行
#include <QHBoxLayout>     // 添加这行
#include <QCheckBox>       // 添加这行
#include <QPushButton>     // 添加这行
#include <QStyleFactory>   // 添加这行（可选）
#include <QFontDatabase>   // 添加这行（可选）

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , tcpSocket(new QTcpSocket(this))
    , username("游客")
    , currentChatTarget("所有人")
    , isConnected(false)
    , serverAddress("127.0.0.1")
    , serverPort(8888)
{
    ui->setupUi(this);

    // 设置窗口标题和图标
    setWindowTitle("LAN 聊天客户端");
    setWindowIcon(QIcon(":/icons/chat.png"));  // 如果有资源文件的话

    // 初始化
    setupUI();
    setupConnections();
    setupDefaultValues();
    loadSettings();

    // 尝试自动连接
    QTimer::singleShot(100, this, &Widget::startAutoConnect);
}

Widget::~Widget()
{
    saveSettings();
    delete ui;
}
void Widget::updateConnectionStatus()
{
    // 根据当前的连接状态更新UI
    if (isConnected) {
        ui->statusLabel->setText("已连接");
        ui->statusLabel->setStyleSheet("color: green;");
    } else {
        ui->statusLabel->setText("未连接");
        ui->statusLabel->setStyleSheet("color: gray;");
    }
}
void Widget::setupUI()
{
    // 设置控件属性
    ui->chatText->setReadOnly(true);
    ui->chatText->setAcceptRichText(true);

    // 设置输入框提示
    ui->messageInput->setPlaceholderText("输入消息... (按Enter发送)");
    ui->serverAddressInput->setPlaceholderText("127.0.0.1");
    ui->serverPortInput->setPlaceholderText("8888");
    ui->usernameInput->setPlaceholderText("输入用户名");

    // 设置按钮文本
    ui->connectButton->setText("连接");
    ui->sendButton->setText("发送");
    ui->disconnectButton->setText("断开");
    ui->clearButton->setText("清空");
    ui->settingsButton->setText("设置");

    // 设置按钮图标（如果可用）
    // ui->connectButton->setIcon(QIcon(":/icons/connect.png"));
    // ui->sendButton->setIcon(QIcon(":/icons/send.png"));

    // 初始状态
    ui->disconnectButton->setEnabled(false);
    ui->messageInput->setEnabled(false);
    ui->sendButton->setEnabled(false);

    // 设置状态栏文本
    ui->statusLabel->setText("未连接");
    ui->statusLabel->setStyleSheet("color: gray;");
}

void Widget::setupConnections()
{
    // 按钮点击事件
    connect(ui->connectButton, &QPushButton::clicked, this, &Widget::onConnectClicked);
    connect(ui->disconnectButton, &QPushButton::clicked, this, &Widget::onDisconnectClicked);
    connect(ui->sendButton, &QPushButton::clicked, this, &Widget::onSendClicked);
    connect(ui->clearButton, &QPushButton::clicked, this, &Widget::onClearChatClicked);
    connect(ui->settingsButton, &QPushButton::clicked, this, &Widget::onSettingsClicked);

    // 输入框事件
    connect(ui->messageInput, &QLineEdit::returnPressed, this, &Widget::onMessageReturnPressed);

    // 用户列表事件
    connect(ui->userList, &QListWidget::itemClicked, this, &Widget::onUserListItemClicked);

    // 网络信号
    connect(tcpSocket, &QTcpSocket::connected, this, &Widget::onSocketConnected);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &Widget::onSocketDisconnected);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &Widget::onSocketReadyRead);
    connect(tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &Widget::onSocketError);
}

void Widget::setupDefaultValues()
{
    // 设置默认值
    ui->serverAddressInput->setText(serverAddress);
    ui->serverPortInput->setText(QString::number(serverPort));

    // 尝试获取系统用户名作为默认用户名
    QString systemUser = qgetenv("USERNAME");
    if (systemUser.isEmpty()) systemUser = qgetenv("USER");
    if (!systemUser.isEmpty()) {
        ui->usernameInput->setText(systemUser);
        username = systemUser;
    } else {
        ui->usernameInput->setText("用户" + QString::number(rand() % 1000));
    }
}

void Widget::onConnectClicked()
{
    // 获取输入值
    serverAddress = ui->serverAddressInput->text().trimmed();
    QString portText = ui->serverPortInput->text().trimmed();
    username = ui->usernameInput->text().trimmed();

    // 验证输入
    if (serverAddress.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请输入服务器地址");
        return;
    }

    if (portText.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请输入端口号");
        return;
    }

    bool ok;
    serverPort = portText.toUShort(&ok);
    if (!ok || serverPort == 0) {
        QMessageBox::warning(this, "输入错误", "端口号无效");
        return;
    }

    if (username.isEmpty()) {
        username = "匿名用户";
        ui->usernameInput->setText(username);
    }

    // 保存设置
    saveSettings();

    // 连接服务器
    connectToServer();
}

void Widget::connectToServer()
{
    if (isConnected) {
        QMessageBox::information(this, "提示", "已经连接到服务器");
        return;
    }

    // 显示连接状态
    ui->statusLabel->setText("正在连接...");
    ui->statusLabel->setStyleSheet("color: orange;");
    ui->connectButton->setEnabled(false);

    // 连接服务器
    tcpSocket->connectToHost(serverAddress, serverPort);

    // 设置超时
    QTimer::singleShot(5000, this, [this]() {
        if (!isConnected) {
            tcpSocket->abort();
            ui->statusLabel->setText("连接超时");
            ui->statusLabel->setStyleSheet("color: red;");
            ui->connectButton->setEnabled(true);
            QMessageBox::warning(this, "连接超时", "无法连接到服务器，请检查地址和端口");
        }
    });
}

void Widget::onSocketConnected()
{
    isConnected = true;

    // 更新UI状态
    ui->statusLabel->setText("已连接");
    ui->statusLabel->setStyleSheet("color: green;");
    ui->connectButton->setEnabled(false);
    ui->disconnectButton->setEnabled(true);
    ui->messageInput->setEnabled(true);
    ui->sendButton->setEnabled(true);

    // 发送登录消息
    QString loginMsg = QString("LOGIN:%1").arg(username);
    tcpSocket->write(loginMsg.toUtf8());

    // 显示系统消息
    appendSystemMessage(QString("已连接到服务器 %1:%2").arg(serverAddress).arg(serverPort));
}

void Widget::onSocketDisconnected()
{
    isConnected = false;

    // 更新UI状态
    ui->statusLabel->setText("未连接");
    ui->statusLabel->setStyleSheet("color: gray;");
    ui->connectButton->setEnabled(true);
    ui->disconnectButton->setEnabled(false);
    ui->messageInput->setEnabled(false);
    ui->sendButton->setEnabled(false);

    // 清空用户列表
    ui->userList->clear();

    // 显示系统消息
    appendSystemMessage("与服务器的连接已断开");

    // 尝试自动重连（可选）
    if (ui->autoReconnectCheck->isChecked()) {
        QTimer::singleShot(3000, this, &Widget::connectToServer);
    }
}

void Widget::onSocketReadyRead()
{
    while (tcpSocket->canReadLine()) {
        QByteArray data = tcpSocket->readLine();
        QString message = QString::fromUtf8(data).trimmed();

        if (message.isEmpty()) continue;

        // 处理服务器消息
        QString formattedMessage = formatMessage(message);

        // 检查消息类型
        if (message.startsWith("[System]")) {
            // 系统消息
            QString systemMsg = message.mid(9);
            appendSystemMessage(systemMsg);

            // 检查用户加入/离开
            if (systemMsg.contains("加入了聊天室")) {
                updateUserList();
            } else if (systemMsg.contains("离开了聊天室")) {
                updateUserList();
            }

        } else if (message.startsWith("在线用户")) {
            // 用户列表消息
            ui->userList->clear();

            // 解析用户列表
            QString userListStr = message.mid(message.indexOf(":") + 1);
            QStringList users = userListStr.split(",", Qt::SkipEmptyParts);

            for (const QString &user : users) {
                QString trimmedUser = user.trimmed();
                if (!trimmedUser.isEmpty()) {
                    QListWidgetItem *item = new QListWidgetItem(trimmedUser);
                    item->setIcon(QIcon(":/icons/user.png"));
                    ui->userList->addItem(item);
                }
            }

        } else {
            // 普通聊天消息
            QString pattern = "\\[(\\d{1,2}:\\d{2})\\] (\\w+): (.+)";
            QRegularExpression re(pattern);
            QRegularExpressionMatch match = re.match(message);

            if (match.hasMatch()) {
                QString time = match.captured(1);
                QString sender = match.captured(2);
                QString content = match.captured(3);

                appendMessage(sender, content, sender == username);
            } else {
                // 其他格式的消息直接显示
                appendSystemMessage(message);
            }
        }
    }
}

void Widget::onSocketError(QAbstractSocket::SocketError error)
{
    QString errorMsg;
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        errorMsg = "连接被拒绝，服务器可能未启动";
        break;
    case QAbstractSocket::RemoteHostClosedError:
        errorMsg = "服务器关闭了连接";
        break;
    case QAbstractSocket::HostNotFoundError:
        errorMsg = "找不到服务器，请检查地址";
        break;
    case QAbstractSocket::SocketTimeoutError:
        errorMsg = "连接超时";
        break;
    case QAbstractSocket::NetworkError:
        errorMsg = "网络错误，请检查网络连接";
        break;
    default:
        errorMsg = tcpSocket->errorString();
    }

    ui->statusLabel->setText("连接错误");
    ui->statusLabel->setStyleSheet("color: red;");
    ui->connectButton->setEnabled(true);

    QMessageBox::warning(this, "连接错误", errorMsg);
}

void Widget::onDisconnectClicked()
{
    disconnectFromServer();
}

void Widget::disconnectFromServer()
{
    if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
        tcpSocket->disconnectFromHost();
    } else {
        tcpSocket->abort();
    }
}

void Widget::onSendClicked()
{
    QString message = ui->messageInput->text().trimmed();
    if (message.isEmpty()) return;

    sendMessage(message);
}

void Widget::onMessageReturnPressed()
{
    onSendClicked();
}

void Widget::sendMessage(const QString &message)
{
    if (!isConnected) {
        QMessageBox::warning(this, "发送失败", "未连接到服务器");
        return;
    }

    // 检查是否为命令
    if (message.startsWith("/")) {
        sendCommand(message);
        ui->messageInput->clear();
        return;
    }

    // 发送普通消息
    QString formattedMsg = QString("CHAT:%1:%2").arg(username).arg(message);
    tcpSocket->write(formattedMsg.toUtf8());

    // 在本地显示自己发送的消息
    appendMessage(username, message, true);

    // 清空输入框
    ui->messageInput->clear();
}

void Widget::sendCommand(const QString &command)
{
    if (!isConnected) return;

    QString cmd = command.mid(1);  // 去掉开头的"/"
    tcpSocket->write(cmd.toUtf8());

    // 处理本地命令
    if (cmd.startsWith("name ")) {
        QString newName = cmd.mid(5);
        username = newName;
        ui->usernameInput->setText(newName);
        appendSystemMessage(QString("用户名已更改为: %1").arg(newName));
    }
}

void Widget::appendMessage(const QString &sender, const QString &message, bool isSelf)
{
    QString time = getTimestamp();
    QString html;

    if (isSelf) {
        // 自己发送的消息（右对齐，蓝色）
        html = QString("<div style='text-align: right; margin: 5px;'>"
                       "<span style='color: #666; font-size: 10px;'>%1</span><br>"
                       "<span style='background: #007AFF; color: white; padding: 8px 12px; "
                       "border-radius: 15px; display: inline-block; max-width: 70%;'>"
                       "%2</span><br>"
                       "<span style='color: #999; font-size: 10px;'>%3</span>"
                       "</div>")
                   .arg(time, message.toHtmlEscaped(), sender);
    } else {
        // 他人发送的消息（左对齐，灰色）
        html = QString("<div style='text-align: left; margin: 5px;'>"
                       "<span style='color: #666; font-size: 10px;'>%1</span><br>"
                       "<span style='background: #F0F0F0; color: black; padding: 8px 12px; "
                       "border-radius: 15px; display: inline-block; max-width: 70%;'>"
                       "%2</span><br>"
                       "<span style='color: #999; font-size: 10px;'>%3</span>"
                       "</div>")
                   .arg(sender, message.toHtmlEscaped(), time);
    }

    QTextCursor cursor(ui->chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    // 滚动到底部
    QScrollBar *scrollbar = ui->chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void Widget::appendSystemMessage(const QString &message)
{
    QString html = QString("<div style='text-align: center; margin: 10px;'>"
                           "<span style='color: #888; font-size: 11px; "
                           "background: #F8F8F8; padding: 3px 8px; border-radius: 10px;'>"
                           "%1</span>"
                           "</div>")
                       .arg(message.toHtmlEscaped());

    QTextCursor cursor(ui->chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    // 滚动到底部
    QScrollBar *scrollbar = ui->chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

QString Widget::getTimestamp()
{
    return QTime::currentTime().toString("hh:mm");
}

QString Widget::formatMessage(const QString &rawMessage)
{
    // 这里可以添加更多的消息格式化逻辑
    return rawMessage;
}

void Widget::updateUserList()
{
    // 请求用户列表
    if (isConnected) {
        tcpSocket->write("USERS\n");
    }
}

void Widget::onUserListItemClicked(QListWidgetItem *item)
{
    QString selectedUser = item->text();
    if (selectedUser != currentChatTarget) {
        currentChatTarget = selectedUser;
        appendSystemMessage(QString("正在与 %1 聊天").arg(selectedUser));
    }
}

void Widget::onClearChatClicked()
{
    ui->chatText->clear();
    appendSystemMessage("聊天记录已清空");
}

void Widget::onSettingsClicked()
{
    // 创建设置对话框
    QDialog settingsDialog(this);
    settingsDialog.setWindowTitle("设置");
    settingsDialog.setFixedSize(400, 300);

    QVBoxLayout *layout = new QVBoxLayout(&settingsDialog);

    // 自动连接选项
    QCheckBox *autoConnectCheck = new QCheckBox("启动时自动连接", &settingsDialog);
    autoConnectCheck->setChecked(ui->autoReconnectCheck->isChecked());

    // 声音提醒选项
    QCheckBox *soundCheck = new QCheckBox("新消息声音提醒", &settingsDialog);

    // 保存聊天记录选项
    QCheckBox *saveHistoryCheck = new QCheckBox("保存聊天记录", &settingsDialog);

    // 按钮
    QPushButton *saveButton = new QPushButton("保存", &settingsDialog);
    QPushButton *cancelButton = new QPushButton("取消", &settingsDialog);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(cancelButton);

    layout->addWidget(autoConnectCheck);
    layout->addWidget(soundCheck);
    layout->addWidget(saveHistoryCheck);
    layout->addStretch();
    layout->addLayout(buttonLayout);

    connect(saveButton, &QPushButton::clicked, [&]() {
        ui->autoReconnectCheck->setChecked(autoConnectCheck->isChecked());
        settingsDialog.accept();
    });

    connect(cancelButton, &QPushButton::clicked, [&]() {
        settingsDialog.reject();
    });

    settingsDialog.exec();
}

void Widget::saveSettings()
{
    QSettings settings("MyChat", "P2PClient");

    settings.setValue("Server/Address", serverAddress);
    settings.setValue("Server/Port", serverPort);
    settings.setValue("User/Username", username);
    settings.setValue("Window/Geometry", saveGeometry());
    // settings.setValue("Window/State", saveState());
}

void Widget::loadSettings()
{
    QSettings settings("MyChat", "P2PClient");

    serverAddress = settings.value("Server/Address", "127.0.0.1").toString();
    serverPort = settings.value("Server/Port", 8888).toUInt();
    username = settings.value("User/Username", username).toString();

    ui->serverAddressInput->setText(serverAddress);
    ui->serverPortInput->setText(QString::number(serverPort));
    ui->usernameInput->setText(username);

    restoreGeometry(settings.value("Window/Geometry").toByteArray());
    // restoreState(settings.value("Window/State").toByteArray());
}

void Widget::startAutoConnect()
{
    if (ui->autoReconnectCheck->isChecked()) {
        onConnectClicked();
    }
}

void Widget::showNotification(const QString &title, const QString &message)
{
    // 简单的通知实现
    ui->statusLabel->setText(message);
    QTimer::singleShot(3000, this, [this]() {
        if (isConnected) {
            ui->statusLabel->setText("已连接");
            ui->statusLabel->setStyleSheet("color: green;");
        } else {
            ui->statusLabel->setText("未连接");
            ui->statusLabel->setStyleSheet("color: gray;");
        }
    });
}
