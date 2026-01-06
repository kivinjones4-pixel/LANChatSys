#include "privatechatwindow.h"
#include "widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QDateTime>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QCloseEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QImage>
#include <QUrl>

PrivateChatWindow::PrivateChatWindow(const QString &targetUser,
                                     const QString &myUsername,
                                     QTcpSocket *socket,
                                     Widget *parent)
    : QWidget(parent)
    , targetUser(targetUser)
    , myUsername(myUsername)
    , tcpSocket(socket)
    , parentWidget(parent)
    , unreadCount(0)
    , isActive(true)
{
    setWindowTitle(QString("私聊 - %1").arg(targetUser));
    setMinimumSize(500, 600);

    setupUI();
    setupConnections();

    // 加载历史记录
    loadChatHistory();
}

PrivateChatWindow::~PrivateChatWindow()
{
    saveChatHistory();
}

void PrivateChatWindow::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 标题栏
    titleLabel = new QLabel(QString("与 %1 的私聊").arg(targetUser));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 5px;");
    mainLayout->addWidget(titleLabel);

    // 聊天显示区域
    chatText = new QTextBrowser();
    chatText->setReadOnly(true);
    chatText->setOpenLinks(false);
    chatText->setOpenExternalLinks(false);
    chatText->setAcceptRichText(true);
    mainLayout->addWidget(chatText);

    // 输入区域
    QHBoxLayout *inputLayout = new QHBoxLayout();
    messageInput = new QLineEdit();
    messageInput->setPlaceholderText("输入消息... (按Enter发送)");
    sendButton = new QPushButton("发送");
    uploadButton = new QPushButton("📎");
    uploadButton->setToolTip("上传文件");
    uploadButton->setFixedWidth(40);
    clearButton = new QPushButton("清空");
    clearButton->setFixedWidth(60);

    inputLayout->addWidget(messageInput);
    inputLayout->addWidget(uploadButton);
    inputLayout->addWidget(sendButton);
    inputLayout->addWidget(clearButton);
    mainLayout->addLayout(inputLayout);

    // 上传状态
    uploadProgressBar = new QProgressBar();
    uploadProgressBar->setVisible(false);
    uploadProgressBar->setRange(0, 100);
    uploadStatusLabel = new QLabel("就绪");
    uploadStatusLabel->setAlignment(Qt::AlignCenter);

    QHBoxLayout *statusLayout = new QHBoxLayout();
    statusLayout->addWidget(uploadProgressBar);
    statusLayout->addWidget(uploadStatusLabel);
    mainLayout->addLayout(statusLayout);
}

void PrivateChatWindow::setupConnections()
{
    connect(sendButton, &QPushButton::clicked, this, &PrivateChatWindow::onSendClicked);
    connect(uploadButton, &QPushButton::clicked, this, &PrivateChatWindow::onUploadClicked);
    connect(clearButton, &QPushButton::clicked, this, &PrivateChatWindow::onClearClicked);
    connect(messageInput, &QLineEdit::returnPressed, this, &PrivateChatWindow::onSendClicked);
    connect(chatText, &QTextBrowser::anchorClicked, this, &PrivateChatWindow::handleDownloadRequest);
}

void PrivateChatWindow::onSendClicked()
{
    QString message = messageInput->text().trimmed();
    if (message.isEmpty()) return;

    emit messageSent(message, targetUser);
    messageInput->clear();
}

void PrivateChatWindow::onUploadClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "选择要发送的文件",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "所有文件 (*.*);;图片文件 (*.jpg *.jpeg *.png *.bmp *.gif);;文档文件 (*.pdf *.doc *.docx *.txt)"
        );

    if (!filePath.isEmpty()) {
        emit fileUploadRequested(filePath, targetUser);
    }
}

void PrivateChatWindow::onClearClicked()
{
    chatText->clear();
    appendSystemMessage("聊天记录已清空");
}

void PrivateChatWindow::appendMessage(const QString &sender, const QString &message, bool isSelf)
{
    QString time = getTimestamp();
    QString html;

    if (isSelf) {
        html = QString(
                   "<div style='margin: 5px; text-align: right;'>"
                   "<div style='display: inline-block; max-width: 70%; text-align: left;'>"
                   "<div style='background-color: #049e04; color: white; padding: 8px 12px; "
                   "border-radius: 12px; border-bottom-right-radius: 4px; margin-left: auto; "
                   "word-wrap: break-word;'>%1</div>"
                   "<div style='color: #666; font-size: 10px; margin-top: 2px;'>"
                   "<span style='color:#049e04; font-weight: bold;'>[我]</span> "
                   "<span style='color: #999;'>%2</span>"
                   "</div>"
                   "</div>"
                   "</div>")
                   .arg(message.toHtmlEscaped(), time);
    } else {
        html = QString(
                   "<div style='margin: 5px;'>"
                   "<div style='color: #666; font-size: 10px;'>"
                   "<span style='color: #333; font-weight: bold; margin-right: 5px;'>[%1]</span>"
                   "</div>"
                   "<div style='background-color: #f0f0f0; color: #333; "
                   "padding: 8px 12px; border-radius: 12px; border-bottom-left-radius: 4px; "
                   "display: inline-block; max-width: 70%; margin-top: 2px; "
                   "word-wrap: break-word;'>%2</div>"
                   "<div style='color: #999; font-size: 9px; margin-top: 2px;'>%3</div>"
                   "</div>")
                   .arg(sender, message.toHtmlEscaped(), time);
    }

    QTextCursor cursor(chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);

    QScrollBar *scrollbar = chatText->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());

    // 如果不是自己发送的消息且窗口不活跃，增加未读计数
    if (!isSelf && !isActive) {
        unreadCount++;
        setWindowTitle(QString("私聊 - %1 (%2条未读)").arg(targetUser).arg(unreadCount));
    }
}

void PrivateChatWindow::appendSystemMessage(const QString &message)
{
    QString html = QString(
                       "<div style='text-align: center; margin: 10px;'>"
                       "<span style='color: #888; font-size: 11px; "
                       "padding: 5px 10px; border-radius: 10px; "
                       "background-color: #f5f5f5; display: inline-block;'>"
                       "[系统] %1<br>"
                       "<span style='font-size: 9px; color: #aaa;'>%2</span>"
                       "</span>"
                       "</div>")
                       .arg(message.toHtmlEscaped(), QDateTime::currentDateTime().toString("hh:mm:ss"));

    QTextCursor cursor(chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);
}

void PrivateChatWindow::appendImageMessage(const QString &sender, const QImage &image,
                                           const QString &fileName, const QString &filePath, bool isSelf)
{
    // 缩放图片
    QImage scaledImage = image.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 转换为Base64
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    scaledImage.save(&buffer, "PNG");
    QString base64Image = QString::fromLatin1(byteArray.toBase64().data());

    QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString fileUrl = QUrl::fromLocalFile(filePath).toString();

    QString html;
    if (isSelf) {
        html = QString(
                   "<div style='margin: 10px; text-align: right;'>"
                   "<div style='display: inline-block; max-width: 70%; text-align: left;'>"
                   "<div style='background-color: #049e04; padding: 8px; "
                   "border-radius: 12px; border-bottom-right-radius: 4px; margin-left: auto; "
                   "word-wrap: break-word; max-width: 300px;'>"
                   "<a href='%1' style='text-decoration: none;'>"
                   "<img src='data:image/png;base64,%2' "
                   "style='max-width: 280px; border-radius: 5px; cursor: pointer; display: block;'/>"
                   "</a>"
                   "<div style='color: white; font-size: 10px; margin-top: 5px; opacity: 0.9;'>"
                   "🖼️ %3<br>"
                   "💾 点击图片查看"
                   "</div>"
                   "</div>"
                   "<div style='color: #666; font-size: 10px; margin-top: 2px;'>"
                   "<span style='color:#049e04; font-weight: bold;'>[我]</span> "
                   "<span style='color: #999;'>%4</span>"
                   "</div>"
                   "</div>"
                   "</div>")
                   .arg(fileUrl, base64Image, fileName.toHtmlEscaped(), currentTime);
    } else {
        html = QString(
                   "<div style='margin: 10px;'>"
                   "<div style='color: #666; font-size: 10px;'>"
                   "<span style='color: #333; font-weight: bold;'>[%1]</span> "
                   "</div>"
                   "<div style='background-color: #f0f0f0; padding: 8px; "
                   "border-radius: 12px; border-bottom-left-radius: 4px; "
                   "display: inline-block; max-width: 300px; margin-top: 2px; margin-bottom: 5px;'>"
                   "<a href='%2' style='text-decoration: none;'>"
                   "<img src='data:image/png;base64,%3' "
                   "style='max-width: 280px; border-radius: 5px; cursor: pointer; display: block;'/>"
                   "</a>"
                   "<div style='color: #333; font-size: 10px; margin-top: 5px;'>"
                   "🖼️ %4<br>"
                   "💾 点击图片查看"
                   "</div>"
                   "</div>"
                   "<div style='color: #999; font-size: 9px;'>接收时间: %5</div>"
                   "</div>")
                   .arg(sender, fileUrl, base64Image, fileName.toHtmlEscaped(), currentTime);
    }

    QTextCursor cursor(chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);
}

void PrivateChatWindow::appendFileMessage(const QString &sender, const QString &fileName,
                                          qint64 fileSize, const QString &filePath, bool isSelf)
{
    QString sizeStr = formatFileSize(fileSize);
    QString fileUrl = QUrl::fromLocalFile(filePath).toString();
    QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");

    QString html;
    if (isSelf) {
        html = QString(
                   "<div style='margin: 5px; text-align: right;'>"
                   "<div style='display: inline-block; max-width: 70%; text-align: left;'>"
                   "<div style='background-color: #049e04; color: white; padding: 8px 12px; "
                   "border-radius: 12px; border-bottom-right-radius: 4px; margin-left: auto; "
                   "word-wrap: break-word; max-width: 300px;'>"
                   "<div style='font-size: 16px; margin-bottom: 5px;'>📎</div>"
                   "<div style='font-weight: bold; font-size: 12px;'>%1</div>"
                   "<div style='font-size: 11px; opacity: 0.9; margin-top: 5px;'>"
                   "📏 大小: %2<br>"
                   "<a href='%3' style='color: white; text-decoration: underline;'>💾 点击下载</a>"
                   "</div>"
                   "</div>"
                   "<div style='color: #666; font-size: 10px; margin-top: 2px;'>"
                   "<span style='color:#049e04; font-weight: bold;'>[我]</span> "
                   "<span style='color: #999;'>%4</span>"
                   "</div>"
                   "</div>"
                   "</div>")
                   .arg(fileName.toHtmlEscaped(), sizeStr, fileUrl, currentTime);
    } else {
        html = QString(
                   "<div style='margin: 5px;'>"
                   "<div style='color: #666; font-size: 10px;'>"
                   "<span style='color: #333; font-weight: bold; margin-right: 5px;'>[%1]</span>"
                   "</div>"
                   "<div style='background-color: #f0f0f0; color: #333; padding: 8px 12px; "
                   "border-radius: 12px; border-bottom-left-radius: 4px; "
                   "display: inline-block; max-width: 300px; margin-top: 2px; "
                   "word-wrap: break-word;'>"
                   "<div style='font-size: 16px; margin-bottom: 5px;'>📎</div>"
                   "<div style='font-weight: bold; font-size: 12px;'>%2</div>"
                   "<div style='font-size: 11px; opacity: 0.9; margin-top: 5px;'>"
                   "📏 大小: %3<br>"
                   "<a href='%4' style='color: #007AFF; text-decoration: underline;'>💾 点击下载</a>"
                   "</div>"
                   "</div>"
                   "<div style='color: #999; font-size: 9px; margin-top: 2px;'>%5</div>"
                   "</div>")
                   .arg(sender, fileName.toHtmlEscaped(), sizeStr, fileUrl, currentTime);
    }

    QTextCursor cursor(chatText->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);
}

void PrivateChatWindow::handleDownloadRequest(const QUrl &url)
{
    if (url.scheme() == "file") {
        QString filePath = url.toLocalFile();
        QFileInfo fileInfo(filePath);

        if (fileInfo.exists()) {
            QDesktopServices::openUrl(url);
        } else {
            QMessageBox::warning(this, "文件不存在", "文件不存在或已被移动: " + filePath);
        }
    } else {
        QDesktopServices::openUrl(url);
    }
}

void PrivateChatWindow::markAsRead()
{
    unreadCount = 0;
    setWindowTitle(QString("私聊 - %1").arg(targetUser));
    isActive = true;
}

void PrivateChatWindow::saveChatHistory()
{
    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/LANChat/History/";
    QDir().mkpath(saveDir);

    QString fileName = QString("%1_%2.html").arg(myUsername).arg(targetUser);
    QString filePath = saveDir + fileName;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << chatText->toHtml();
        file.close();
    }
}

void PrivateChatWindow::loadChatHistory()
{
    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/LANChat/History/";
    QString fileName = QString("%1_%2.html").arg(myUsername).arg(targetUser);
    QString filePath = saveDir + fileName;

    QFile file(filePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString html = in.readAll();
        chatText->setHtml(html);
        file.close();
    }
}

void PrivateChatWindow::closeEvent(QCloseEvent *event)
{
    emit windowClosed(targetUser);
    event->accept();
}

QString PrivateChatWindow::getTimestamp()
{
    return QTime::currentTime().toString("hh:mm");
}

QString PrivateChatWindow::formatFileSize(qint64 bytes)
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
