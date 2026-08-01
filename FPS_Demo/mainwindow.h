#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QTcpSocket>
#include "gamewidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QString getCurrentUsername() const { return currentUsername; }
    void setCurrentUsername(const QString &name);
    void sendToServer(const QJsonObject &obj);
    void fetchUserStats();
    void setSocket(QTcpSocket *socket);

private slots:
    void startGameWithMode(GameMode mode);

    void onServerRead();
    void onUploadStats(const QString &username,float bestSurvival,float bestSpeedrun,int totalKills);

private:
    QString currentUsername;

    QTcpSocket *serverSocket=nullptr;
    QByteArray recvBuffer;
    void handleServerResponse(const QByteArray &data);

    Ui::MainWindow *ui;
    QStackedWidget *stackedWidget;
    GameWidget *gameWidget;//游戏界面（3D）
    QWidget *lobbyWidget;//大厅界面

    QSqlDatabase db;
    void initDatabase();
    void showAchievements();
    void requestRanking(const QString &rankType);
    void showRankingDialog(const QJsonArray &rank,const QString &rankType);

    GameMode currentMode;
    QPushButton *practiceBtn;
    QPushButton *survivalBtn;
    QPushButton *speedrunBtn;
    QPushButton *survivalRankBtn;
    QPushButton *speedrunRankBtn;
    QLabel *titleLabel;
    QLabel *recordLabel;
    QLabel *usernameLabel;

    void updateRecords();
    void updateLobbyUI();
};
#endif // MAINWINDOW_H
