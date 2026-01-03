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

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , tcpSocket(new QTcpSocket(this))
    , username("游客")
    , currentChatTarget("所有人")
    , isConnected(false)
    , serverAddress("127.0.0.1")
    , serverPort(8888)
    , totalFileSize(0)
    , currentUpload(nullptr)
{
    ui->setupUi(this);

    // 设置窗口标题和图标
    setWindowTitle("LAN 聊天客户端 - 文件传输支持");
    // setWindowIcon(QIcon(":/icons/chat.png"));  // 如果有资源文件的话

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
    connect(tcpSocket, &QTcpSocket::bytesWritten, this, &Widget::onSocketBytesWritten);
    connect(tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &Widget::onSocketError);
}
void Widget::setupUI()
{
    // 设置控件属性
    ui->chatText->setReadOnly(true);
    ui->chatText->setAcceptRichText(true);
    ui->chatText->setOpenLinks(true);
    ui->chatText->setOpenExternalLinks(true);

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

void Widget::startNextUpload()
{
    if (pendingUploads.isEmpty()) {
        currentUpload = nullptr;
        ui->uploadProgressBar->setVisible(false);
        ui->uploadStatusLabel->setText("就绪");
        return;
    }

    currentUpload = pendingUploads.takeFirst();
    currentUpload->isSending = true;

    // 发送文件头信息
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);

    // 文件头格式：类型(4字节) + 文件名长度(4字节) + 文件名 + 文件大小(8字节)
    stream << (qint32)currentUpload->fileType;

    QByteArray fileNameBytes = currentUpload->fileName.toUtf8();
    stream << (qint32)fileNameBytes.size();
    stream.writeRawData(fileNameBytes.constData(), fileNameBytes.size());

    stream << (qint64)currentUpload->fileSize;

    // 添加头标识
    QByteArray fullHeader;
    fullHeader.append("FILE_START");
    fullHeader.append(header);

    tcpSocket->write(fullHeader);

    // 显示上传状态
    ui->uploadProgressBar->setVisible(true);
    ui->uploadProgressBar->setValue(0);
    ui->uploadStatusLabel->setText(QString("正在上传: %1").arg(currentUpload->fileName));

    // 开始发送第一个数据块
    QTimer::singleShot(100, this, &Widget::sendFileChunk);
}

void Widget::sendFileChunk()
{
    if (!currentUpload || !currentUpload->file) {
        return;
    }

    // 读取数据块
    QByteArray chunk = currentUpload->file->read(CHUNK_SIZE);
    if (chunk.isEmpty()) {
        // 文件发送完成
        currentUpload->file->close();
        delete currentUpload->file;
        delete currentUpload;
        currentUpload = nullptr;

        // appendSystemMessage("文件上传完成");

        // 开始下一个上传
        startNextUpload();
        return;
    }

    // 发送数据块
    QByteArray dataPacket;
    dataPacket.append("FILE_DATA");
    dataPacket.append(chunk);

    tcpSocket->write(dataPacket);

    // 更新进度
    currentUpload->bytesWritten += chunk.size();
    updateUploadProgress(currentUpload->bytesWritten, currentUpload->bytesTotal);
}

void Widget::onSocketBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);

    // 继续发送下一个数据块
    if (currentUpload && currentUpload->isSending) {
        QTimer::singleShot(10, this, &Widget::sendFileChunk);
    }
}
void Widget::updateUploadProgress(qint64 bytesWritten, qint64 bytesTotal)
{
    int progress = (bytesTotal > 0) ? (bytesWritten * 100 / bytesTotal) : 0;
    ui->uploadProgressBar->setValue(progress);

    if (currentUpload) {
        ui->uploadStatusLabel->setText(
            QString("上传中: %1 (%2/%3)")
                .arg(currentUpload->fileName)
                .arg(formatFileSize(bytesWritten))
                .arg(formatFileSize(bytesTotal))
            );
    }
}
void Widget::sendFile(const QString &filePath)
{
    QFile *file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "无法打开文件");
        delete file;
        return;
    }

    FileTransfer *transfer = new FileTransfer();
    transfer->file = file;
    transfer->fileName = QFileInfo(filePath).fileName();
    transfer->fileSize = file->size();
    transfer->fileType = getFileType(filePath);
    transfer->bytesWritten = 0;
    transfer->bytesTotal = transfer->fileSize;
    transfer->isSending = false;

    // 先在聊天窗口显示自己的上传消息（使用更新后的函数）
    if (transfer->fileType == Image) {
        QImage image(filePath);
        if (!image.isNull()) {
            // 传递5个参数
            appendImageMessage(username, image, transfer->fileName, filePath, true);
        }
    } else {
        // 传递5个参数
        appendFileMessage(username, transfer->fileName, transfer->fileSize, filePath, true);
    }

    // 添加到上传队列
    pendingUploads.append(transfer);

    // 如果没有正在进行的上传，开始上传
    if (!currentUpload) {
        startNextUpload();
    } else {
        appendSystemMessage(QString("文件 '%1' 已添加到上传队列").arg(transfer->fileName));
    }
}
void Widget::onSocketReadyRead()
{
    while (tcpSocket->bytesAvailable() > 0) {
        QByteArray data = tcpSocket->readAll();

        // 检查是否是文件传输相关数据
        if (data.startsWith("FILE_START")) {
            processFileHeader(data.mid(10)); // 移除标识
        } else if (data.startsWith("FILE_DATA")) {
            processFileChunk(data.mid(9)); // 移除标识
        } else if (data.startsWith("IMAGE_MSG")) {
            processImageMessage(data.mid(9));
        } else {
            // 处理文本消息
            QString message = QString::fromUtf8(data).trimmed();
            processTextMessage(message);
        }
    }
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
void Widget::processFileHeader(const QByteArray &data)
{
    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_5_15);

    qint32 fileType;
    qint32 fileNameLength;
    qint64 fileSize;

    stream >> fileType;
    stream >> fileNameLength;

    char *fileNameBuffer = new char[fileNameLength + 1];
    stream.readRawData(fileNameBuffer, fileNameLength);
    fileNameBuffer[fileNameLength] = '\0';
    receivedFileName = QString::fromUtf8(fileNameBuffer, fileNameLength);
    delete[] fileNameBuffer;

    stream >> fileSize;

    // 保存接收文件的信息
    receivedFileType = static_cast<FileType>(fileType);
    totalFileSize = fileSize;

    // 清空并准备缓冲区
    uploadBuffer.clear();
    uploadBuffer.reserve(fileSize);

    // 显示接收信息和进度条
    ui->uploadProgressBar->setVisible(true);
    ui->uploadProgressBar->setValue(0);
    ui->uploadStatusLabel->setText(QString("正在接收: %1").arg(receivedFileName));

    appendSystemMessage(QString("正在接收文件: %1 (%2)")
                            .arg(receivedFileName)
                            .arg(formatFileSize(fileSize)));
}
void Widget::processFileChunk(const QByteArray &data)
{
    uploadBuffer.append(data);

    // 更新进度显示
    if (totalFileSize > 0) {
        int progress = (uploadBuffer.size() * 100) / totalFileSize;
        ui->uploadProgressBar->setValue(progress);
        ui->uploadStatusLabel->setText(
            QString("正在接收: %1 (%2%)")
                .arg(receivedFileName)
                .arg(progress)
            );
    }

    // 文件接收完成
    if (uploadBuffer.size() >= totalFileSize && totalFileSize > 0) {
        // 保存接收的文件
        saveReceivedFile(uploadBuffer, receivedFileName, receivedFileType);

        // 重置状态
        uploadBuffer.clear();
        totalFileSize = 0;
        uploadBuffer.reserve(0);

        ui->uploadProgressBar->setVisible(false);
        ui->uploadStatusLabel->setText("就绪");
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
void Widget::appendMessage(const QString &sender, const QString &message, bool isSelf)
{
    QString time = getTimestamp();
    QString html;

    if (isSelf) {
        // 自己发送的消息（左对齐，但用不同颜色区分）
        html = QString(
                       "<br/>"
                       "<div style='margin: 5px;'>"
                       "<div style='color: #666; font-size: 10px;'>"
                       "<span style='color: #0ba50b; font-weight: bold; margin-right: 5px;'>[我]:</span>"
                       "<span style='color: #049e04; padding: 4px 8px; "
                       "border-radius: 8px; display: inline; border: 1px solid #00FF00;'>%1</span>"
                       "</div>"
                       "<div style='color: #999; font-size: 9px; margin-top: 2px;'>发送时间: %2</div>"
                       "</div>")
                   .arg(message.toHtmlEscaped(), QDateTime::currentDateTime().toString("hh:mm:ss"));
    } else {
        // 他人发送的消息（左对齐）
        html = QString("<div style='margin: 5px;'>"
                       "<div style='color: #666; font-size: 10px;'>"
                       "<span style='color: #333; font-weight: bold; margin-right: 5px;'>%1</span>"
                       "<span style='background: #F0F0F0; color: black; padding: 4px 8px; "
                       "border-radius: 8px; display: inline;'>%2</span>"
                       "</div>"
                       "<div style='color: #999; font-size: 9px; margin-top: 2px;'>接收时间: %3</div>"
                       "</div>")
                   .arg(sender, message.toHtmlEscaped(), QDateTime::currentDateTime().toString("hh:mm:ss"));
    }

    QTextCursor cursor(ui->chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    // 滚动到底部
    QScrollBar *scrollbar = ui->chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void Widget::appendFileMessage(const QString &sender, const QString &fileName, qint64 fileSize,
                               const QString &filePath, bool isSelf)
{
    QString time = getTimestamp();
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

    // 创建下载链接 - 修复这里
    QString fileUrl = QUrl::fromLocalFile(filePath).toString();
    QString downloadLink = QString("<a href='%1' style='color: inherit; text-decoration: none;'>%2</a>")
                               .arg(fileUrl, fileName.toHtmlEscaped());

    if (isSelf) {
        html = QString(
                   "<br/>"
                   "<div style='margin: 5px;'>"
                   "<div style='color: #666; font-size: 10px;'>"
                   "<span style='color: #0ba50b; font-weight: bold;'>[我]</span>"
                   "</div>"
                   "<div style='color: #049e04; padding: 12px 15px; "
                   "border-radius: 10px; display: inline-block; max-width: 300px; "
                   "margin-top: 2px; margin-bottom: 5px; cursor: pointer;'>"
                   "<div style='font-size: 16px; margin-bottom: 5px;'>%1</div>"
                   "<div style='font-weight: bold; font-size: 12px;'>%2</div>"
                   "<div style='font-size: 11px; opacity: 0.9; margin-top: 5px;'>"
                   "📏 大小: %3<br>"
                   "💾 点击文件名下载"
                   "</div>"
                   "</div>"
                   "<div style='color: #999; font-size: 9px;'>发送时间: %4</div>"
                   "</div>")
                   .arg(fileIcon, downloadLink, sizeStr,
                        QDateTime::currentDateTime().toString("hh:mm:ss"));
    } else {
        html = QString("<div style='margin: 5px;'>"
                       "<div style='color: #666; font-size: 10px;'>"
                       "<span style='color: #333; font-weight: bold; margin-right: 5px;'>%1</span>"
                       "</div>"
                       "<div style='background: #F0F0F0; color: #333; padding: 12px 15px; "
                       "border-radius: 10px; display: inline-block; max-width: 300px; "
                       "margin-top: 2px; margin-bottom: 5px; cursor: pointer;'>"
                       "<div style='font-size: 16px; margin-bottom: 5px;'>%2</div>"
                       "<div style='font-weight: bold; font-size: 12px;'>%3</div>"
                       "<div style='font-size: 11px; opacity: 0.9; margin-top: 5px;'>"
                       "📏 大小: %4<br>"
                       "⏰ 时间: %5<br>"
                       "💾 点击文件名下载"
                       "</div>"
                       "</div>"
                       "<div style='color: #999; font-size: 9px; margin-top: 2px;'>发送时间: %6</div>"
                       "</div>")
                   .arg(sender, fileIcon, downloadLink, sizeStr,
                        QDateTime::currentDateTime().toString("hh:mm:ss"),
                        QDateTime::currentDateTime().toString("hh:mm:ss"));
    }

    QTextCursor cursor(ui->chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    QScrollBar *scrollbar = ui->chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

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

    QString time = getTimestamp();
    QString html;

    // 创建下载链接
    QString fileUrl = QUrl::fromLocalFile(filePath).toString();
    QString downloadLink = QString("<a href='%1' style='color: inherit; text-decoration: none;'>%2</a>")
                               .arg(fileUrl, fileName.toHtmlEscaped());

    if (isSelf) {
        html = QString(
                       "<br/>"
                       "<div style='margin: 10px;'>"
                       "<div style='color: #666; font-size: 10px;'>"
                       "<span style='color: #0ba50b; font-weight: bold;'>[我]</span>"
                       "</div>"
                       "<div style='padding: 10px; border-radius: 10px; "
                       "display: inline-block; max-width: 300px; margin-top: 2px; margin-bottom: 5px;'>"
                       "<img src='data:image/png;base64,%1' "
                       "style='max-width: 280px; border-radius: 5px; cursor: pointer;'/><br>"
                       "<div style='color:#049e04; font-size: 10px; margin-top: 5px;'>"
                       "🖼️ %2 "
                       "💾 点击文件名下载"
                       "</div>"
                       "</div>"
                       "<div style='color: #999; font-size: 9px;'>发送时间: %5</div>"
                       "</div>")
                   .arg(base64Image, downloadLink,
                        QDateTime::currentDateTime().toString("hh:mm:ss"),
                        QDateTime::currentDateTime().toString("hh:mm:ss"));
    } else {
        html = QString("<div style='margin: 10px;'>"
                       "<div style='color: #666; font-size: 10px;'>"
                       "<span style='color: #333; font-weight: bold;'>%1</span> "
                       "<span style='color: #999;'>%2</span>"
                       "</div>"
                       "<div style='background: #F0F0F0; padding: 10px; border-radius: 10px; "
                       "display: inline-block; max-width: 300px; margin-top: 2px; margin-bottom: 5px;'>"
                       "<img src='data:image/png;base64,%3' "
                       "style='max-width: 280px; border-radius: 5px; cursor: pointer;'/><br>"
                       "<div style='color: #666; font-size: 10px; margin-top: 5px;'>"
                       "🖼️ %4<br>"
                       "⏰ %5<br>"
                       "💾 点击图片查看大图"
                       "</div>"
                       "</div>"
                       "<div style='color: #999; font-size: 9px;'>接收时间: %6</div>"
                       "</div>")
                   .arg(sender, time, base64Image, downloadLink,
                        QDateTime::currentDateTime().toString("hh:mm:ss"),
                        QDateTime::currentDateTime().toString("hh:mm:ss"));
    }

    QTextCursor cursor(ui->chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    QScrollBar *scrollbar = ui->chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void Widget::appendSystemMessage(const QString &message)
{
    QString html = QString("<div style='text-align: center; margin: 10px;'>"
                           "<span style='color: #888; font-size: 11px; "
                           "background: #F8F8F8; padding: 5px 10px; border-radius: 10px; "
                           "display: inline-block;'>"
                           "ⓘ %1<br>"
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

    // 处理服务器消息
    if (message.startsWith("[System]")) {
        QString systemMsg = message.mid(9);
        appendSystemMessage(systemMsg);

        if (systemMsg.contains("加入了聊天室") || systemMsg.contains("离开了聊天室")) {
            updateUserList();
        }
    } else if (message.startsWith("在线用户")) {
        ui->userList->clear();
        QString userListStr = message.mid(message.indexOf(":") + 1);
        QStringList users = userListStr.split(",", Qt::SkipEmptyParts);

        for (const QString &user : users) {
            QString trimmedUser = user.trimmed();
            if (!trimmedUser.isEmpty()) {
                QListWidgetItem *item = new QListWidgetItem(trimmedUser);
                // item->setIcon(QIcon(":/icons/user.png"));
                ui->userList->addItem(item);
            }
        }
    } else {
        QString pattern = "\\[(\\d{1,2}:\\d{2})\\] (\\w+): (.+)";
        QRegularExpression re(pattern);
        QRegularExpressionMatch match = re.match(message);

        if (match.hasMatch()) {
            QString time = match.captured(1);
            QString sender = match.captured(2);
            QString content = match.captured(3);

            appendMessage(sender, content, sender == username);
        } else {
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
