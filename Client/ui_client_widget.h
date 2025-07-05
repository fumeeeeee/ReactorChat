/********************************************************************************
** Form generated from reading UI file 'client_widget.ui'
**
** Created by: Qt User Interface Compiler version 6.8.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_CLIENT_WIDGET_H
#define UI_CLIENT_WIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_client_widget
{
public:
    QPushButton *msgsender_btn;
    QTextBrowser *textBrowser;
    QLineEdit *sender_edit;
    QPushButton *filesender_btn;
    QPushButton *exit_button;
    QListWidget *user_list;
    QWidget *horizontalLayoutWidget;
    QHBoxLayout *horizontalLayout;
    QLabel *time;
    QLabel *timing;
    QLabel *Now_user;
    QLabel *label;

    void setupUi(QWidget *client_widget)
    {
        if (client_widget->objectName().isEmpty())
            client_widget->setObjectName("client_widget");
        client_widget->resize(700, 500);
        client_widget->setMinimumSize(QSize(700, 500));
        client_widget->setMaximumSize(QSize(700, 500));
        client_widget->setStyleSheet(QString::fromUtf8("\n"
"    QWidget {\n"
"        background-color: #e0e0e0;\n"
"    }\n"
"    QPushButton {\n"
"        background-color: rgba(255, 255, 255, 200);\n"
"        color: #000;\n"
"        border: 1px solid #999;\n"
"        border-radius: 6px;\n"
"        font-size: 10pt;\n"
"        padding: 4px;\n"
"    }\n"
"    QPushButton:hover {\n"
"        background-color: #d0e7ff;\n"
"    }\n"
"    QLineEdit, QTextBrowser, QListWidget {\n"
"        background-color: rgba(255, 255, 255, 230);\n"
"        color: #000;\n"
"        border: 1px solid #aaa;\n"
"        border-radius: 6px;\n"
"        font-size: 10pt;\n"
"    }\n"
"    QLabel {\n"
"        color: #000;\n"
"        font-size: 10pt;\n"
"    }\n"
"   "));
        msgsender_btn = new QPushButton(client_widget);
        msgsender_btn->setObjectName("msgsender_btn");
        msgsender_btn->setGeometry(QRect(590, 410, 81, 31));
        textBrowser = new QTextBrowser(client_widget);
        textBrowser->setObjectName("textBrowser");
        textBrowser->setGeometry(QRect(10, 30, 551, 361));
        sender_edit = new QLineEdit(client_widget);
        sender_edit->setObjectName("sender_edit");
        sender_edit->setGeometry(QRect(10, 410, 551, 61));
        sender_edit->setFrame(true);
        sender_edit->setClearButtonEnabled(false);
        filesender_btn = new QPushButton(client_widget);
        filesender_btn->setObjectName("filesender_btn");
        filesender_btn->setGeometry(QRect(590, 440, 81, 31));
        exit_button = new QPushButton(client_widget);
        exit_button->setObjectName("exit_button");
        exit_button->setGeometry(QRect(590, 380, 81, 31));
        exit_button->setStyleSheet(QString::fromUtf8(""));
        user_list = new QListWidget(client_widget);
        user_list->setObjectName("user_list");
        user_list->setGeometry(QRect(580, 30, 101, 341));
        QFont font;
        font.setPointSize(10);
        user_list->setFont(font);
        horizontalLayoutWidget = new QWidget(client_widget);
        horizontalLayoutWidget->setObjectName("horizontalLayoutWidget");
        horizontalLayoutWidget->setGeometry(QRect(0, 0, 421, 31));
        horizontalLayout = new QHBoxLayout(horizontalLayoutWidget);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        time = new QLabel(horizontalLayoutWidget);
        time->setObjectName("time");

        horizontalLayout->addWidget(time);

        timing = new QLabel(horizontalLayoutWidget);
        timing->setObjectName("timing");

        horizontalLayout->addWidget(timing);

        Now_user = new QLabel(client_widget);
        Now_user->setObjectName("Now_user");
        Now_user->setGeometry(QRect(420, 0, 137, 29));
        label = new QLabel(client_widget);
        label->setObjectName("label");
        label->setGeometry(QRect(580, 0, 111, 31));

        retranslateUi(client_widget);
        QObject::connect(exit_button, &QPushButton::clicked, client_widget, qOverload<>(&QWidget::close));

        QMetaObject::connectSlotsByName(client_widget);
    } // setupUi

    void retranslateUi(QWidget *client_widget)
    {
        client_widget->setWindowTitle(QCoreApplication::translate("client_widget", "Online ChatRoom", nullptr));
        msgsender_btn->setText(QCoreApplication::translate("client_widget", "\345\217\221\351\200\201\346\266\210\346\201\257", nullptr));
        filesender_btn->setText(QCoreApplication::translate("client_widget", "\345\217\221\351\200\201\346\226\207\344\273\266", nullptr));
        exit_button->setText(QCoreApplication::translate("client_widget", "\351\200\200\345\207\272\347\231\273\345\275\225", nullptr));
        time->setText(QCoreApplication::translate("client_widget", "\347\231\273\345\205\245\346\227\266\351\227\264:", nullptr));
        timing->setText(QCoreApplication::translate("client_widget", "\345\267\262\347\231\273\345\205\245:", nullptr));
        Now_user->setText(QCoreApplication::translate("client_widget", "\345\275\223\345\211\215\347\224\250\346\210\267:", nullptr));
        label->setText(QCoreApplication::translate("client_widget", "\345\275\223\345\211\215\345\234\250\347\272\277\347\224\250\346\210\267\345\210\227\350\241\250:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class client_widget: public Ui_client_widget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_CLIENT_WIDGET_H
