#ifndef LOG_IN_H
#define LOG_IN_H

#include <QDialog>
#include <QTcpSocket>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <QCryptographicHash>
#include <QDataStream>
#include <QRegularExpression>
#include <QDebug>
#include "client_widget.h"

#define MAX_NAMEBUFFER 64

QT_BEGIN_NAMESPACE
namespace Ui { class log_in; }
QT_END_NAMESPACE

class log_in : public QDialog
{
    Q_OBJECT
    // 使得该类具备 Qt 的元对象系统（Meta-Object System）功能

public:
    explicit log_in(QDialog *parent = nullptr);
    ~log_in();

    QTcpSocket* getsocket() const { return socket; }
    QString getname() const { return currentUsername; }

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError socketError);

private:
    void sendAuthRequest(bool isLogin);
    void showMessage(const QString& msg);

    QString currentUsername;
    Ui::log_in *ui;
    QTcpSocket* socket;
};

#endif // LOG_IN_H
