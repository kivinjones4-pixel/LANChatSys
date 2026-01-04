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
    , totalFileSize(0)
    , currentUpload(nullptr)
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

    // 定期清理 QTextBrowser 状态
    QTimer *cleanTimer = new QTimer(this);
    connect(cleanTimer, &QTimer::timeout, this, &Widget::cleanTextBrowser);
    cleanTimer->start(5000);  // 每5秒清理一次

    QTimer::singleShot(100, this, &Widget::startAutoConnect);
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
    // 连接QTextBrowser的锚点点击信号
    connect(ui->chatText, &QTextBrowser::anchorClicked, this, &Widget::handleDownloadRequest);

    // 额外连接：捕获QTextBrowser的链接激活信号（如果有的话）
    connect(ui->chatText, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        // 阻止所有默认行为
        ui->chatText->setTextInteractionFlags(Qt::TextSelectableByMouse);

        // 手动处理链接
        handleDownloadRequest(url);
    });
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
    // 设置标志位防止QTextBrowser处理
    isHandlingDownload = true;

    // 延迟处理，确保事件循环完成
    QTimer::singleShot(0, this, [this, url]() {
        if (url.scheme() == "file") {
            QString filePath = url.toLocalFile();
            QFileInfo fileInfo(filePath);

            if (fileInfo.exists()) {
                QMessageBox msgBox;
                msgBox.setWindowTitle("文件操作");
                msgBox.setText(QString("文件: %1").arg(fileInfo.fileName()));
                msgBox.setInformativeText("你想要打开文件还是打开所在文件夹？");

                QPushButton *openButton = msgBox.addButton("打开文件", QMessageBox::ActionRole);
                QPushButton *openFolderButton = msgBox.addButton("打开文件夹", QMessageBox::ActionRole);
                QPushButton *cancelButton = msgBox.addButton("取消", QMessageBox::RejectRole);

                msgBox.exec();

                if (msgBox.clickedButton() == openButton) {
                    // 使用系统默认程序打开文件
                    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
                } else if (msgBox.clickedButton() == openFolderButton) {
                    // 打开文件所在文件夹
                    QString folderPath = QFileInfo(filePath).absolutePath();
                    QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
                }
            } else {
                QMessageBox::warning(this, "文件不存在", "文件不存在或已被移动: " + filePath);
            }
        } else {
            // 对于其他URL，直接打开
            QDesktopServices::openUrl(url);
        }

        // 重置标志位
        isHandlingDownload = false;

        // 强制刷新QTextBrowser，清除可能残留的内容
        ui->chatText->document()->clearUndoRedoStacks();
        ui->chatText->document()->setModified(false);
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

    // 清空用户列表
    ui->userList->clear();

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
        // 只处理文本消息
        QString content = jsonObj["content"].toString();
        QString timestamp = jsonObj["timestamp"].toString();

        // 显示文本消息
        appendMessage(sender, content, sender == username);
    }
    else if (type == "file_base64" || type == "image_base64") {
        // 处理文件/图片消息，但不显示文件内容
        QString fileName = jsonObj["filename"].toString();
        qint64 fileSize = jsonObj["filesize"].toString().toLongLong();
        QString base64Data = jsonObj["filedata"].toString();

        // 解码Base64数据
        QByteArray fileData = QByteArray::fromBase64(base64Data.toUtf8());

        // 保存文件（但不显示二进制内容）
        QString savePath = saveBase64File(fileName, fileData, type == "image_base64");

        if (!savePath.isEmpty()) {
            // 创建文件消息（只显示文件名和下载链接，不显示文件内容）
            QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");

            QString html;
            if (type == "image_base64") {
                QImage image;
                if (image.loadFromData(fileData)) {
                    // 只显示缩略图和下载链接
                    appendImageMessage(sender, image, fileName, savePath, sender == username);
                }
            } else {
                // 只显示文件信息和下载链接
                appendFileMessage(sender, fileName, fileSize, savePath, sender == username);
            }
        }
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
        html = QString(
                       "<br/>"
                       "<div style='margin: 5px;'>"
                       "<div style='color: #666; font-size: 10px;'>"
                       "<span style='color: #333; font-weight: bold; margin-right: 5px;'>[%1]:</span>"
                       "<span style='color: black; padding: 4px 8px; "
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

    // 创建文件URL链接
    QString fileUrl = QUrl::fromLocalFile(filePath).toString();
    QString downloadLink = QString("<a href='%1' style='color: #007AFF; text-decoration: none;'>💾 点击下载</a>")
                               .arg(fileUrl);

    QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");

    // 使用简单的HTML，不包含复杂的JavaScript
    if (isSelf) {
        html = QString(
                   "<br/>"
                   "<div style='margin: 5px;'>"
                   "<div style='color: #666; font-size: 10px;'>"
                   "<span style='color: #0ba50b; font-weight: bold;'>[我]</span>"
                   "</div>"
                   "<div style='color: #049e04; padding: 12px 15px; "
                   "border-radius: 10px; display: inline-block; max-width: 300px; "
                   "margin-top: 2px; margin-bottom: 5px;'>"
                   "<div style='font-size: 16px; margin-bottom: 5px;'>%1</div>"
                   "<div style='font-weight: bold; font-size: 12px;'>%2</div>"
                   "<div style='font-size: 11px; opacity: 0.9; margin-top: 5px;'>"
                   "📏 大小: %3<br>"
                   "%4"
                   "</div>"
                   "</div>"
                   "<div style='color: #999; font-size: 9px;'>发送时间: %5</div>"
                   "</div>")
                   .arg(fileIcon, fileName.toHtmlEscaped(), sizeStr,
                        downloadLink, // 使用下载链接
                        currentTime); // 修复：使用单独的变量
    } else {
        html = QString(
                       "<br/>"
                       "<div style='margin: 5px;'>"
                       "<div style='color: #666; font-size: 10px;'>"
                       "<span style='color: #333; font-weight: bold; margin-right: 5px;'>[%1]</span>"
                       "</div>"
                       "<div style='color: #333; padding: 12px 15px; "
                       "border-radius: 10px; display: inline-block; max-width: 300px; "
                       "margin-top: 2px; margin-bottom: 5px;'>"
                       "<div style='font-size: 16px; margin-bottom: 5px;'>%2</div>"
                       "<div style='font-weight: bold; font-size: 12px;'>%3</div>"
                       "<div style='font-size: 11px; opacity: 0.9; margin-top: 5px;'>"
                       "📏 大小: %4<br>"
                       "%5"
                       "</div>"
                       "</div>"
                       "<div style='color: #999; font-size: 9px; margin-top: 2px;'>发送时间: %6</div>"
                       "</div>")
                   .arg(sender, fileIcon, fileName.toHtmlEscaped(), sizeStr,
                        downloadLink, // 使用下载链接
                        currentTime); // 修复：使用单独的变量
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

    QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss"); // 修复：正确的时间格式
    QString html;

    // 创建文件URL链接
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
                   "<a href='%1' style='text-decoration: none;'>"
                   "<img src='data:image/png;base64,%2' "
                   "style='max-width: 280px; border-radius: 5px; cursor: pointer;'/>"
                   "</a><br>"
                   "<div style='color:#049e04; font-size: 10px; margin-top: 5px;'>"
                   "🖼️ %3 "
                   "💾 点击图片查看"
                   "</div>"
                   "</div>"
                   "<div style='color: #999; font-size: 9px;'>发送时间: %4</div>"
                   "</div>")
                   .arg(fileUrl, base64Image, downloadLink, currentTime); // 修复：正确的参数数量
    } else {
        html = QString(
                       "<br/>"
                       "<div style='margin: 10px;'>"
                       "<div style='color: #666; font-size: 10px;'>"
                       "<span style='color: #333; font-weight: bold;'>[%1]</span> "
                       "</div>"
                       "<div style=' padding: 10px; border-radius: 10px; "
                       "display: inline-block; max-width: 300px; margin-top: 2px; margin-bottom: 5px;'>"
                       "<a href='%2' style='text-decoration: none;'>"
                       "<img src='data:image/png;base64,%3' "
                       "style='max-width: 280px; border-radius: 5px; cursor: pointer;'/>"
                       "</a><br>"
                       "<div style='color: #666; font-size: 10px; margin-top: 5px;'>"
                       "🖼️ %4<br>"
                       "💾 点击图片查看"
                       "</div>"
                       "</div>"
                       "<div style='color: #999; font-size: 9px;'>接收时间: %5</div>"
                       "</div>")
                   .arg(sender, fileUrl, base64Image, downloadLink, currentTime); // 修复：正确的参数
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

        // 更新用户列表
        if (systemMsg.contains("加入了") || systemMsg.contains("离开了")) {
            updateUserList();
        }
    }
    // 处理用户列表消息
    else if (message.contains("在线用户")) {
        ui->userList->clear();
        QStringList parts = message.split(":");
        if (parts.size() > 1) {
            QString userListStr = parts[1].trimmed();
            QStringList users = userListStr.split(",", Qt::SkipEmptyParts);

            for (const QString &user : users) {
                QString trimmedUser = user.trimmed();
                if (!trimmedUser.isEmpty()) {
                    QListWidgetItem *item = new QListWidgetItem(trimmedUser);
                    ui->userList->addItem(item);
                }
            }
        }
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
void Widget::sendFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "无法打开文件");
        return;
    }

    QByteArray fileData = file.readAll();
    file.close();

    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName();
    qint64 fileSize = fileInfo.size();
    FileType fileType = getFileType(filePath);

    // 转换为Base64
    QString base64Data = fileData.toBase64();

    // 限制文件大小（例如10MB）
    if (fileSize > 10 * 1024 * 1024) {
        QMessageBox::warning(this, "文件太大", "文件大小超过10MB限制");
        return;
    }

    // 保存到自己本地的LANChat目录
    QString savePath = saveBase64File(fileName, fileData, fileType == Image);
    if (savePath.isEmpty()) {
        QMessageBox::warning(this, "错误", "无法保存文件到本地");
        return;
    }

    // 构建JSON格式的消息
    QJsonObject fileJson;
    fileJson["type"] = fileType == Image ? "image_base64" : "file_base64";
    fileJson["sender"] = username;
    fileJson["filename"] = fileName;
    fileJson["filesize"] = QString::number(fileSize);
    fileJson["filedata"] = base64Data;

    if (fileType == Image) {
        QImage image(filePath);
        if (!image.isNull()) {
            // 获取图片尺寸信息
            fileJson["width"] = image.width();
            fileJson["height"] = image.height();

            // 显示在聊天窗口（不等待服务器返回）
            // 使用保存到本地目录的路径，而不是原始路径
            appendImageMessage(username, image, fileName, savePath, true);
        }
    } else {
        // 显示文件消息（不等待服务器返回）
        // 使用保存到本地目录的路径，而不是原始路径
        appendFileMessage(username, fileName, fileSize, savePath, true);
    }

    // 发送JSON消息
    QJsonDocument doc(fileJson);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    tcpSocket->write(jsonString.toUtf8() + "\n");

    // 显示上传状态
    ui->uploadProgressBar->setVisible(true);
    ui->uploadProgressBar->setValue(100);
    ui->uploadStatusLabel->setText(QString("已上传: %1").arg(fileName));

    // 2秒后隐藏进度条
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

    // 发送JSON格式的文本消息
    QJsonObject msgJson;
    msgJson["type"] = "text";
    msgJson["sender"] = username;
    msgJson["content"] = message;
    msgJson["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    QJsonDocument doc(msgJson);
    QString jsonString = doc.toJson(QJsonDocument::Compact);
    tcpSocket->write(jsonString.toUtf8() + "\n");

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
