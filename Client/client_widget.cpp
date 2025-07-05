#include "client_widget.h"
#include "ui_client_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QFileInfo>

client_widget::client_widget(QWidget *parent, QTcpSocket* Tcpsocket)
    : QWidget(parent)
    , tcpsocket(Tcpsocket)
    , ui(new Ui::client_widget)
{
    ui->setupUi(this);

    connect(ui->msgsender_btn, &QPushButton::clicked, this, &client_widget::sendMsg);
    connect(ui->filesender_btn, &QPushButton::clicked, this, &client_widget::sendFile);
    connect(tcpsocket, &QTcpSocket::readyRead, this, &client_widget::onReadyRead);
    connect(ui->sender_edit, &QLineEdit::returnPressed, this, &client_widget::sendMsg);

    // 文件传输超时定时器
    file_timeout_timer = new QTimer(this);
    file_timeout_timer->setSingleShot(true);
    // 设置定时器为“单次触发”模式，触发一次后自动停止，不会重复自己再次启动开始计时
    file_timeout_timer->setInterval(30000); // 30秒超时
    connect(file_timeout_timer, &QTimer::timeout, this, &client_widget::onFileTransferTimeout);
}

// 保证样式表（QSS）正常生效
void client_widget::paintEvent(QPaintEvent *)
{
    QStyleOption opt;
    opt.initFrom(this);                     // 初始化控件样式选项
    QPainter p(this);                       // 创建绘图设备
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);  // 由当前样式绘制控件背景
}

client_widget::~client_widget()
{
    resetFileTransferState();
    delete ui;
}

void client_widget::initialize(const QString& username)
{
    MSG_header header;
    header.Type = JOIN;
    header.length = 0;
    std::strncpy(header.sender_name, username.toUtf8().constData(), sizeof(header.sender_name));

    qint64 bytesWritten = tcpsocket->write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (bytesWritten == -1)
    {
        QMessageBox::critical(this, tr("错误"), tr("JOIN发送失败: %1").arg(tcpsocket->errorString()));
        return;
    }

    user_name = username;
    ui->Now_user->setText("当前在线用户:" + username);
    logintime = QDateTime::currentDateTime();
    ui->time->setText("登录时间: " + logintime.toString("yyyy-MM-dd HH:mm:ss"));
    timer = new QTimer(this);
    timer->start(1000);
    connect(timer, &QTimer::timeout, this, &client_widget::updateLoginTime);
}

void client_widget::updateLoginTime()
{
    qint64 seconds = logintime.secsTo(QDateTime::currentDateTime());
    ui->timing->setText("已登录时间: " + QString::number(seconds) + " 秒");
}

void client_widget::sendMsg()
{
    QString msg = ui->sender_edit->text();
    if (msg.isEmpty()) return;

    MSG_header header;
    std::strncpy(header.sender_name, user_name.toUtf8().constData(), sizeof(header.sender_name));
    header.Type = GROUP_MSG;
    header.length = msg.toUtf8().size();

    QByteArray data;
    data.append(reinterpret_cast<const char*>(&header), sizeof(header));
    data.append(msg.toUtf8());
    tcpsocket->write(data);

    ui->sender_edit->clear();
    ui->textBrowser->append(user_name + ": " + msg);
}

void client_widget::sendFile()
{
    if (file_state.is_sending)
    {
        QMessageBox::information(this, tr("提示"), tr("正在发送文件，请等待完成"));
        return;
    }

    QString file_path = QFileDialog::getOpenFileName(this, tr("选择文件"), "", tr("所有文件 (*)"));
    if (file_path.isEmpty()) return;

    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this, tr("错误"), tr("无法打开文件: %1").arg(file.errorString()));
        return;
    }

    QFileInfo file_info(file_path);
    QString filename = file_info.fileName();

    // 发送文件开始消息
    MSG_header header;
    // std::strncpy 会拷贝字符串直到遇到 '\0' 或达到最大长度，如果拷贝的字符串长度小于目标缓冲区，会用 '\0' 填充剩余空间
    // 保证目标缓冲区以 '\0' 结尾（只要源字符串长度不超过缓冲区大小）。
    // std::memcpy 是按字节复制固定大小数据，不会自动添加 '\0'，不会检查字符串结束符。
    std::strncpy(header.sender_name, user_name.toUtf8().constData(), sizeof(header.sender_name));
    header.Type = FILE_MSG;
    header.length = sizeof(FileInfo);

    FileInfo file_info_struct;
    std::strncpy(file_info_struct.filename, filename.toUtf8().constData(), sizeof(file_info_struct.filename));
    file_info_struct.file_size = file.size();

    QByteArray start_data;
    start_data.append(reinterpret_cast<const char*>(&header), sizeof(header));
    start_data.append(reinterpret_cast<const char*>(&file_info_struct), sizeof(FileInfo));
    tcpsocket->write(start_data);

    // 初始化发送状态
    file_state.is_sending = true;
    file_state.filename = file_path;
    file_state.total_size = file.size();
    file_state.sent_bytes = 0;

    showFileTransferDialog(QString("正在发送文件: %1").arg(filename));

    ui->textBrowser->append(QString("开始发送文件: %1").arg(filename));

    // 开始分块传输
    chunkedFileTransfer(file, filename);
}


void client_widget::chunkedFileTransfer(QFile& file, const QString& filename)
{
    // 1. 优化分片大小 - 1MB效率更高
    // 现代网络的TCP窗口通常在64KB-1MB范围，1MB能充分利用TCP窗口
    // 以太网MTU是1500字节，1MB分片会被TCP自动分解成合适的网络包
    const int OPTIMAL_CHUNK_SIZE = 1024 * 1024; // 1MB
    // const int OPTIMAL_CHUNK_SIZE = 200 * 1024;

    // 2. 设置socket缓冲区优化为四分片
    tcpsocket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, OPTIMAL_CHUNK_SIZE * 4);

    // 3. 预分配数据包内存，避免频繁分配
    QByteArray data_packet;
    data_packet.reserve(sizeof(MSG_header) + OPTIMAL_CHUNK_SIZE);

    // 4. 批量事件处理，减少processEvents调用
    int chunk_counter = 0;
    const int EVENT_PROCESS_INTERVAL = 8; // 每8个分片处理一次事件

    file_state.abort = false; // 启动传输前初始化

    // file close掉后,file.atEnd()返回true,跳出循环
    while (!file.atEnd())
    {
        if (file_state.abort) // 检测是否用户取消
        {
            ui->textBrowser->append("用户取消了文件发送");
            file.close();
            resetFileTransferState();
            return;
        }

        // 读取分片大小
        QByteArray chunk = file.read(OPTIMAL_CHUNK_SIZE);
        if (chunk.isEmpty()) break;

        // 构造包头
        MSG_header data_header;
        std::strncpy(data_header.sender_name, user_name.toUtf8().constData(), sizeof(data_header.sender_name));
        data_header.Type = FILE_DATA;
        data_header.length = chunk.size();

        // 重用数据包内存
        data_packet.clear();
        data_packet.append(reinterpret_cast<const char*>(&data_header), sizeof(data_header));
        data_packet.append(chunk);

        qint64 bytes_written = tcpsocket->write(data_packet);
        if (bytes_written == -1)
        {
            QMessageBox::critical(this, tr("错误"), tr("文件发送失败: %1").arg(tcpsocket->errorString()));
            file.close();
            resetFileTransferState();
            return;
        }

        file_state.sent_bytes += chunk.size();

        // 流控：防止socket缓冲区过满
        if (tcpsocket->bytesToWrite() > OPTIMAL_CHUNK_SIZE * 2)
        {
            tcpsocket->waitForBytesWritten(50); // 等待50ms让数据发送出去
        }

        // 批量处理事件，减少UI卡顿
        if (++chunk_counter >= EVENT_PROCESS_INTERVAL)
        {
            updateFileProgress();
            QCoreApplication::processEvents();
            chunk_counter = 0;
        }
    }

    // 发送完成后最终更新进度
    updateFileProgress();

    // 发送文件结束消息
    MSG_header end_header;
    std::strncpy(end_header.sender_name, user_name.toUtf8().constData(), sizeof(end_header.sender_name));
    end_header.Type = FILE_END;
    end_header.length = 0;
    tcpsocket->write(reinterpret_cast<const char*>(&end_header), sizeof(end_header));

    file.close();

    if (file_state.abort)
    {
        ui->textBrowser->append("用户取消了文件发送");
    } else
    {
        ui->textBrowser->append(QString("文件发送完成: %1").arg(filename));
        QMessageBox::information(this, tr("成功"), tr("文件发送完成!"));
    }

    resetFileTransferState();
}


void client_widget::onReadyRead()
{
    while (tcpsocket->bytesAvailable() > 0)
    {
        // 假设快速收到多个消息包：
        // 时刻1: 收到数据包1 -> 触发readyRead信号
        // 时刻2: 收到数据包2 (信号处理中)
        // 时刻3: 收到数据包3 (信号处理中)
        // 结果：只会触发一次readyRead信号
        // 确保本次信号触发后,的确将内核缓冲区的所有数据拷贝到应用层缓冲区
        readMsg();
    }
}

void client_widget::readMsg()
{
    // qDebug() << "readMsg触发,有消息传来";

    msg_buffer.buffer.append(tcpsocket->readAll());

    // qDebug() << "readMsg读取数据大小为" << msg_buffer.buffer.size();

    while (true)
    {
        if (msg_buffer.state == ReadState::ReadingHeader)
        {
            if (msg_buffer.buffer.size() < static_cast<int>(sizeof(MSG_header)))
                return; // 等待更多数据

            memcpy(&msg_buffer.current_header, msg_buffer.buffer.constData(), sizeof(MSG_header));
            msg_buffer.buffer.remove(0, sizeof(MSG_header));
            msg_buffer.state = ReadState::ReadingBody;
        }

        if (msg_buffer.state == ReadState::ReadingBody)
        {
            int body_len = msg_buffer.current_header.length;
            if (msg_buffer.buffer.size() < body_len)
                return; // 等待更多数据

            QByteArray body = msg_buffer.buffer.left(body_len);// left用来从字节数组的开头截取指定长度的子数组
            msg_buffer.buffer.remove(0, body_len);

            // 当解析出一个完整的包之后进行派发消息
            dispatchMessage(msg_buffer.current_header, body);

            // 回到读取下一个 header 状态
            msg_buffer.state = ReadState::ReadingHeader;
        }
    }
}

void client_widget::dispatchMessage(const MSG_header& header, const QByteArray& body)
{
    // qDebug() << "dispatchMessage触发,分发消息";
    switch (header.Type)
    {
    case INITIAL:
    {
        QString userList = QString::fromUtf8(body);
        QStringList users = userList.split(',', Qt::SkipEmptyParts);
        // 按逗号分割字符串，生成字符串列表，忽略空串
        ui->user_list->clear();
        for (const QString& user : users)
            ui->user_list->addItem(user.trimmed());// 去除首尾空白:trim--修剪
        break;
    }
    case JOIN:
    {
        ui->textBrowser->append(QString("%1: %2 %3").arg("SERVER", header.sender_name, "加入聊天室."));
        ui->user_list->addItem(header.sender_name);
        break;
    }
    case EXIT:
    {
        ui->textBrowser->append(QString("%1: %2 %3").arg("SERVER", header.sender_name, "退出聊天室."));
        for (int i = 0; i < ui->user_list->count(); ++i)
        {
            QListWidgetItem* item = ui->user_list->item(i);
            if (item->text() == header.sender_name)
            {
                delete ui->user_list->takeItem(i);
                break;
            }
        }
        break;
    }
    case GROUP_MSG:
    {
        QString msg = QString::fromUtf8(body);
        ui->textBrowser->append(QString("%1: %2").arg(header.sender_name, msg));
        break;
    }
    case FILE_MSG:
    {
        FileInfo file_info;
        memcpy(&file_info, body.constData(), sizeof(FileInfo));
        handleFileMsg(header, file_info);
        break;
    }
    case FILE_DATA:
    {
        handleFileData(header, body);
        break;
    }
    case FILE_END:
    {
        handleFileEnd(header);
        break;
    }
    default:
    {
        qDebug() << "未知的消息类型";
        break;
    }
    }
}

void client_widget::handleFileMsg(const MSG_header& header, const FileInfo& file_info)
{
    // 如果正在传输文件，先重置状态
    if (file_state.is_receiving || file_state.is_sending)
    {
        resetFileTransferState();
    }

    // 弹出询问对话框，是否接收文件
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "文件接收",
        tr("%1 向您发送文件 \"%2\" (大小: %3 字节)\n是否接收？")
            .arg(header.sender_name)
            .arg(file_info.filename)
            .arg(file_info.file_size),
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply == QMessageBox::No)
    {
        return;
    }

    // 选择保存路径
    QString save_path = QFileDialog::getSaveFileName(
        this,
        "保存文件",
        file_info.filename,
        "所有文件 (*)"
        );

    if (save_path.isEmpty()) return;

    // 初始化接收状态
    file_state.is_receiving = true;
    file_state.filename = save_path;
    file_state.sender_name = header.sender_name;
    file_state.total_size = file_info.file_size;
    file_state.received_bytes = 0;

    file_state.file = new QFile(save_path);
    if (!file_state.file->open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, tr("错误"), tr("无法创建文件: %1").arg(file_state.file->errorString()));
        resetFileTransferState();
        return;
    }

    // 显示进度对话框
    showFileTransferDialog(tr("正在接收文件: %1").arg(QFileInfo(save_path).fileName()));

    // 启动超时定时器
    file_timeout_timer->start();

    ui->textBrowser->append(QString("开始接收来自 %1 的文件: %2")
                                .arg(header.sender_name)
                                .arg(file_info.filename));
}

void client_widget::handleFileData(const MSG_header& header, const QByteArray& data)
{
    if (!file_state.is_receiving || !file_state.file)
    {
        return;
    }

    // 写入文件数据
    qint64 written = file_state.file->write(data);
    if (written != data.size())
    {
        QMessageBox::critical(this, tr("错误"), tr("写入文件失败"));
        resetFileTransferState();
        return;
    }

    // 更新接收字节数
    file_state.received_bytes += written;
    file_state.chunks_received++;

    // 减少UI更新频率 - 每16个分片更新一次
    if (file_state.chunks_received >= 16 ||
        file_state.received_bytes >= file_state.total_size)
    {
        updateFileProgress();
        file_state.chunks_received = 0;

        // 批量刷新文件缓冲区
        file_state.file->flush();
        // 调用 flush() 可确保所有缓冲区中的内容立即写入文件

        QCoreApplication::processEvents();
    }

    // 重置超时计时器
    file_timeout_timer->start();
}

void client_widget::handleFileEnd(const MSG_header& header)
{
    if (!file_state.is_receiving)
    {
        return;
    }

    // 检查文件大小是否匹配
    if (file_state.received_bytes == file_state.total_size)
    {
        ui->textBrowser->append(QString("文件接收完成: %1 (%2 字节)")
                                    .arg(QFileInfo(file_state.filename).fileName())
                                    .arg(file_state.received_bytes));
        QMessageBox::information(this, tr("成功"), tr("文件接收完成!"));
    }
    else
    {
        ui->textBrowser->append(QString("文件接收不完整: 期望 %1 字节，实际接收 %2 字节")
                                    .arg(file_state.total_size)
                                    .arg(file_state.received_bytes));
        QMessageBox::warning(this, tr("警告"), tr("文件接收不完整"));
    }

    resetFileTransferState();
}

void client_widget::resetFileTransferState()
{
    file_state.abort = true; // 置为 true，通知发送方中断

    if (file_state.file)
    {
        file_state.file->close();
        delete file_state.file;
        file_state.file = nullptr;
        file_state.chunks_received = 0;
    }

    file_state.is_receiving = false;
    file_state.is_sending = false;
    file_state.filename.clear();
    file_state.sender_name.clear();
    file_state.total_size = 0;
    file_state.received_bytes = 0;
    file_state.sent_bytes = 0;

    file_timeout_timer->stop();
    // 该函数处理UI资源
    hideFileTransferDialog();
}

void client_widget::updateFileProgress()
{
    if (!file_progress || !file_status) return;

    size_t current_bytes = file_state.is_receiving ? file_state.received_bytes : file_state.sent_bytes;
    int progress = (int)((double)current_bytes / file_state.total_size * 100);

    file_progress->setValue(progress);
    file_status->setText(QString("%1/%2 字节 (%3%)")
                             .arg(current_bytes)
                             .arg(file_state.total_size)
                             .arg(progress));
}

void client_widget::showFileTransferDialog(const QString& message)
{
    if (file_dialog)
    {
        // 如果文件传输对话框已存在,销毁掉
        hideFileTransferDialog();
    }

    file_dialog = new QWidget(this);
    file_dialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    file_dialog->setWindowTitle("文件传输");
    file_dialog->resize(300, 120);

    QVBoxLayout* layout = new QVBoxLayout(file_dialog);

    file_status = new QLabel(message);
    layout->addWidget(file_status);

    file_progress = new QProgressBar();
    file_progress->setRange(0, 100);
    file_progress->setValue(0);
    layout->addWidget(file_progress);

    file_cancel_btn = new QPushButton("取消");
    connect(file_cancel_btn, &QPushButton::clicked, [this]()
    {
        resetFileTransferState();
    });
    layout->addWidget(file_cancel_btn);

    file_dialog->show();
}

void client_widget::hideFileTransferDialog()
{
    // 处理UI资源
    if (file_dialog)
    {
        file_dialog->close();
        delete file_dialog;
        file_dialog = nullptr;
        file_progress = nullptr;
        file_status = nullptr;
        file_cancel_btn = nullptr;
    }
}

void client_widget::onFileTransferTimeout()
{
    if (file_state.is_receiving)
    {
        QMessageBox::warning(this, tr("超时"), tr("文件接收超时"));
        ui->textBrowser->append("文件接收超时");
    }
    resetFileTransferState();
}
