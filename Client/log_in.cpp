#include "log_in.h"
#include "ui_log_in.h"

#include <QCryptographicHash>

log_in::log_in(QDialog *parent)
    : QDialog(parent)
    , ui(new Ui::log_in)
    , socket(new QTcpSocket(this))
{
    ui->setupUi(this);

    connect(ui->loginButton, &QPushButton::clicked, this, &log_in::onLoginClicked);
    connect(ui->registerButton, &QPushButton::clicked, this, &log_in::onRegisterClicked);
    connect(socket, &QTcpSocket::readyRead, this, &log_in::onSocketReadyRead);

    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &log_in::onSocketError);

    socket->connectToHost("127.0.0.1", 1234);
}

log_in::~log_in()
{
    delete ui;
}


void log_in::onSocketError([[maybe_unused]]QAbstractSocket::SocketError socketError)
{
    showMessage("无法连接至服务器,请尝试重启客户端");
}

void log_in::onLoginClicked()
{
    sendAuthRequest(true);
}

void log_in::onRegisterClicked()
{
    sendAuthRequest(false);
}

QByteArray rsaEncryptEVP(const QByteArray& data, const QString& publicKeyPemPath)
{
    // OpenSSL EVP:envelope--OpenSSL提供的统一加密接口
    // OAEP（Optimal Asymmetric Encryption Padding）最优非对称加密填充
    // 纯粹的RSA加密数据直接使用是不安全的，容易被破解
    // 填充方案通过对原始数据增加随机性和结构，增强安全性。
    // 为明文数据加上随机填充，保证每次加密即使是相同数据，密文也不同


    // 1. 读取 PEM 公钥文件内容:文件数据->QByteArray->BIO->EVP_PKEY
    QFile file(publicKeyPemPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "无法打开公钥文件：" << publicKeyPemPath;
        return {};
    }
    QByteArray pemData = file.readAll();
    file.close();

    // BIO = Basic Input/Output,
    // 它是 OpenSSL 提供的一个I/O 抽象层，用来封装各种数据源和数据目标（如文件、内存缓冲区、套接字、管道等）的读写操作。
    BIO* bio = BIO_new_mem_buf(pemData.data(), pemData.size());
    // 用于公钥数据包装成一个 BIO 对象，方便后续用 BIO 接口读取这段内存数据
    if (!bio)
    {
        qWarning() << "BIO_new_mem_buf 失败";
        return {};
    }

    // EVP_PKEY 是 OpenSSL 抽象的密钥类型，支持 RSA、ECDSA 等多种算法。
    EVP_PKEY* evp_pubkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!evp_pubkey)
    {
        qWarning() << "解析公钥失败：" << ERR_error_string(ERR_get_error(), nullptr);
        return {};
    }

    // 创建一个上下文对象，后续所有基于该密钥的加密操作都通过这个上下文完成。
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(evp_pubkey, nullptr);
    if (!ctx)
    {
        qWarning() << "创建 EVP_PKEY_CTX 失败";
        EVP_PKEY_free(evp_pubkey);
        return {};
    }

    // 初始化上下文为加密模式
    if (EVP_PKEY_encrypt_init(ctx) <= 0)
    {
        qWarning() << "EVP_PKEY_encrypt_init 失败";
        EVP_PKEY_free(evp_pubkey);
        EVP_PKEY_CTX_free(ctx);
        return {};
    }

    // 设置加密方式为RSA,设置填充为 OAEP :保证加密过程对明文做随机填充，抵抗多种密码学攻击
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0)
    {
        qWarning() << "设置 RSA OAEP 填充失败";
        EVP_PKEY_free(evp_pubkey);
        EVP_PKEY_CTX_free(ctx);
        return {};
    }

    // 2. 计算加密输出长度
    // 传入 nullptr 作为输出缓冲，OpenSSL 只返回加密后数据所需的长度，存入 outlen
    size_t outlen = 0;
    if (EVP_PKEY_encrypt(ctx, nullptr, &outlen, reinterpret_cast<const unsigned char*>(data.data()), data.size()) <= 0)
    {
        qWarning() << "计算加密长度失败";
        EVP_PKEY_free(evp_pubkey);
        EVP_PKEY_CTX_free(ctx);
        return {};
    }

    QByteArray encrypted(outlen, 0);

    // 3. 执行加密
    if (EVP_PKEY_encrypt(ctx, reinterpret_cast<unsigned char*>(encrypted.data()), &outlen,
                         reinterpret_cast<const unsigned char*>(data.data()), data.size()) <= 0)
    {
        qWarning() << "EVP_PKEY_encrypt 加密失败：" << ERR_error_string(ERR_get_error(), nullptr);
        EVP_PKEY_free(evp_pubkey);
        EVP_PKEY_CTX_free(ctx);
        return {};
    }

    EVP_PKEY_free(evp_pubkey);
    EVP_PKEY_CTX_free(ctx);

    encrypted.resize(outlen);
    return encrypted;
}


void log_in::sendAuthRequest(bool isLogin)
{
    QString username = ui->usernameEdit->text();
    QString password = ui->passwordEdit->text();

    QRegularExpression usernameRegex("^[A-Za-z][A-Za-z0-9_]{2,19}$");
    QRegularExpression passwordRegex("^[A-Za-z0-9~!@#$%^&*()_+\\-=]{6,32}$");

    if (username.isEmpty() || password.isEmpty())
    {
        showMessage("用户名或密码不能为空");
        return;
    }

    if (!usernameRegex.match(username).hasMatch())
    {
        showMessage("用户名格式错误：需以字母开头，仅可包含字母、数字、下划线，3-20位");
        return;
    }

    if (!passwordRegex.match(password).hasMatch())
    {
        showMessage("密码格式错误：需为6~32位，仅可包含字母、数字及常见符号");
        return;
    }

    // --- 1. 对密码进行 SHA256 哈希 --- 返回256位,32bytes哈希值
    QByteArray passwordHash = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);

    // --- 2. 使用 RSA 公钥加密密码哈希 ---公钥长度256字节2048bits
    QString rsaPublicKeyPem = "public.pem";
    QByteArray encryptedHash = rsaEncryptEVP(passwordHash, rsaPublicKeyPem);
    if (encryptedHash.isEmpty())
    {
        showMessage("加密失败！");
        return;
    }

    // --- 3. 构造 payload（直接赋值加密后的原始二进制数据） ---
    QByteArray payload = encryptedHash;


    // --- 4. 构造 header ---
    MSG_header header{};
    QByteArray nameBytes = username.toUtf8();
    int copyLen = fmin(nameBytes.size(), MAX_NAMEBUFFER - 1);
    memcpy(header.sender_name, nameBytes.constData(), copyLen);
    header.sender_name[copyLen] = '\0';
    header.Type = isLogin ? LOGIN : REGISTER;
    header.length = payload.size();
    // qDebug() << payload.size();

    // --- 5. 合成数据包 ---
    QByteArray finalPacket;
    finalPacket.append(reinterpret_cast<const char*>(&header), sizeof(MSG_header));
    finalPacket.append(payload);

    // --- 6. 发送 ---
    if (socket && socket->state() == QAbstractSocket::ConnectedState)
    {
        socket->write(finalPacket);
        socket->flush();
        showMessage(isLogin ? "登录请求已发送" : "注册请求已发送");
    }
    else
    {
        showMessage("无法连接服务器");
    }
}




 void log_in::onSocketReadyRead()
    {
        // qDebug() << "log_in::onSocketReadyRead triggered";

        QByteArray reply = socket->readAll();

        if (reply.size() < sizeof(MSG_header)) return;
        MSG_header replyheader;
        memcpy(&replyheader, reply.data(), sizeof(MSG_header));

        switch (replyheader.Type)
        {
        case REGISTER_success:
            showMessage("注册成功，请登录");
            break;
        case REGISTER_failed:
            showMessage("注册失败，用户名已存在");
            break;
        case LOGIN_success:
            showMessage("登录成功,即将进入聊天室");

            currentUsername = ui->usernameEdit->text();

            disconnect(socket, &QTcpSocket::readyRead, this, &log_in::onSocketReadyRead);

            accept();  // 关闭登录窗口，返回 QDialog::Accepted
            break;
        case LOGIN_failed:
            showMessage("登录失败，用户名或密码错误");
            break;
        default:
            showMessage("未知服务器回应");
            break;
        }
    }

void log_in::showMessage(const QString& msg)
{
    ui->statusLabel->setText(msg);
}

