/********************************************************************************
** Form generated from reading UI file 'log_in.ui'
**
** Created by: Qt User Interface Compiler version 6.8.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_LOG_IN_H
#define UI_LOG_IN_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_log_in
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *titleLabel;
    QLineEdit *usernameEdit;
    QLineEdit *passwordEdit;
    QLabel *statusLabel;
    QHBoxLayout *horizontalLayout;
    QPushButton *loginButton;
    QPushButton *registerButton;

    void setupUi(QWidget *log_in)
    {
        if (log_in->objectName().isEmpty())
            log_in->setObjectName("log_in");
        log_in->resize(360, 260);
        log_in->setStyleSheet(QString::fromUtf8("\n"
"    QWidget {\n"
"        background-color: #f0f0f0;\n"
"    }\n"
"    QLineEdit {\n"
"        border: 1px solid #aaa;\n"
"        border-radius: 5px;\n"
"        padding: 4px;\n"
"        min-height: 30px;\n"
"        font-size: 10pt;\n"
"    }\n"
"    QPushButton {\n"
"        background-color: #0078d7;\n"
"        color: white;\n"
"        border-radius: 4px;\n"
"        padding: 6px 12px;\n"
"        font-size: 10pt;\n"
"    }\n"
"    QPushButton:hover {\n"
"        background-color: #005999;\n"
"    }\n"
"    QLabel {\n"
"        color: #555;\n"
"        font-size: 9pt;\n"
"    }\n"
"    QLabel#titleLabel {\n"
"        font-size: 16pt;\n"
"        font-weight: bold;\n"
"        color: #333;\n"
"    }\n"
"   "));
        verticalLayout = new QVBoxLayout(log_in);
        verticalLayout->setSpacing(12);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(20, 20, 20, 20);
        titleLabel = new QLabel(log_in);
        titleLabel->setObjectName("titleLabel");
        titleLabel->setAlignment(Qt::AlignmentFlag::AlignCenter);

        verticalLayout->addWidget(titleLabel);

        usernameEdit = new QLineEdit(log_in);
        usernameEdit->setObjectName("usernameEdit");
        usernameEdit->setStyleSheet(QString::fromUtf8("color: rgb(0, 0, 0);"));

        verticalLayout->addWidget(usernameEdit);

        passwordEdit = new QLineEdit(log_in);
        passwordEdit->setObjectName("passwordEdit");
        passwordEdit->setStyleSheet(QString::fromUtf8("color: rgb(0, 0, 0);"));
        passwordEdit->setEchoMode(QLineEdit::EchoMode::Password);

        verticalLayout->addWidget(passwordEdit);

        statusLabel = new QLabel(log_in);
        statusLabel->setObjectName("statusLabel");
        QFont font;
        font.setPointSize(9);
        font.setItalic(true);
        statusLabel->setFont(font);
        statusLabel->setAlignment(Qt::AlignmentFlag::AlignCenter);

        verticalLayout->addWidget(statusLabel);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(15);
        horizontalLayout->setObjectName("horizontalLayout");
        loginButton = new QPushButton(log_in);
        loginButton->setObjectName("loginButton");

        horizontalLayout->addWidget(loginButton);

        registerButton = new QPushButton(log_in);
        registerButton->setObjectName("registerButton");

        horizontalLayout->addWidget(registerButton);


        verticalLayout->addLayout(horizontalLayout);


        retranslateUi(log_in);

        QMetaObject::connectSlotsByName(log_in);
    } // setupUi

    void retranslateUi(QWidget *log_in)
    {
        log_in->setWindowTitle(QCoreApplication::translate("log_in", "\347\224\250\346\210\267\347\231\273\345\275\225", nullptr));
        titleLabel->setText(QCoreApplication::translate("log_in", "\346\254\242\350\277\216\347\231\273\345\275\225 ChatRoom", nullptr));
        usernameEdit->setPlaceholderText(QCoreApplication::translate("log_in", "\347\224\250\346\210\267\345\220\215", nullptr));
        passwordEdit->setPlaceholderText(QCoreApplication::translate("log_in", "\345\257\206\347\240\201", nullptr));
        statusLabel->setText(QString());
        loginButton->setText(QCoreApplication::translate("log_in", "\347\231\273\345\275\225", nullptr));
        registerButton->setText(QCoreApplication::translate("log_in", "\346\263\250\345\206\214", nullptr));
    } // retranslateUi

};

namespace Ui {
    class log_in: public Ui_log_in {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_LOG_IN_H
