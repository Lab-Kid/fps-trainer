#ifndef LOGINWIDGET_H
#define LOGINWIDGET_H

#include <QWidget>
#include <QTcpSocket>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class loginwidget:public QWidget
{
    Q_OBJECT
public:
    explicit loginwidget(QWidget *parent=nullptr);
    ~loginwidget();

    QTcpSocket* getSocket()const{return socket;}

signals:
    void loginSuccess(const QString &username);
    void guestLogin();//游客登录

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onReadyRead();
    void onGuestClicked();

private:
    QTcpSocket *socket;
    QByteArray m_buffer;
    QLineEdit *usernameEdit;
    QLineEdit *passwordEdit;
    QPushButton *loginBtn;
    QPushButton *registerBtn;
    QLabel *statusLabel;

    void sendRequest(const QJsonObject &obj);
    void handleResponse(const QByteArray &data);
};

#endif // LOGINWIDGET_H
