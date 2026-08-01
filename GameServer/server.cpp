#include "server.h"
#include <QDebug>
#include <QDataStream>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

server::server(QObject *parent):QTcpServer(parent)
{
    db= QSqlDatabase::addDatabase("QSQLITE","server_conn");
    db.setDatabaseName("server_data.db");
    if (db.open()) {
        QSqlQuery query(db);
        query.exec("CREATE TABLE IF NOT EXISTS users ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "username TEXT UNIQUE, "
                   "password TEXT, "
                   "best_survival REAL DEFAULT 0, "
                   "best_speedrun REAL DEFAULT 9999, "
                   "total_kills INTEGER DEFAULT 0)");
        query.exec("CREATE TABLE IF NOT EXISTS user_achievements ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "username TEXT, "
                   "achievement_id INTEGER, "
                   "unlocked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
                   "FOREIGN KEY(username) REFERENCES users(username), "
                   "UNIQUE(username, achievement_id))");
        qDebug() << "Server DB ready.";
        // 修复数据：将 best_speedrun 为 0 或负数的改为 9999
        query.exec("UPDATE users SET best_speedrun = 9999 WHERE best_speedrun <= 0");
        qDebug() << "修复数据完成";
    }else{
        qDebug()<<"数据库打开失败："<<db.lastError().text();
    }
}

server::~server()
{
    db.close();
}

bool server::startServer(quint16 port) {
    return listen(QHostAddress::Any, port);
}

void server::incomingConnection(qintptr socketDescriptor) {
    QTcpSocket *socket=new QTcpSocket(this);
    socket->setSocketDescriptor(socketDescriptor);
    connect(socket,&QTcpSocket::readyRead,this,&server::onReadyRead);
    connect(socket,&QTcpSocket::disconnected,this,&server::onDisconnected);
    buffer[socket]=QByteArray();
    qDebug()<<"新客户端连接："<<socket->peerAddress().toString();
}

void server::onReadyRead(){
    QTcpSocket *socket=qobject_cast<QTcpSocket*>(sender());
    if(!socket)return;

    //将数据追加到缓冲区
    buffer[socket].append(socket->readAll());

    //处理完整的包
    int index;
    while((index=buffer[socket].indexOf('\n'))!=-1){
        QByteArray packet = buffer[socket].left(index);
        buffer[socket].remove(0,index+1);
        handlePacket(socket,packet);
    }
}

void server::handlePacket(QTcpSocket *socket,const QByteArray &data){
    qDebug()<<"Raw data(hex):"<<data.toHex();
    qDebug()<<"Raw data(string):"<<data;
    QJsonDocument doc=QJsonDocument::fromJson(data);
    if(doc.isNull()){
        sendResponse(socket,"{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    QJsonObject obj=doc.object();
    QString action=obj.value("action").toString();

    if(action=="REGISTER"){
        QString username=obj.value("username").toString();
        QString password=obj.value("password").toString();
        if(username.isEmpty()||password.isEmpty()){
            sendResponse(socket,"{\"status\":\"error\",\"message\":\"用户名或密码不能为空\"}");
            return;
        }
        QSqlQuery query(db);
        query.prepare("SELECT username FROM users WHERE username = ?");
        query.bindValue(0,username);
        query.exec();
        if(query.next()){
            sendResponse(socket,"{\"status\":\"error\",\"message\":\"用户名已存在\"}");
            return;
        }
        query.prepare("INSERT INTO users (username, password) VALUES (?, ?)");
        query.bindValue(0,username);
        query.bindValue(1,password);
        if(query.exec()){
            sendResponse(socket,"{\"status\":\"success\",\"message\":\"注册成功\"}");
        }else{
            sendResponse(socket,"{\"status\":\"error\",\"message\":\"注册失败\"}");
        }
    }
    else if(action=="LOGIN"){
        QString username=obj.value("username").toString();
        QString password=obj.value("password").toString();
        QSqlQuery query(db);
        query.prepare("SELECT password FROM users WHERE username = ?");
        query.bindValue(0,username);
        query.exec();
        if(query.next()){
            QString dbPass=query.value(0).toString();
            if(dbPass==password){
                sendResponse(socket,"{\"status\":\"success\",\"message\":\"登录成功\"}");
            }else{
                sendResponse(socket,"{\"status\":\"error\",\"message\":\"密码错误\"}");
            }
        }else{
            sendResponse(socket,"{\"status\":\"error\",\"message\":\"用户不存在\"}");
        }
    }
    else if (action == "UPDATE_STATS") {
        QString username = obj.value("username").toString();
        float bestSurvival = obj.value("bestSurvival").toDouble();
        float bestSpeedrun = obj.value("bestSpeedrun").toDouble();
        int totalKills = obj.value("totalKills").toInt();

        if(bestSpeedrun<=0||bestSpeedrun>=9999){
            bestSpeedrun=9999;
        }

        QSqlQuery query(db);
        query.prepare("UPDATE users SET best_survival = ?, best_speedrun = ?, total_kills = ? WHERE username = ?");
        query.bindValue(0, bestSurvival);
        query.bindValue(1, bestSpeedrun);
        query.bindValue(2, totalKills);
        query.bindValue(3, username);
        if (query.exec()) {
            sendResponse(socket, "{\"status\":\"success\",\"message\":\"更新成功\"}");
        } else {
            sendResponse(socket, "{\"status\":\"error\",\"message\":\"更新失败\"}");
        }
    }
    else if (action == "GET_RANK") {
        QString rankType = obj.value("rankType").toString(); // "survival" 或 "speedrun"
        QString orderBy;
        if (rankType == "survival") {
            orderBy = "best_survival DESC";
        } else if (rankType == "speedrun") {
            // 速通按时间升序（越快越好），排除未通关的（9999）
            orderBy = "best_speedrun ASC";
        } else {
            // 默认按生存排序
            orderBy = "best_survival DESC";
        }

        // 速通模式只显示有成绩的（best_speedrun < 9999）
        QString whereClause;
        if (rankType == "speedrun") {
            whereClause = "WHERE best_speedrun > 0 AND best_speedrun < 9999";
        }

        QString sql = QString("SELECT username, best_survival, best_speedrun, total_kills FROM users %1 ORDER BY %2 LIMIT 10")
                          .arg(whereClause)
                          .arg(orderBy);

        QSqlQuery query(db);
        if (!query.exec(sql)) {
            qDebug() << "排行榜查询失败:" << query.lastError().text();
            sendResponse(socket, "{\"status\":\"error\",\"message\":\"数据库查询失败\"}");
            return;
        }

        QJsonArray rankArray;
        while (query.next()) {
            QJsonObject entry;
            entry["username"] = query.value(0).toString();
            entry["bestSurvival"] = query.value(1).toDouble();
            entry["bestSpeedrun"] = query.value(2).toDouble();
            entry["totalKills"] = query.value(3).toInt();
            rankArray.append(entry);
        }

        QJsonObject response;
        response["status"] = "success";
        response["rankType"] = rankType;
        response["rank"] = rankArray;
        sendResponse(socket, QJsonDocument(response).toJson(QJsonDocument::Compact));
    }
    else if (action == "GET_USER_STATS") {
        qDebug()<<"收到GET_USER_STATS请求";
        QString username = obj.value("username").toString();
        qDebug()<<"查询用户名："<<username;

        QSqlQuery query(db);
        query.prepare("SELECT best_survival, best_speedrun, total_kills FROM users WHERE username = ?");
        query.bindValue(0, username);
        if(!query.exec()){
            qDebug()<<"查询执行失败："<<query.lastError().text();
            sendResponse(socket, "{\"status\":\"error\",\"message\":\"数据库查询失败\"}");
            return;
        }
        qDebug() << "查询执行成功，行数:" << query.size();

        if (query.next()) {

            QJsonObject stats;
            stats["bestSurvival"] = query.value(0).toDouble();
            stats["bestSpeedrun"] = query.value(1).toDouble();
            stats["totalKills"] = query.value(2).toInt();

            QSqlQuery achQuery(db);
            achQuery.prepare("SELECT achievement_id FROM user_achievements WHERE username = ?");
            achQuery.bindValue(0, username);
            achQuery.exec();

            QJsonArray unlockedAchievements;
            while (achQuery.next()) {
                unlockedAchievements.append(achQuery.value(0).toInt());
            }
            stats["unlockedAchievements"] = unlockedAchievements; // 将成就列表放入返回数据

            QJsonObject response;
            response["status"] = "success";
            response["userStats"] = stats;
            QByteArray jsonResponse = QJsonDocument(response).toJson(QJsonDocument::Compact);
            qDebug() << "准备返回数据:" << jsonResponse;
            sendResponse(socket, jsonResponse);
            qDebug() << "响应已发送";
        } else {
            qDebug() << "用户名不存在:" << username;
            sendResponse(socket, "{\"status\":\"error\",\"message\":\"用户不存在\"}");
        }
    }
    else if (action == "UNLOCK_ACHIEVEMENT") {
        QString username = obj.value("username").toString();
        int achievementId = obj.value("achievementId").toInt();

        QSqlQuery query(db);
        query.prepare("INSERT OR IGNORE INTO user_achievements (username, achievement_id) VALUES (?, ?)");
        query.bindValue(0, username);
        query.bindValue(1, achievementId);
        if (query.exec()) {
            sendResponse(socket, "{\"status\":\"success\",\"message\":\"成就已同步\"}");
        } else {
            sendResponse(socket, "{\"status\":\"error\",\"message\":\"成就同步失败\"}");
        }
    }
    else {
        sendResponse(socket, "{\"status\":\"error\",\"message\":\"未知操作\"}");
    }
}

void server::sendResponse(QTcpSocket *socket, const QByteArray &response){
    socket->write(response+'\n');
}

void server::onDisconnected(){
    QTcpSocket *socket=qobject_cast<QTcpSocket*>(sender());
    if(socket){
        buffer.remove(socket);
        socket->deleteLater();
        qDebug()<<"客户端断开连接";
    }
}
