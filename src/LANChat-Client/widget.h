// widget.h
#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTcpSocket>
#include <QListWidgetItem>
#include <QTimer>
#include <QFile>
#include <QFileDialog>
#include <QDataStream>
#include <QBuffer>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProgressBar>
#include <QLabel>
#include <QLineEdit>
#include <QTextBrowser>
#include <QCheckBox>
#include <QGroupBox>
#include <QListWidget>

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
    void onUploadClicked();

    // 网络事件
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSocketBytesWritten(qint64 bytes);

    // 界面事件
    void onUserListItemClicked(QListWidgetItem *item);
    void onClearChatClicked();
    void onSettingsClicked();

    // 工具函数
    // void updateConnectionStatus();
    void appendMessage(const QString &sender, const QString &message, bool isSelf = false);
    void appendSystemMessage(const QString &message);
    void appendImageMessage(const QString &sender, const QImage &image, const QString &fileName,
                            const QString &filePath, bool isSelf = false);
    void appendFileMessage(const QString &sender, const QString &fileName, qint64 fileSize,
                           const QString &filePath, bool isSelf = false);
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

    // 文件上传相关
    enum FileType {
        Text = 0,
        Image = 1,
        Video = 2,
        Audio = 3,
        Document = 4,
        Other = 5
    };
    enum MessageType {
        MT_TEXT = 0,
        MT_FILE_BASE64 = 1,
        MT_IMAGE_BASE64 = 2
    };
    struct FileTransfer {
        QFile *file;
        QString fileName;
        qint64 fileSize;
        FileType fileType;
        qint64 bytesWritten;
        qint64 bytesTotal;
        bool isSending;
    };

    QList<FileTransfer*> pendingUploads;
    FileTransfer* currentUpload;
    QByteArray uploadBuffer;
    const qint64 CHUNK_SIZE = 64 * 1024; // 64KB chunks

    // 文件接收相关
    qint64 totalFileSize;           // 当前接收文件的总大小
    QString receivedFileName;       // 当前接收的文件名
    FileType receivedFileType;      // 当前接收的文件类型

    // 初始化函数
    void setupUI();
    void setupConnections();
    void setupDefaultValues();

    // 网络函数
    void connectToServer();
    void disconnectFromServer();
    void sendMessage(const QString &message);
    void sendCommand(const QString &command);
    void sendFile(const QString &filePath);
    void sendFileChunk();
    void startNextUpload();
    void cancelUpload();

    // 消息处理
    void processFileHeader(const QByteArray &data);
    void processFileChunk(const QByteArray &data);
    void processImageMessage(const QByteArray &data);
    void processTextMessage(const QString &message);
    void saveReceivedFile(const QByteArray &fileData, const QString &fileName, FileType fileType);

    // 工具函数
    QString getTimestamp();
    QString formatMessage(const QString &rawMessage);
    QString formatFileSize(qint64 bytes);
    FileType getFileType(const QString &filePath);
    QString getFileTypeString(FileType type);
    void saveSettings();
    void loadSettings();
    void showNotification(const QString &title, const QString &message);
    void updateUploadProgress(qint64 bytesWritten, qint64 bytesTotal);
};

#endif // WIDGET_H
