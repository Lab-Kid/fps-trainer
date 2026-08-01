#include <QCoreApplication>
#include "server.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    server server;
    if(!server.startServer(12345)){
        qDebug()<<"服务器启动失败，端口被占用？";
        return 1;
    }
    qDebug()<<"服务器已启动，监听端口12345";

    return a.exec();
}
