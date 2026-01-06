#ifndef PRIVATECHATWINDOW_H
#define PRIVATECHATWINDOW_H

#include <QWidget>
#include <QTcpSocket>
#include <QTextBrowser>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QJsonDocument>
#include <QImage>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QLabel;
class QProgressBar;
QT_END_NAMESPACE

class Widget; // 前向声明

class PrivateChatWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PrivateChatWindow(const QString &targetUser,
                               const QString &myUsername,
                               QTcpSocket *socket,
                               Widget *parent = nullptr);
    ~PrivateChatWindow();

    void appendMessage(const QString &sender, const QString &message, bool isSelf = false);
    void appendSystemMessage(const QString &message);
    void appendImageMessage(const QString &sender, const QImage &image,
                            const QString &fileName, const QString &filePath, bool isSelf = false);
    void appendFileMessage(const QString &sender, const QString &fileName,
                           qint64 fileSize, const QString &filePath, bool isSelf = false);

    QString getTargetUser() const { return targetUser; }
    void markAsRead();
    bool hasUnread() const { return unreadCount > 0; }
    int getUnreadCount() const { return unreadCount; }

    void saveChatHistory();
    void loadChatHistory();

signals:
    void windowClosed(const QString &targetUser);
    void messageSent(const QString &message, const QString &targetUser);
    void fileUploadRequested(const QString &filePath, const QString &targetUser);

private slots:
    void onSendClicked();
    void onUploadClicked();
    void onClearClicked();
    void handleDownloadRequest(const QUrl &url);

private:
    QString targetUser;
    QString myUsername;
    QTcpSocket *tcpSocket;
    Widget *parentWidget;

    // UI组件
    QLabel *titleLabel;
    QTextBrowser *chatText;
    QLineEdit *messageInput;
    QPushButton *sendButton;
    QPushButton *uploadButton;
    QPushButton *clearButton;
    QProgressBar *uploadProgressBar;
    QLabel *uploadStatusLabel;

    int unreadCount;
    bool isActive;

    void setupUI();
    void setupConnections();
    void closeEvent(QCloseEvent *event) override;

    QString getTimestamp();
    QString formatFileSize(qint64 bytes);
};

#endif // PRIVATECHATWINDOW_H
