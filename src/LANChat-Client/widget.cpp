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
#include <QDesktopServices>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTimer>
#include <QIcon>
#include <QTime>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QRegularExpression>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QPushButton>
#include <QStyleFactory>
#include <QFontDatabase>
#include <QBuffer>
#include <QImageReader>
#include <QMimeDatabase>
#include <cmath>

// 在构造函数中安装事件过滤器
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , tcpSocket(new QTcpSocket(this))
    , username("游客")
    , currentChatTarget("所有人")
    , isConnected(false)
    , serverAddress("127.0.0.1")
    , serverPort(8888)
    , isProcessingDownload(false)
    , currentUpload(nullptr)
    , totalFileSize(0)
    , currentPrivateTarget("")
    , isHandlingDownload(false)

{
    ui->setupUi(this);

    setWindowTitle("LAN 聊天客户端 - 文件传输支持");

    setupUI();
    setupConnections();
    setupTextBrowserConnections();
    setupDefaultValues();
    loadSettings();

    ui->chatText->installEventFilter(this);
    // 设置用户列表的上下文菜单
    ui->userList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->userList, &QListWidget::customContextMenuRequested,
            this, &Widget::onUserListContextMenu);

    // 定期清理 QTextBrowser 状态
    QTimer *cleanTimer = new QTimer(this);
    connect(cleanTimer, &QTimer::timeout, this, &Widget::cleanTextBrowser);
    cleanTimer->start(5000);  // 每5秒清理一次

    QTimer::singleShot(100, this, &Widget::startAutoConnect);
}
// 用户列表右键菜单
void Widget::onUserListContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = ui->userList->itemAt(pos);
    if (!item) return;

    QString selectedUser = item->text();

    // 移除各种标记
    selectedUser = selectedUser.replace(" (我)", "");
    selectedUser = selectedUser.replace(" [离线]", "");
    selectedUser = selectedUser.replace(" 💬", "");

    // 如果是自己或"所有人"，不显示私聊菜单
    if (selectedUser == username || selectedUser == "所有人") return;

    // 创建菜单
    QMenu menu(this);

    QAction *privateChatAction = new QAction("发起私聊", this);
    QAction *profileAction = new QAction("查看资料", this);
    QAction *closePrivateChatAction = nullptr;

    // 如果已经有私聊会话，添加关闭私聊选项
    if (privateChats.contains(selectedUser)) {
        closePrivateChatAction = new QAction("关闭私聊", this);
        menu.addAction(closePrivateChatAction);
        menu.addSeparator();
    }

    menu.addAction(privateChatAction);
    menu.addAction(profileAction);

    // 显示菜单并获取选择的动作
    QAction *selectedAction = menu.exec(ui->userList->viewport()->mapToGlobal(pos));

    if (selectedAction == privateChatAction) {
        // 发起私聊
        startPrivateChat(selectedUser);
    }
    else if (selectedAction == profileAction) {
        // 查看资料
        showUserProfile(selectedUser);
    }
    else if (closePrivateChatAction && selectedAction == closePrivateChatAction) {
        // 关闭私聊
        closePrivateChat(selectedUser);
    }

    // 清理内存
    delete privateChatAction;
    delete profileAction;
    if (closePrivateChatAction) delete closePrivateChatAction;
}

// 用户资料查看函数（需要实现）
void Widget::showUserProfile(const QString &username)
{
    QString info = QString("用户: %1\n").arg(username);

    // 如果用户在线，显示在线信息
    bool isOnline = false;
    for (int i = 0; i < ui->userList->count(); ++i) {
        QListWidgetItem *item = ui->userList->item(i);
        QString itemText = item->text();
        QString cleanName = itemText;
        cleanName = cleanName.replace(" (我)", "");
        cleanName = cleanName.replace(" [离线]", "");
        cleanName = cleanName.replace(" 💬", "");

        if (cleanName == username) {
            isOnline = !itemText.contains("[离线]");
            break;
        }
    }

    if (isOnline) {
        info += "状态: 在线\n";
    } else {
        info += "状态: 离线\n";
    }

    // 如果有私聊历史，显示消息数量
    if (privateChats.contains(username)) {
        int messageCount = privateChats[username].messages.size();
        info += QString("私聊消息数: %1\n").arg(messageCount);
    }

    QMessageBox::information(this, "用户资料", info);
}

// 关闭私聊函数
void Widget::closePrivateChat(const QString &targetUser)
{
    if (privateChats.contains(targetUser)) {
        privateChats.remove(targetUser);

        // 更新用户列表显示，移除私聊标记
        for (int i = 0; i < ui->userList->count(); ++i) {
            QListWidgetItem *item = ui->userList->item(i);
            QString itemText = item->text();

            QString cleanName = itemText;
            cleanName = cleanName.replace(" (我)", "");
            cleanName = cleanName.replace(" [离线]", "");
            cleanName = cleanName.replace(" 💬", "");

            if (cleanName == targetUser) {
                // 移除私聊标记
                QString newText = cleanName;
                if (cleanName == username) {
                    newText += " (我)";
                }
                if (itemText.contains("[离线]")) {
                    newText += " [离线]";
                }
                item->setText(newText);

                // 恢复颜色
                if (cleanName == username) {
                    item->setForeground(Qt::green);
                } else if (itemText.contains("[离线]")) {
                    item->setForeground(Qt::gray);
                } else {
                    item->setForeground(Qt::black);
                }
                break;
            }
        }

        // 如果当前正在和该用户私聊，切换回所有人聊天
        if (currentChatTarget == targetUser) {
            currentChatTarget = "所有人";
            ui->userList->setCurrentRow(0);
            appendSystemMessage("已关闭私聊，现在与所有人聊天");
        }
    }
}
// 开始私聊
void Widget::startPrivateChat(const QString &targetUser)
{
    if (targetUser.isEmpty() || targetUser == username) return;

    // 检查是否已经有私聊会话
    if (!privateChats.contains(targetUser)) {
        PrivateChat chat;
        chat.targetUser = targetUser;
        chat.isActive = true;
        privateChats[targetUser] = chat;
    }

    currentPrivateTarget = targetUser;
    currentChatTarget = targetUser;

    // 更新用户列表显示
    updatePrivateChatIndicator();

    // 显示系统消息
    appendSystemMessage(QString("已开始与 %1 的私聊").arg(targetUser));
}
// 发送私聊消息
void Widget::sendPrivateMessage(const QString &message, const QString &targetUser)
{
    if (!isConnected || targetUser.isEmpty() || message.isEmpty()) return;

    QJsonObject msgJson;
    msgJson["type"] = "private";
    msgJson["sender"] = username;
    msgJson["target"] = targetUser;
    msgJson["content"] = message;
    msgJson["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    QJsonDocument doc(msgJson);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    tcpSocket->write(jsonString.toUtf8() + "\n");

    // 在本地显示私聊消息
    // QString displayMsg = QString("[私聊] %1").arg(message);
    // appendMessage(username, displayMsg, true);

    // 保存到私聊历史
    if (privateChats.contains(targetUser)) {
        privateChats[targetUser].messages.append(QString("[%1] 我: %2")
                                                     .arg(QTime::currentTime().toString("hh:mm"))
                                                     .arg(message));
    }
}
// 事件过滤器实现
bool Widget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->chatText && event->type() == QEvent::KeyPress) {
        // 如果正在处理下载，忽略某些按键事件
        if (isHandlingDownload) {
            return true; // 阻止事件传播
        }
    }
    return QWidget::eventFilter(obj, event);
}

void Widget::setupTextBrowserConnections()
{
    // 只连接一次，避免重复处理
    disconnect(ui->chatText, &QTextBrowser::anchorClicked, this, nullptr);
    connect(ui->chatText, &QTextBrowser::anchorClicked, this, &Widget::handleDownloadRequest);
}
void Widget::cleanTextBrowser()
{
    // 清理 QTextBrowser 的缓存和状态
    ui->chatText->clearFocus();
    ui->chatText->document()->clearUndoRedoStacks();
    ui->chatText->document()->setModified(false);

    // 重新设置文本交互标志
    ui->chatText->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
}

void Widget::handleDownloadRequest(const QUrl &url)
{
    // 立即阻止所有后续处理
    // 设置标志位防止重复处理
    if (isProcessingDownload) {
        return;  // 如果正在处理，直接返回
    }

    isProcessingDownload = true;

    isProcessingDownload = true;

    // 延迟处理，确保事件循环完成
    QTimer::singleShot(100, this, [this, url]() {
        if (url.scheme() == "file") {
            QString filePath = url.toLocalFile();
            QFileInfo fileInfo(filePath);

            if (fileInfo.exists()) {
                // 创建一个模态对话框，防止事件循环问题
                QDialog dialog(this);
                dialog.setWindowTitle("文件操作");

                QVBoxLayout *layout = new QVBoxLayout(&dialog);
                QLabel *label = new QLabel(QString("文件: %1").arg(fileInfo.fileName()), &dialog);
                layout->addWidget(label);

                QLabel *infoLabel = new QLabel("你想要打开文件还是打开所在文件夹？", &dialog);
                layout->addWidget(infoLabel);

                QHBoxLayout *buttonLayout = new QHBoxLayout();
                QPushButton *openButton = new QPushButton("打开文件", &dialog);
                QPushButton *openFolderButton = new QPushButton("打开文件夹", &dialog);
                QPushButton *cancelButton = new QPushButton("取消", &dialog);

                buttonLayout->addWidget(openButton);
                buttonLayout->addWidget(openFolderButton);
                buttonLayout->addWidget(cancelButton);
                layout->addLayout(buttonLayout);

                // 连接按钮信号
                connect(openButton, &QPushButton::clicked, &dialog, [&dialog, filePath]() {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
                    dialog.accept();
                });

                connect(openFolderButton, &QPushButton::clicked, &dialog, [&dialog, filePath]() {
                    QString folderPath = QFileInfo(filePath).absolutePath();
                    QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
                    dialog.accept();
                });

                connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

                // 显示对话框
                dialog.exec();
            } else {
                QMessageBox::warning(this, "文件不存在", "文件不存在或已被移动: " + filePath);
            }
        } else {
            // 对于其他URL，直接打开
            QDesktopServices::openUrl(url);
        }

        // 重置标志位
        isProcessingDownload = false;
    });
}

Widget::~Widget()
{
    // 清理上传队列
    for (auto transfer : pendingUploads) {
        if (transfer->file) {
            transfer->file->close();
            delete transfer->file;
        }
        delete transfer;
    }
    pendingUploads.clear();

    if (currentUpload) {
        if (currentUpload->file) {
            currentUpload->file->close();
            delete currentUpload->file;
        }
        delete currentUpload;
    }
    saveSettings();
    delete ui;
}
void Widget::setupConnections()
{
    // 按钮点击事件
    connect(ui->connectButton, &QPushButton::clicked, this, &Widget::onConnectClicked);
    connect(ui->disconnectButton, &QPushButton::clicked, this, &Widget::onDisconnectClicked);
    connect(ui->sendButton, &QPushButton::clicked, this, &Widget::onSendClicked);
    connect(ui->uploadButton, &QPushButton::clicked, this, &Widget::onUploadClicked);
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
void Widget::setupUI()
{
    // 设置控件属性
    ui->chatText->setReadOnly(true);
    ui->chatText->setAcceptRichText(true);
    // 关键：禁用链接自动打开，完全由我们自己处理
    ui->chatText->setOpenLinks(false);  // 禁用自动打开链接
    ui->chatText->setOpenExternalLinks(false);  // 禁用外部链接

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
    ui->uploadButton->setText("上传文件");

    // 设置进度条
    ui->uploadProgressBar->setRange(0, 100);
    ui->uploadProgressBar->setValue(0);
    ui->uploadProgressBar->setVisible(false);
    ui->uploadStatusLabel->setText("就绪");

    // 初始化用户列表
    ui->userList->clear();
    QListWidgetItem *defaultItem = new QListWidgetItem("所有人");
    ui->userList->addItem(defaultItem);
    ui->userList->setCurrentItem(defaultItem);
    ui->userList->setStyleSheet(
        "QListWidget::item:selected {"
        "    background-color: #4CAF50;"
        "}"
        "QListWidget::item:hover {"
        "    background-color: #A5D6A7;"
        "}"
        );
    // 设置用户列表样式
    ui->userList->setAlternatingRowColors(true);
    QFont font = ui->userList->font();
    font.setPointSize(10);
    ui->userList->setFont(font);

    // 初始状态
    ui->disconnectButton->setEnabled(false);
    ui->messageInput->setEnabled(false);
    ui->sendButton->setEnabled(false);
    ui->uploadButton->setEnabled(false);

    // 设置状态栏文本
    ui->statusLabel->setText("未连接");
    ui->statusLabel->setStyleSheet("color: gray;");
}


void Widget::setupDefaultValues()
{
    ui->serverAddressInput->setText(serverAddress);
    ui->serverPortInput->setText(QString::number(serverPort));

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
void Widget::onSocketReadyRead()
{
    while (tcpSocket->bytesAvailable() > 0) {
        QByteArray data = tcpSocket->readLine(); // 按行读取

        if (data.isEmpty()) continue;

        // 检查是否是二进制数据
        if (isBinaryData(data)) {
            qDebug() << "收到二进制数据，跳过显示";
            continue; // 跳过二进制数据
        }

        QString message = QString::fromUtf8(data).trimmed();

        // 尝试解析JSON消息
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(message.toUtf8(), &parseError);

        if (parseError.error == QJsonParseError::NoError) {
            // 是JSON消息
            processJsonMessage(jsonDoc.object());
        } else {
            // 是普通文本消息
            processTextMessage(message);
        }
    }
}

// 检查是否是二进制数据
bool Widget::isBinaryData(const QByteArray &data)
{
    // 检查数据中非打印字符的比例
    int nonPrintable = 0;
    for (int i = 0; i < data.size(); ++i) {
        unsigned char c = data.at(i);
        // 非打印字符（除空格、换行、制表符等）
        if (c < 32 && c != 9 && c != 10 && c != 13) {
            nonPrintable++;
        }
        // 如果检测到PNG文件头等二进制标志
        if (i > 0 && data.at(i-1) == (char)0x89 && data.at(i) == 'P') {
            return true;
        }
    }

    // 如果超过10%是非打印字符，很可能是二进制数据
    return (nonPrintable * 10 > data.size());
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
    ui->uploadButton->setEnabled(true);

    // 发送登录消息
    QString loginMsg = QString("LOGIN:%1").arg(username);
    tcpSocket->write(loginMsg.toUtf8());

    // 显示系统消息
    appendSystemMessage(QString("已连接到服务器 %1:%2").arg(serverAddress).arg(serverPort));

    // 延迟请求用户列表（等待服务器处理登录）
    QTimer::singleShot(100, this, &Widget::updateUserList);
}

void Widget::onUploadClicked()
{
    if (!isConnected) {
        QMessageBox::warning(this, "上传失败", "未连接到服务器");
        return;
    }

    // 打开文件对话框
    QStringList filters;
    filters << "所有文件 (*.*)"
            << "图片文件 (*.jpg *.jpeg *.png *.bmp *.gif *.webp)"
            << "视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv)"
            << "音频文件 (*.mp3 *.wav *.flac *.ogg)"
            << "文档文件 (*.pdf *.doc *.docx *.txt *.xlsx *.pptx)";

    QString filePath = QFileDialog::getOpenFileName(
        this,
        "选择要上传的文件",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        filters.join(";;")
        );

    if (filePath.isEmpty()) {
        return;
    }

    // 检查文件大小（限制为50MB）
    QFileInfo fileInfo(filePath);
    if (fileInfo.size() > 50 * 1024 * 1024) {
        QMessageBox::warning(this, "文件太大", "文件大小超过50MB限制");
        return;
    }

    sendFile(filePath);
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
    ui->uploadButton->setEnabled(false);

    // 清空用户列表
    ui->userList->clear();

    // 添加"所有人"选项
    QListWidgetItem *item = new QListWidgetItem("所有人");
    ui->userList->addItem(item);
    ui->userList->setCurrentItem(item);
    currentChatTarget = "所有人";

    // 显示系统消息
    appendSystemMessage("与服务器的连接已断开");

    // 尝试自动重连（可选）
    if (ui->autoReconnectCheck->isChecked()) {
        QTimer::singleShot(3000, this, &Widget::connectToServer);
    }
}
void Widget::processImageMessage(const QByteArray &data)
{
    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_5_15);

    qint32 senderNameLength;
    stream >> senderNameLength;

    char *senderNameBuffer = new char[senderNameLength + 1];
    stream.readRawData(senderNameBuffer, senderNameLength);
    senderNameBuffer[senderNameLength] = '\0';
    QString senderName = QString::fromUtf8(senderNameBuffer, senderNameLength);
    delete[] senderNameBuffer;

    qint32 imageDataLength;
    stream >> imageDataLength;

    QByteArray imageData(imageDataLength, '\0');
    stream.readRawData(imageData.data(), imageDataLength);

    qint32 fileNameLength;
    stream >> fileNameLength;

    char *fileNameBuffer = new char[fileNameLength + 1];
    stream.readRawData(fileNameBuffer, fileNameLength);
    fileNameBuffer[fileNameLength] = '\0';
    QString fileName = QString::fromUtf8(fileNameBuffer, fileNameLength);
    delete[] fileNameBuffer;

    // 加载图片
    QImage image;
    image.loadFromData(imageData);

    if (!image.isNull()) {
        // 保存到本地
        QString saveDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/LANChat/";
        QDir().mkpath(saveDir);

        // 如果文件名已存在，添加时间戳
        QString savePath = saveDir + fileName;
        QFileInfo fileInfo(savePath);
        if (fileInfo.exists()) {
            QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
            QString baseName = fileInfo.baseName();
            QString suffix = fileInfo.suffix();
            savePath = saveDir + baseName + "_" + timestamp + "." + suffix;
        }

        // 保存图片
        image.save(savePath);

        // 调用更新后的函数，传递5个参数
        appendImageMessage(senderName, image, fileName, savePath, senderName != username);
    }
}
void Widget::processJsonMessage(const QJsonObject &jsonObj)
{
    if (!jsonObj.contains("type")) return;

    QString type = jsonObj["type"].toString();
    QString sender = jsonObj["sender"].toString();

    if (type == "text") {
        // 检查是否为私聊消息
        bool isPrivate = jsonObj.contains("isPrivate") && jsonObj["isPrivate"].toBool();
        QString content = jsonObj["content"].toString();
        QString timestamp = jsonObj["timestamp"].toString();

        if (isPrivate) {
            // 私聊消息
            QString displayMsg = QString("[私聊] %1").arg(content);
            appendMessage(sender, displayMsg, sender == username);

            // 保存到私聊历史
            if (privateChats.contains(sender)) {
                privateChats[sender].messages.append(QString("[%1] %2: %3")
                                                         .arg(QTime::currentTime().toString("hh:mm"))
                                                         .arg(sender)
                                                         .arg(content));
            }
        } else {
            // 普通群聊消息
            appendMessage(sender, content, sender == username);
        }
    }
    else if (type == "private") {
        // 处理私聊消息
        QString target = jsonObj["target"].toString();
        QString content = jsonObj["content"].toString();
        QString timestamp = jsonObj["timestamp"].toString();

        // 如果当前不在与发送者的私聊中，启动私聊
        if (!privateChats.contains(sender)) {
            startPrivateChat(sender);
        }

        // 显示私聊消息
        QString displayMsg = QString("[私聊] %1").arg(content);
        appendMessage(sender, displayMsg, sender == username);

        // 保存到私聊历史
        if (privateChats.contains(sender)) {
            privateChats[sender].messages.append(QString("[%1] %2: %3")
                                                     .arg(QTime::currentTime().toString("hh:mm"))
                                                     .arg(sender)
                                                     .arg(content));
        }

        // 显示通知（如果窗口不在前台）
        if (!isActiveWindow()) {
            showNotification("私聊消息", QString("%1: %2").arg(sender).arg(content));
        }
    }
    else if (type == "user_status") {
        // 处理用户状态变化
        QString user = jsonObj["username"].toString();
        bool online = jsonObj["online"].toBool();

        // 更新用户列表显示
        updateUserListWithStatus(user, online);
    }
    else if (type == "user_list") {
        // 处理用户列表更新
        QJsonArray usersArray = jsonObj["users"].toArray();
        updateUserListFromJson(usersArray);
    }
    else if (type == "error") {
        // 处理错误消息
        QString errorMsg = jsonObj["message"].toString();
        appendSystemMessage(QString("错误: %1").arg(errorMsg));
    }
    else if (type == "file_base64" || type == "image_base64") {
        QString fileName = jsonObj["filename"].toString();
        qint64 fileSize = jsonObj["filesize"].toString().toLongLong();
        QString base64Data = jsonObj["filedata"].toString();

        // 清理Base64数据：移除空格和换行符
        base64Data = base64Data.replace(QRegularExpression("\\s+"), "");

        // 验证Base64数据长度
        if (base64Data.length() % 4 != 0) {
            qDebug() << "Base64数据长度错误，尝试补全";
            int padding = 4 - (base64Data.length() % 4);
            base64Data += QString(padding, '=');
        }

        // 解码Base64数据
        QByteArray fileData = QByteArray::fromBase64(base64Data.toUtf8());

        if (fileData.isEmpty()) {
            qDebug() << "Base64解码失败:" << fileName;
            appendSystemMessage(QString("文件 %1 解码失败").arg(fileName));
            return;
        }

        // 验证文件大小
        if (fileData.size() != fileSize && fileSize > 0) {
            qDebug() << "文件大小不匹配，解码后:" << fileData.size() << "期望:" << fileSize;
        }

        // 保存文件
        QString savePath = saveBase64File(fileName, fileData, type == "image_base64");

        if (!savePath.isEmpty()) {
            QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");

            if (type == "image_base64") {
                QImage image;
                if (image.loadFromData(fileData)) {
                    // 检查是否是私聊消息
                    bool isPrivate = jsonObj.contains("target") &&
                                     jsonObj["target"].toString() != "所有人" &&
                                     jsonObj["target"].toString() != "";

                    // 如果是私聊消息，设置当前聊天目标
                    if (isPrivate) {
                        QString target = jsonObj["target"].toString();
                        if (target != username) {
                            currentChatTarget = target;
                        }
                    }

                    appendImageMessage(sender, image, fileName, savePath, sender == username);
                } else {
                    // 如果图片加载失败，显示为普通文件
                    appendFileMessage(sender, fileName, fileData.size(), savePath, sender == username);
                }
            } else {
                // 检查是否是私聊消息
                bool isPrivate = jsonObj.contains("target") &&
                                 jsonObj["target"].toString() != "所有人" &&
                                 jsonObj["target"].toString() != "";

                // 如果是私聊消息，设置当前聊天目标
                if (isPrivate) {
                    QString target = jsonObj["target"].toString();
                    if (target != username) {
                        currentChatTarget = target;
                    }
                }

                appendFileMessage(sender, fileName, fileData.size(), savePath, sender == username);
            }
        } else {
            appendSystemMessage(QString("无法保存文件: %1").arg(fileName));
        }
    }
}
// 更新用户状态
void Widget::updateUserListWithStatus(const QString &user, bool online)
{
    for (int i = 0; i < ui->userList->count(); ++i) {
        QListWidgetItem *item = ui->userList->item(i);
        QString itemText = item->text();

        QString cleanName = itemText;
        cleanName = cleanName.replace(" (我)", "");
        cleanName = cleanName.replace(" [离线]", "");
        cleanName = cleanName.replace(" 💬", "");

        if (cleanName == user) {
            // 更新显示
            QString newText = cleanName;

            // 如果是自己，添加标记
            if (user == username) {
                newText += " (我)";
            }

            // 更新在线状态
            if (!online) {
                newText += " [离线]";
            }

            // 保持私聊标记
            if (itemText.contains(" 💬")) {
                newText += " 💬";
            }

            item->setText(newText);

            // 更新颜色
            if (user == username) {
                item->setForeground(Qt::green);
            } else if (!online) {
                item->setForeground(Qt::gray);
            } else {
                item->setForeground(Qt::black);
            }

            break;
        }
    }
}
void Widget::updateUserListFromJson(const QJsonArray &usersArray)
{
    // 保存当前选择
    QString selectedUser;
    QListWidgetItem* selectedItem = ui->userList->currentItem();
    if (selectedItem) {
        selectedUser = selectedItem->text();
        // 移除可能的私聊标记
        selectedUser = selectedUser.replace(" 💬", "");
        selectedUser = selectedUser.replace(" (我)", "");
    }

    ui->userList->clear();

    // 添加"所有人"选项
    QListWidgetItem *allItem = new QListWidgetItem("所有人");
    ui->userList->addItem(allItem);

    for (const QJsonValue &userValue : usersArray) {
        QJsonObject userObj = userValue.toObject();
        QString userName = userObj["username"].toString();
        bool online = userObj["online"].toBool();
        bool isSelf = userObj["isSelf"].toBool();

        if (userName.isEmpty()) continue;

        QString displayName = userName;

        // 如果是自己，添加标记
        if (isSelf) {
            displayName = userName + " (我)";
        }

        // 如果不在线，添加离线标记
        if (!online) {
            displayName += " [离线]";
        }

        // 如果有未读私聊消息，添加标记
        if (privateChats.contains(userName) && !privateChats[userName].messages.isEmpty()) {
            displayName += " 💬";
        }

        QListWidgetItem *item = new QListWidgetItem(displayName);

        // 设置颜色和字体
        if (isSelf) {
            item->setForeground(Qt::green);
            item->setFont(QFont("Arial", 10, QFont::Bold));
        } else if (!online) {
            item->setForeground(Qt::gray);
            item->setFont(QFont("Arial", 9));
        } else if (privateChats.contains(userName)) {
            item->setForeground(Qt::blue);
            item->setFont(QFont("Arial", 10, QFont::Bold));
        }

        ui->userList->addItem(item);

        // 恢复之前的选择
        if (userName == selectedUser) {
            item->setSelected(true);
            currentChatTarget = selectedUser;
        }
    }

    // 如果没有选择，默认选择"所有人"
    if (!selectedItem && ui->userList->count() > 0) {
        ui->userList->setCurrentItem(allItem);
        currentChatTarget = "所有人";
    }
}
// 更新私聊指示器
void Widget::updatePrivateChatIndicator()
{
    for (int i = 0; i < ui->userList->count(); ++i) {
        QListWidgetItem *item = ui->userList->item(i);
        QString itemText = item->text();

        QString cleanName = itemText;
        cleanName = cleanName.replace(" (我)", "");
        cleanName = cleanName.replace(" [离线]", "");
        cleanName = cleanName.replace(" 💬", "");

        // 如果有私聊历史，添加标记
        if (privateChats.contains(cleanName) && !privateChats[cleanName].messages.isEmpty()) {
            if (!itemText.contains(" 💬")) {
                item->setText(itemText + " 💬");
                item->setForeground(Qt::blue);
            }
        }
    }
}
void Widget::appendMessage(const QString &sender, const QString &message, bool isSelf)
{
    QString time = getTimestamp();
    QString html;

    // 检查是否是私聊消息
    bool isPrivate = message.contains("[私聊]");
    QString displayMessage = message;

    if (isPrivate) {
        displayMessage = message.mid(4); // 移除"[私聊]"前缀
    }

    if (isSelf) {
        // 自己发送的消息
        QString bubbleColor = isPrivate ? "#049e04":"#333";

        html = QString(
                   "<br/>"
                   "<div style='margin: 5px; text-align: right;'>"
                   "<div style='display: inline-block; max-width: 70%%; text-align: left;'>"
                   "<div style='color: %1; padding: 8px 12px; "
                   "border-radius: 12px; border-bottom-right-radius: 4px; margin-left: auto; "
                   "word-wrap: break-word;'>%2</div>"
                   "<div style='color: #666; font-size: 10px; margin-top: 2px;'>"
                   "<span style='color:%3; font-weight: bold;'>[我]</span> "
                   "<span style='color: #999;'>%4</span>"
                   "</div>"
                   "</div>"
                   "</div>")
                   .arg( bubbleColor, displayMessage.toHtmlEscaped(), bubbleColor,time);
    } else {
        // 他人发送的消息
        QString bubbleColor = isPrivate ? "#4CAF50" : "#333";

        html = QString(
                   "<br/>"
                   "<div style='margin: 5px;'>"
                   "<div style='color: #666; font-size: 10px;'>"
                   "<span style='color: %1; font-weight: bold; margin-right: 5px;'>[%2]</span>"
                   "</div>"
                   "<div style='color: %1; "
                   "padding: 8px 12px; border-radius: 12px; border-bottom-left-radius: 4px; "
                   "display: inline-block; max-width: 70%%; margin-top: 2px; "
                   "word-wrap: break-word;'>%3</div>"
                   "<div style='color: #999; font-size: 9px; margin-top: 2px;'>%4</div>"
                   "</div>")
                   .arg(bubbleColor,
                        sender,
                        displayMessage.toHtmlEscaped(),
                        time);
    }

    QTextCursor cursor(ui->chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    // 滚动到底部
    QScrollBar *scrollbar = ui->chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}
// 自己发送的文件消息显示在右侧
void Widget::appendFileMessage(const QString &sender, const QString &fileName, qint64 fileSize,
                               const QString &filePath, bool isSelf)
{
    QString sizeStr = formatFileSize(fileSize);
    QString html;

    QString fileIcon;
    QString fileExtension = QFileInfo(fileName).suffix().toLower();

    // 根据文件类型设置图标
    if (fileExtension == "mp4" || fileExtension == "avi" || fileExtension == "mkv" ||
        fileExtension == "mov" || fileExtension == "wmv") {
        fileIcon = "🎬";
    } else if (fileExtension == "mp3" || fileExtension == "wav" || fileExtension == "flac" ||
               fileExtension == "ogg") {
        fileIcon = "🎵";
    } else if (fileExtension == "jpg" || fileExtension == "jpeg" || fileExtension == "png" ||
               fileExtension == "bmp" || fileExtension == "gif") {
        fileIcon = "🖼️";
    } else if (fileExtension == "pdf") {
        fileIcon = "📄";
    } else if (fileExtension == "doc" || fileExtension == "docx") {
        fileIcon = "📝";
    } else if (fileExtension == "zip" || fileExtension == "rar" || fileExtension == "7z") {
        fileIcon = "📦";
    } else {
        fileIcon = "📎";
    }

    // 检查是否为私聊消息
    bool isPrivate = currentChatTarget != "所有人" && currentChatTarget != username;

    // 根据是否为私聊设置颜色
    QString bubbleColor = isPrivate ? "#049e04" : "#333";
    QString linkColor = "#007AFF"; // 链接颜色保持蓝色

    // 创建文件URL链接
    QString fileUrl = QUrl::fromLocalFile(filePath).toString();
    QString downloadLink = QString("<a href='%1' style='color: %2; text-decoration: none;'>💾 点击下载</a>")
                               .arg(fileUrl, linkColor);

    QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");

    if (isSelf) {
        // 自己发送的文件消息 - 显示在右侧
        html = QString(
                   "<br/>"
                   "<div style='margin: 5px; text-align: right;'>"
                   "<div style='display: inline-block; max-width: 70%%; text-align: left;'>"
                   "<div style='color: %1; padding: 8px 12px; "
                   "border-radius: 12px; border-bottom-right-radius: 4px; margin-left: auto; "
                   "word-wrap: break-word; max-width: 300px;'>"
                   "<div style='font-size: 16px; margin-bottom: 5px;'>%2</div>"
                   "<div style='font-weight: bold; font-size: 12px;'>%3</div>"
                   "<div style='font-size: 11px; opacity: 0.9; margin-top: 5px;'>"
                   "📏 大小: %4<br>"
                   "%5"
                   "</div>"
                   "</div>"
                   "<div style='color: #666; font-size: 10px; margin-top: 2px;'>"
                   "<span style='color:%1; font-weight: bold;'>[我]</span> "
                   "<span style='color: #999;'>%6</span>"
                   "</div>"
                   "</div>"
                   "</div>")
                   .arg(bubbleColor,
                        fileIcon,
                        fileName.toHtmlEscaped(),
                        sizeStr,
                        downloadLink,
                        currentTime);
    } else {
        // 他人发送的文件消息 - 显示在左侧
        html = QString(
                   "<br/>"
                   "<div style='margin: 5px;'>"
                   "<div style='color: #666; font-size: 10px;'>"
                   "<span style='color: %1; font-weight: bold; margin-right: 5px;'>[%2]</span>"
                   "</div>"
                   "<div style='color: %1; padding: 8px 12px; "
                   "border-radius: 12px; border-bottom-left-radius: 4px; "
                   "display: inline-block; max-width: 300px; margin-top: 2px; "
                   "word-wrap: break-word;'>"
                   "<div style='font-size: 16px; margin-bottom: 5px;'>%3</div>"
                   "<div style='font-weight: bold; font-size: 12px;'>%4</div>"
                   "<div style='font-size: 11px; opacity: 0.9; margin-top: 5px;'>"
                   "📏 大小: %5<br>"
                   "%6"
                   "</div>"
                   "</div>"
                   "<div style='color: #999; font-size: 9px; margin-top: 2px;'>%7</div>"
                   "</div>")
                   .arg(bubbleColor,
                        sender,
                        fileIcon,
                        fileName.toHtmlEscaped(),
                        sizeStr,
                        downloadLink,
                        currentTime);
    }

    QTextCursor cursor(ui->chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    QScrollBar *scrollbar = ui->chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

// 修改 appendImageMessage 函数，添加私聊颜色支持
void Widget::appendImageMessage(const QString &sender, const QImage &image, const QString &fileName,
                                const QString &filePath, bool isSelf)
{
    // 缩放图片以适应聊天窗口
    QImage scaledImage = image.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 转换为Base64
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    scaledImage.save(&buffer, "PNG");
    QString base64Image = QString::fromLatin1(byteArray.toBase64().data());

    QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString html;

    // 检查是否为私聊消息
    bool isPrivate = currentChatTarget != "所有人" && currentChatTarget != username;

    // 根据是否为私聊设置颜色
    QString bubbleColor = isPrivate ? "#049e04" : "#333";
    QString linkColor = "#007AFF"; // 链接颜色保持蓝色

    // 创建文件URL链接
    QString fileUrl = QUrl::fromLocalFile(filePath).toString();
    QString downloadLink = QString("<a href='%1' style='color: %2; text-decoration: none;'>%3</a>")
                               .arg(fileUrl, linkColor, fileName.toHtmlEscaped());

    if (isSelf) {
        // 自己发送的图片消息 - 显示在右侧
        html = QString(
                   "<br/>"
                   "<div style='margin: 10px; text-align: right;'>"
                   "<div style='display: inline-block; max-width: 70%%; text-align: left;'>"
                   "<div style='color: %1; padding: 8px; "
                   "border-radius: 12px; border-bottom-right-radius: 4px; margin-left: auto; "
                   "word-wrap: break-word; max-width: 300px;'>"
                   "<a href='%2' style='text-decoration: none;'>"
                   "<img src='data:image/png;base64,%3' "
                   "style='max-width: 280px; border-radius: 5px; cursor: pointer; display: block;'/>"
                   "</a>"
                   "<div style='font-size: 10px; margin-top: 5px; opacity: 0.9; color: %1;'>"
                   "🖼️ %4<br>"
                   "💾 点击图片查看"
                   "</div>"
                   "</div>"
                   "<div style='color: #666; font-size: 10px; margin-top: 2px;'>"
                   "<span style='color:%1; font-weight: bold;'>[我]</span> "
                   "<span style='color: #999;'>%5</span>"
                   "</div>"
                   "</div>"
                   "</div>")
                   .arg(bubbleColor,
                        fileUrl,
                        base64Image,
                        downloadLink,
                        currentTime);
    } else {
        // 他人发送的图片消息 - 显示在左侧
        html = QString(
                   "<br/>"
                   "<div style='margin: 10px;'>"
                   "<div style='color: #666; font-size: 10px;'>"
                   "<span style='color: %1; font-weight: bold;'>[%2]</span> "
                   "</div>"
                   "<div style='color: %1; padding: 8px; "
                   "border-radius: 12px; border-bottom-left-radius: 4px; "
                   "display: inline-block; max-width: 300px; margin-top: 2px; margin-bottom: 5px;'>"
                   "<a href='%3' style='text-decoration: none;'>"
                   "<img src='data:image/png;base64,%4' "
                   "style='max-width: 280px; border-radius: 5px; cursor: pointer; display: block;'/>"
                   "</a>"
                   "<div style='color: %1; font-size: 10px; margin-top: 5px;'>"
                   "🖼️ %5<br>"
                   "💾 点击图片查看"
                   "</div>"
                   "</div>"
                   "<div style='color: #999; font-size: 9px;'>接收时间: %6</div>"
                   "</div>")
                   .arg(bubbleColor,
                        sender,
                        fileUrl,
                        base64Image,
                        downloadLink,
                        currentTime);
    }

    QTextCursor cursor(ui->chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    QScrollBar *scrollbar = ui->chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}
void Widget::appendSystemMessage(const QString &message)
{
    QString html = QString(
                       "<br/>"
                       "<div style='text-align: center; margin: 10px;'>"
                       "<span style='color: #888; font-size: 11px; "
                       "padding: 5px 10px; border-radius: 10px; "
                       "display: inline-block;'>"
                       "[系统]%1<br>"
                       "<span style='font-size: 9px; color: #aaa;'>%2</span>"
                       "</span>"
                       "</div>")
                       .arg(message.toHtmlEscaped(), QDateTime::currentDateTime().toString("hh:mm:ss"));

    QTextCursor cursor(ui->chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    // 滚动到底部
    QScrollBar *scrollbar = ui->chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}
void Widget::processTextMessage(const QString &message)
{
    if (message.isEmpty()) return;

    // 处理服务器系统消息
    if (message.startsWith("[系统]") || message.startsWith("[System]")) {
        QString systemMsg = message.mid(message.indexOf("]") + 1).trimmed();
        appendSystemMessage(systemMsg);

        // 如果系统消息包含用户加入或离开，更新用户列表
        if (systemMsg.contains("加入了") || systemMsg.contains("离开了") ||
            systemMsg.contains("加入") || systemMsg.contains("离开")) {
            QTimer::singleShot(500, this, &Widget::updateUserList);
        }
        return;
    }

    // 处理用户列表消息
    else if (message.contains("在线用户") || message.contains("在线用户:")) {
        QStringList parts = message.split(":");
        if (parts.size() > 1) {
            QString userListStr = parts[1].trimmed();
            QStringList users = userListStr.split(",", Qt::SkipEmptyParts);

            // 先保存当前选择的用户
            QString selectedUser;
            QListWidgetItem* selectedItem = ui->userList->currentItem();
            if (selectedItem) {
                selectedUser = selectedItem->text();
            }

            // 清空并重新填充用户列表
            ui->userList->clear();

            for (const QString &user : users) {
                QString trimmedUser = user.trimmed();
                if (!trimmedUser.isEmpty()) {
                    QListWidgetItem *item = new QListWidgetItem(trimmedUser);

                    // 如果是自己，添加特殊标记
                    if (trimmedUser == username) {
                        item->setText(trimmedUser + " (我)");
                        item->setForeground(Qt::green);
                        item->setFont(QFont("Arial", 10, QFont::Bold));
                    }

                    ui->userList->addItem(item);

                    // 恢复之前的选择
                    if (trimmedUser == selectedUser) {
                        item->setSelected(true);
                        currentChatTarget = selectedUser;
                    }
                }
            }

            // 如果没有选择且列表不为空，默认选择第一个
            if (ui->userList->count() > 0 && !selectedItem) {
                ui->userList->setCurrentRow(0);
                currentChatTarget = ui->userList->item(0)->text();
            }

            // 更新状态栏显示在线人数
            ui->statusLabel->setText(QString("已连接 - 在线: %1人").arg(users.size()));
        }
        return;
    }

    // 处理普通聊天消息格式 [时间] 用户名: 消息
    else {
        QRegularExpression pattern("\\[(\\d{1,2}:\\d{2})\\] (.+?): (.+)");
        QRegularExpressionMatch match = pattern.match(message);

        if (match.hasMatch()) {
            QString time = match.captured(1);
            QString sender = match.captured(2);
            QString content = match.captured(3);

            appendMessage(sender, content, sender == username);
        } else {
            // 如果不是标准格式，显示为系统消息
            appendSystemMessage(message);
        }
    }
}
void Widget::saveReceivedFile(const QByteArray &fileData, const QString &fileName, FileType fileType)
{
    QString saveDir;

    switch (fileType) {
    case Image:
        saveDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/LANChat/";
        break;
    case Video:
        saveDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/LANChat/";
        break;
    case Audio:
        saveDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/LANChat/";
        break;
    default:
        saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/LANChat/";
        break;
    }

    QDir().mkpath(saveDir);

    // 如果文件名已存在，添加时间戳
    QString savePath = saveDir + fileName;
    QFileInfo fileInfo(savePath);
    if (fileInfo.exists()) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QString baseName = fileInfo.baseName();
        QString suffix = fileInfo.suffix();
        savePath = saveDir + baseName + "_" + timestamp + "." + suffix;
    }

    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(fileData);
        file.close();

        appendSystemMessage(QString("文件已保存到: %1").arg(savePath));

        // 如果是图片，显示在聊天窗口（传递5个参数）
        if (fileType == Image) {
            QImage image;
            if (image.load(savePath)) {
                appendImageMessage("系统", image, fileName, savePath, false);
            }
        } else if (fileType == Video) {
            // 注意：appendFileMessage 函数也需要更新为5个参数
            appendFileMessage("系统", fileName, fileData.size(), savePath, false);
        } else {
            // 其他类型文件
            appendFileMessage("系统", fileName, fileData.size(), savePath, false);
        }
    }
}
Widget::FileType Widget::getFileType(const QString &filePath)
{
    QMimeDatabase mimeDb;
    QMimeType mimeType = mimeDb.mimeTypeForFile(filePath);
    QString mimeName = mimeType.name();

    if (mimeName.startsWith("image/")) {
        return Image;
    } else if (mimeName.startsWith("video/")) {
        return Video;
    } else if (mimeName.startsWith("audio/")) {
        return Audio;
    } else if (mimeName == "application/pdf" ||
               mimeName == "application/msword" ||
               mimeName == "application/vnd.openxmlformats-officedocument.wordprocessingml.document" ||
               mimeName == "text/plain") {
        return Document;
    } else {
        return Other;
    }
}

QString Widget::getFileTypeString(FileType type)
{
    switch (type) {
    case Image: return "图片";
    case Video: return "视频";
    case Audio: return "音频";
    case Document: return "文档";
    default: return "文件";
    }
}

QString Widget::formatFileSize(qint64 bytes)
{
    const QStringList units = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = bytes;

    while (size >= 1024 && unitIndex < units.size() - 1) {
        size /= 1024;
        unitIndex++;
    }

    return QString("%1 %2").arg(size, 0, 'f', 2).arg(units[unitIndex]);
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
// 修改 sendFile 函数，添加私聊支持
void Widget::sendFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "无法打开文件");
        return;
    }

    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName();
    qint64 fileSize = fileInfo.size();
    FileType fileType = getFileType(filePath);

    // 限制文件大小（例如10MB）
    if (fileSize > 10 * 1024 * 1024) {
        QMessageBox::warning(this, "文件太大", "文件大小超过10MB限制");
        return;
    }

    // 读取文件数据
    QByteArray fileData = file.readAll();
    file.close();

    // **关键：更安全的Base64编码和清理**
    QString base64Data = fileData.toBase64();

    // 彻底清理Base64字符串
    base64Data = base64Data.replace("\n", "");
    base64Data = base64Data.replace("\r", "");
    base64Data = base64Data.replace("\"", "\\\"");
    base64Data = base64Data.replace("\\", "\\\\");
    base64Data = base64Data.replace("\t", "");

    // **添加Base64完整性检查**
    if (base64Data.length() % 4 != 0) {
        // Base64长度必须是4的倍数
        int padding = 4 - (base64Data.length() % 4);
        base64Data += QString(padding, '=');
    }

    // **验证Base64数据**
    QByteArray testData = QByteArray::fromBase64(base64Data.toUtf8());
    if (testData.isEmpty() && !fileData.isEmpty()) {
        QMessageBox::warning(this, "错误", "Base64编码失败，文件可能包含无效字符");
        return;
    }

    // 保存到本地
    QString savePath = saveBase64File(fileName, fileData, fileType == Image);
    if (savePath.isEmpty()) {
        QMessageBox::warning(this, "错误", "无法保存文件到本地");
        return;
    }

    // **构建更安全的JSON消息**
    QJsonObject fileJson;
    fileJson["type"] = fileType == Image ? "image_base64" : "file_base64";
    fileJson["sender"] = username;
    fileJson["filename"] = fileName;
    fileJson["filesize"] = QString::number(fileSize);
    fileJson["filedata"] = base64Data;

    // 如果是私聊，添加目标用户
    if (currentChatTarget != "所有人" && currentChatTarget != username) {
        fileJson["target"] = currentChatTarget;
    }

    // 本地显示
    if (fileType == Image) {
        QImage image(filePath);
        if (!image.isNull()) {
            fileJson["width"] = image.width();
            fileJson["height"] = image.height();
            appendImageMessage(username, image, fileName, savePath, true);
        }
    } else {
        appendFileMessage(username, fileName, fileSize, savePath, true);
    }

    // **关键：发送前验证JSON**
    QJsonDocument doc(fileJson);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 验证JSON是否有效
    QJsonParseError parseError;
    QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qDebug() << "JSON格式错误:" << parseError.errorString();
        QMessageBox::warning(this, "错误", "消息格式错误，无法发送");
        return;
    }

    qDebug() << "发送文件:" << fileName << "大小:" << fileSize
             << "Base64长度:" << base64Data.length()
             << "JSON长度:" << jsonData.length();

    // 发送JSON数据（确保以换行符结尾）
    if (tcpSocket->write(jsonData + "\n") == -1) {
        QMessageBox::warning(this, "发送失败", "网络错误，文件发送失败");
        return;
    }

    // 显示上传状态
    ui->uploadProgressBar->setVisible(true);
    ui->uploadProgressBar->setValue(100);
    ui->uploadStatusLabel->setText(QString("已上传: %1").arg(fileName));

    QTimer::singleShot(2000, this, [this]() {
        ui->uploadProgressBar->setVisible(false);
        ui->uploadStatusLabel->setText("就绪");
    });
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

    // 如果当前是私聊目标，发送私聊消息
    if (currentChatTarget != "所有人" && currentChatTarget != username) {
        sendPrivateMessage(message, currentChatTarget);
        ui->messageInput->clear();
        return;
    }

    // 普通群聊消息（原代码不变）
    QJsonObject msgJson;
    msgJson["type"] = "text";
    msgJson["sender"] = username;
    msgJson["content"] = message;
    msgJson["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    QJsonDocument doc(msgJson);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    tcpSocket->write(jsonString.toUtf8() + "\n");

    appendMessage(username, message, true);
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

QString Widget::saveBase64File(const QString &fileName, const QByteArray &fileData, bool isImage)
{
    QString saveDir;

    if (isImage) {
        saveDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/LANChat/";
    } else {
        saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/LANChat/";
    }

    QDir().mkpath(saveDir);

    // 如果文件名已存在，添加时间戳
    QString savePath = saveDir + fileName;
    QFileInfo fileInfo(savePath);
    if (fileInfo.exists()) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QString baseName = fileInfo.baseName();
        QString suffix = fileInfo.suffix();
        savePath = saveDir + baseName + "_" + timestamp + "." + suffix;
    }

    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(fileData);
        file.close();
        return savePath;
    }

    return "";
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

    // 移除各种标记
    selectedUser = selectedUser.replace(" (我)", "");
    selectedUser = selectedUser.replace(" [离线]", "");
    selectedUser = selectedUser.replace(" 💬", "");

    if (selectedUser != currentChatTarget) {
        currentChatTarget = selectedUser;

        if (selectedUser == "所有人") {
            appendSystemMessage("现在与所有人聊天");
        } else {
            appendSystemMessage(QString("正在与 %1 聊天").arg(selectedUser));

            // 如果选择的是私聊目标，清空未读标记
            if (privateChats.contains(selectedUser)) {
                // 清除私聊标记
                QString newText = selectedUser;
                if (selectedUser == username) {
                    newText += " (我)";
                }
                item->setText(newText);
                item->setForeground(Qt::black);
            }
        }
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
