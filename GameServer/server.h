#ifndef SERVER_H
#define SERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QMap>

class server : public QTcpServer
{
    Q_OBJECT
public:
    explicit server(QObject *parent = nullptr);
    ~server();

    bool startServer(quint16 port);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    QSqlDatabase db;
    QMap<QTcpSocket*, QByteArray> buffer;  // 每个客户端的数据缓冲区

    void handlePacket(QTcpSocket *socket, const QByteArray &data);
    void sendResponse(QTcpSocket *socket, const QByteArray &response);
};

#endif // SERVER_H
