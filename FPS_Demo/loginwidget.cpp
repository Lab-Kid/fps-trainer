#include "loginwidget.h"
#include <QDebug>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>

loginwidget::loginwidget(QWidget *parent):QWidget(parent)
{
    //创建控件
    usernameEdit=new QLineEdit(this);
    usernameEdit->setPlaceholderText("用户名");
    passwordEdit=new QLineEdit(this);
    passwordEdit->setPlaceholderText("密码");
    passwordEdit->setEchoMode(QLineEdit::Password);
    loginBtn = new QPushButton("登录", this);
    registerBtn = new QPushButton("注册", this);
    statusLabel = new QLabel(this);
    statusLabel->setAlignment(Qt::AlignCenter);
    QPushButton *guestBtn = new QPushButton("游客登录", this);
    guestBtn->setStyleSheet("QPushButton { background-color: #757575; color: white; font-size: 16px; border-radius: 8px; padding: 10px; }");
    connect(guestBtn, &QPushButton::clicked, this, &loginwidget::onGuestClicked);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(usernameEdit);
    layout->addWidget(passwordEdit);
    layout->addWidget(loginBtn);
    layout->addWidget(registerBtn);
    layout->addWidget(statusLabel);
    layout->addWidget(guestBtn);

    //连接信号
    connect(loginBtn, &QPushButton::clicked, this, &loginwidget::onLoginClicked);
    connect(registerBtn, &QPushButton::clicked, this, &loginwidget::onRegisterClicked);

    // 初始化 Socket
    socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::readyRead, this, &loginwidget::onReadyRead);
    connect(socket, &QTcpSocket::connected, this, [=]() {
        statusLabel->setText("已连接服务器");
    });
    connect(socket, &QTcpSocket::disconnected, this, [=]() {
        statusLabel->setText("与服务器断开");
    });

    // 连接服务器
    socket->connectToHost("127.0.0.1", 12345);
    if (!socket->waitForConnected(3000)) {
        statusLabel->setText("无法连接到服务器");
    }
}

loginwidget::~loginwidget()
{

}

void loginwidget::onLoginClicked()
{
    QString username = usernameEdit->text().trimmed();
    QString password = passwordEdit->text().trimmed();
    if (username.isEmpty() || password.isEmpty()) {
        statusLabel->setText("用户名和密码不能为空");
        return;
    }

    QJsonObject obj;
    obj["action"] = "LOGIN";
    obj["username"] = username;
    obj["password"] = password;
    sendRequest(obj);
}

void loginwidget::onRegisterClicked()
{
    QString username = usernameEdit->text().trimmed();
    QString password = passwordEdit->text().trimmed();
    if (username.isEmpty() || password.isEmpty()) {
        statusLabel->setText("用户名和密码不能为空");
        return;
    }

    QJsonObject obj;
    obj["action"] = "REGISTER";
    obj["username"] = username;
    obj["password"] = password;
    sendRequest(obj);
}

void loginwidget::sendRequest(const QJsonObject &obj)
{
    if (socket->state() != QAbstractSocket::ConnectedState) {
        statusLabel->setText("未连接到服务器");
        return;
    }
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    socket->write(data + '\n');
    statusLabel->setText("请求发送中...");
}

void loginwidget::onReadyRead()
{
    m_buffer.append(socket->readAll());
    int index;
    while ((index = m_buffer.indexOf('\n')) != -1) {
        QByteArray packet = m_buffer.left(index);
        m_buffer.remove(0, index + 1);
        handleResponse(packet);
    }
}

void loginwidget::onGuestClicked()
{
    emit guestLogin();
    this->hide();
}

void loginwidget::handleResponse(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        statusLabel->setText("服务器返回无效数据");
        return;
    }
    QJsonObject obj = doc.object();
    QString status = obj.value("status").toString();
    QString message = obj.value("message").toString();
    statusLabel->setText(message);

    if (status == "success") {
        if(message.contains("登录成功")){
            disconnect(socket,&QTcpSocket::readyRead,this,&loginwidget::onReadyRead);
            emit loginSuccess(usernameEdit->text().trimmed());
            //this->close();
            this->hide();
        }else if(message.contains("注册成功")){
            QMessageBox::information(this,"提示：","注册成功，请登录");
        }
    } else {
        QMessageBox::warning(this, "错误", message);
    }
}
