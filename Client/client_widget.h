#ifndef CLIENT_WIDGET_H
#define CLIENT_WIDGET_H

#include <QWidget>
#include <QPainter>
#include <QStyleOption>
#include <QDateTime>
#include <QTimer>
#include <QTcpSocket>
#include <QListView>
#include <QStringListModel>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressBar>
#include <QLabel>

#define MAX_NAME 64
#define MAX_FILENAME 256

QT_BEGIN_NAMESPACE
namespace Ui
{
class client_widget;
}
QT_END_NAMESPACE

enum MSG_type
{
    REGISTER,
    REGISTER_success,
    REGISTER_failed,
    LOGIN,
    LOGIN_success,
    LOGIN_failed,

    INITIAL,
    JOIN,
    EXIT,
    GROUP_MSG,
    FILE_MSG,
    FILE_DATA,
    FILE_END,
};
struct MSG_header
{
    char sender_name[MAX_NAME];
    MSG_type Type;
    size_t length;
};

struct FileInfo
{
    char filename[MAX_FILENAME];
    size_t file_size;
};

enum class ReadState
{
    ReadingHeader,
    ReadingBody
};

struct MessageBuffer
{
    ReadState state = ReadState::ReadingHeader;
    MSG_header current_header;
    QByteArray buffer; // 累积 socket 缓冲区数据
};


// 文件传输状态
struct FileTransferState
{
    bool is_receiving = false;
    bool is_sending = false;
    bool abort = false; // 新增：是否取消发送
    QString filename;
    QString sender_name;
    size_t total_size = 0;
    size_t received_bytes = 0;
    size_t sent_bytes = 0;
    QFile* file = nullptr;
    QProgressBar* progress_bar = nullptr;
    QLabel* status_label = nullptr;
    size_t chunks_received = 0;
};

class client_widget : public QWidget
{
    Q_OBJECT

public:
    client_widget(QWidget* parent = nullptr, QTcpSocket* Tcpsocket = nullptr);
    ~client_widget();
    void paintEvent(QPaintEvent*) override;
    void initialize(const QString& username);

private slots:
    void onReadyRead();
    void updateLoginTime();
    void sendMsg();
    void sendFile();
    void onFileTransferTimeout();

private:
    void readMsg();
    void handleFileMsg(const MSG_header& header,const FileInfo& file_info);
    void handleFileData(const MSG_header& header,const QByteArray& data);
    void handleFileEnd(const MSG_header& header);
    void chunkedFileTransfer(QFile& file, const QString& filename);
    void resetFileTransferState();
    void updateFileProgress();
    void showFileTransferDialog(const QString& message);
    void hideFileTransferDialog();
    void dispatchMessage(const MSG_header& header, const QByteArray& body);

    QString user_name;
    QTcpSocket* tcpsocket;
    Ui::client_widget* ui;
    QDateTime logintime;
    QTimer* timer;
    QTimer* file_timeout_timer;

    FileTransferState file_state;
    MessageBuffer msg_buffer;

    // 文件传输UI组件
    QWidget* file_dialog = nullptr;
    QProgressBar* file_progress = nullptr;
    QLabel* file_status = nullptr;
    QPushButton* file_cancel_btn = nullptr;
};

#endif // CLIENT_WIDGET_H
