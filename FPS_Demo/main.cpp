#include "mainwindow.h"
#include "loginwidget.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    //先显示登录窗口
    loginwidget *login=new loginwidget;
    MainWindow mainWin;

    QObject::connect(login,&loginwidget::loginSuccess,[&](const QString &username){
        mainWin.setCurrentUsername(username);
        mainWin.setSocket(login->getSocket());
        login->hide();
        mainWin.fetchUserStats();//拉取服务器上的用户数据
        mainWin.show();
    });

    QObject::connect(login, &loginwidget::guestLogin, [&](){
        mainWin.setCurrentUsername("Guest");
        mainWin.setSocket(nullptr);   // 不设置网络连接
        mainWin.fetchUserStats();     // 内部会检查游客模式跳过
        mainWin.show();
        login->hide();
    });

    login->show();

    return a.exec();
}
