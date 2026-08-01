#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDialog>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QJsonArray>
#include <QTableWidget>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //1.创建堆叠窗口（总容器）
    stackedWidget=new QStackedWidget(this);
    setCentralWidget(stackedWidget);
    //2.创建【大厅界面】
    lobbyWidget=new QWidget();
    lobbyWidget->setStyleSheet("background-color:#2b2b2b;");
    QVBoxLayout *lobbyLayout=new QVBoxLayout(lobbyWidget);
    lobbyLayout->setAlignment(Qt::AlignCenter);//整体居中

    titleLabel = new QLabel("*", lobbyWidget);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("color:white;font-size:48px;font-weight:bold;margin-bottom:50px;");

    // ----- 三个核心按钮 -----
    // 1. 练习模式
    practiceBtn = new QPushButton("🎯 练习模式");
    practiceBtn->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; font-size: 22px; border-radius: 15px; }"
        "QPushButton:hover { background-color: #45a049; }"
    );

    // 2. 生存模式
    survivalBtn = new QPushButton("🧟 生存模式");
    survivalBtn->setStyleSheet(
        "QPushButton { background-color: #d32f2f; color: white; font-size: 22px; border-radius: 15px; }"
        "QPushButton:hover { background-color: #b71c1c; }"
    );

    // 3. 速通模式
    speedrunBtn = new QPushButton("⏱️ 速通模式");
    speedrunBtn->setStyleSheet(
        "QPushButton { background-color: #1976d2; color: white; font-size: 22px; border-radius: 15px; }"
        "QPushButton:hover { background-color: #0d47a1; }"
    );

    //成就
    QPushButton *achievementBtn = new QPushButton("🏆 成就");
    achievementBtn->setFixedSize(200, 50);
    achievementBtn->setStyleSheet(
        "QPushButton { background-color: #FF9800; color: white; font-size: 18px; border-radius: 10px; }"
        "QPushButton:hover { background-color: #e68900; }"
    );
    connect(achievementBtn, &QPushButton::clicked, this, &MainWindow::showAchievements);

    recordLabel = new QLabel(lobbyWidget);
    recordLabel->setAlignment(Qt::AlignCenter);
    recordLabel->setStyleSheet("color: white; font-size: 16px; margin-top: 20px;");

    usernameLabel = new QLabel(lobbyWidget);
    usernameLabel->setAlignment(Qt::AlignCenter);
    usernameLabel->setStyleSheet("color: #4CAF50; font-size: 18px; font-weight: bold; margin-top: 10px;");
    usernameLabel->setText("玩家: " + currentUsername);

    //排行榜
    QWidget *rankContainer = new QWidget(lobbyWidget);
    QHBoxLayout *rankLayout = new QHBoxLayout(rankContainer);
    rankLayout->setAlignment(Qt::AlignCenter);
    rankLayout->setSpacing(30);

    // 生存排行榜按钮
    survivalRankBtn = new QPushButton("🌍 生存排行");
    survivalRankBtn->setFixedSize(180, 50);
    survivalRankBtn->setStyleSheet(
        "QPushButton { background-color: #2E7D32; color: white; font-size: 16px; border-radius: 10px; }"
        "QPushButton:hover { background-color: #388E3C; }"
    );
    connect(survivalRankBtn, &QPushButton::clicked, this, [this]() {
        requestRanking("survival");
    });

    // 速通排行榜按钮
    speedrunRankBtn = new QPushButton("🌍 速通排行");
    speedrunRankBtn->setFixedSize(180, 50);
    speedrunRankBtn->setStyleSheet(
        "QPushButton { background-color: #0D47A1; color: white; font-size: 16px; border-radius: 10px; }"
        "QPushButton:hover { background-color: #1565C0; }"
    );
    connect(speedrunRankBtn, &QPushButton::clicked, this, [this]() {
        requestRanking("speedrun");
    });

    initDatabase();
    updateRecords();

    // 绑定点击事件（直接指定模式）
    connect(practiceBtn, &QPushButton::clicked, this, [this]() {
        startGameWithMode(GameMode::Practice);
    });
    connect(survivalBtn, &QPushButton::clicked, this, [this]() {
        startGameWithMode(GameMode::Survival);
    });
    connect(speedrunBtn, &QPushButton::clicked, this, [this]() {
        startGameWithMode(GameMode::Speedrun);
    });

    // 添加到布局
    lobbyLayout->addWidget(titleLabel);
    lobbyLayout->addWidget(practiceBtn);
    lobbyLayout->addWidget(survivalBtn);
    lobbyLayout->addWidget(speedrunBtn);
    lobbyLayout->addWidget(achievementBtn);
    lobbyLayout->addWidget(recordLabel);
    lobbyLayout->addWidget(usernameLabel);

    rankLayout->addWidget(survivalRankBtn);
    rankLayout->addWidget(speedrunRankBtn);
    lobbyLayout->addWidget(rankContainer);

    //3.创建【游戏页面】
    gameWidget = new GameWidget(this);
    gameWidget->setMainWindow(this);

    connect(gameWidget,&GameWidget::uploadStatsSignal,this,&MainWindow::onUploadStats);

    connect (gameWidget,&GameWidget::goBackToLobby,this,[this](){
        stackedWidget->setCurrentIndex(0);
        this->setFocus();
        updateRecords();
    });

    //4.将两个页面加入堆叠
    stackedWidget->addWidget(lobbyWidget);//索引0
    stackedWidget->addWidget(gameWidget);//索引1

    //5.设置堆叠窗口为中央部件
    setWindowTitle("FPS");
    resize(1024,768);
    updateLobbyUI();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setCurrentUsername(const QString &name)
{
    currentUsername=name;
    if(usernameLabel){
        if(name == "Guest") {
            usernameLabel->setText("👤 游客模式");
            usernameLabel->setStyleSheet("color: #FFA726; font-size: 18px; font-weight: bold;");
            // 禁用排行榜按钮
            survivalRankBtn->setEnabled(false);
            survivalRankBtn->setToolTip("游客模式下无法查看排行榜");
            speedrunRankBtn->setEnabled(false);
            speedrunRankBtn->setToolTip("游客模式下无法查看排行榜");
        }else {
            usernameLabel->setText("玩家：" + name);
            usernameLabel->setStyleSheet("color: #4CAF50; font-size: 18px; font-weight: bold;");
            survivalRankBtn->setEnabled(true);
            survivalRankBtn->setToolTip("");
            speedrunRankBtn->setEnabled(true);
            speedrunRankBtn->setToolTip("");
        }
    }
}

//网络功能
void MainWindow::sendToServer(const QJsonObject &obj){
    if(currentUsername=="Guest"){
        qDebug()<<"游客模式，不发送网络请求";
    }
    qDebug()<<"sendToServer被调用";
    if(!serverSocket){
        qDebug()<<"serverSocket为空";
        return;
    }
    qDebug()<<"连接状态："<<serverSocket->state();
    if (serverSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "未连接到服务器，无法发送数据";
        return;
    }
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    qDebug()<<"发送内容："<<data;
    serverSocket->write(data + '\n');
}

void MainWindow::onUploadStats(const QString &username, float bestSurvival, float bestSpeedrun, int totalKills)
{
    qDebug()<<"onUploadStats收到信号";

    // 构建 JSON 请求
    QJsonObject obj;
    obj["action"] = "UPDATE_STATS";
    obj["username"] = username;
    obj["bestSurvival"] = bestSurvival;
    obj["bestSpeedrun"] = (bestSpeedrun == 9999 ? 0 : bestSpeedrun);
    obj["totalKills"] = totalKills;

    // 发送到服务器
    sendToServer(obj);
}

void MainWindow::onServerRead()
{
    QByteArray newData = serverSocket->readAll();
     qDebug() << " onServerRead 收到字节:" << newData.size() << "内容:" << newData;
     recvBuffer.append(newData);
     int index;
     while ((index = recvBuffer.indexOf('\n')) != -1) {
         QByteArray packet = recvBuffer.left(index);
         recvBuffer.remove(0, index + 1);
         handleServerResponse(packet);
     }
}

void MainWindow::handleServerResponse(const QByteArray &data)
{
    // 先简单打印，后续再处理排行榜
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        qDebug() << "无效 JSON 响应";
        return;
    }
    QJsonObject obj = doc.object();
    qDebug() << "服务器响应：" << obj;

    // 处理用户数据拉取
    if (obj.contains("userStats")) {
        QJsonObject stats = obj.value("userStats").toObject();
        float survival = stats.value("bestSurvival").toDouble();
        float speedrun = stats.value("bestSpeedrun").toDouble();
        int kills = stats.value("totalKills").toInt();

        // 更新本地数据库（保持与服务器同步）
        QSqlQuery query(db);
        query.prepare("UPDATE player_records SET best_value = ? WHERE mode = 'survival'");
        query.bindValue(0, survival);
        query.exec();
        query.prepare("UPDATE player_records SET best_value = ? WHERE mode = 'speedrun'");
        query.bindValue(0, speedrun);
        query.exec();

        QJsonArray unlockedList = stats.value("unlockedAchievements").toArray();
        if (!unlockedList.isEmpty()) {
            // 先将本地所有成就重置为未解锁（防止脏数据）
            for (const QJsonValue &val : unlockedList) {
                int achId = val.toInt();
                QSqlQuery updateQuery(db);
                updateQuery.prepare("UPDATE achievements SET unlocked = 1 WHERE id = ?");
                updateQuery.bindValue(0, achId);
                updateQuery.exec();
            }
            qDebug() << "从服务器同步了" << unlockedList.size() << "个成就";
        }

        if(gameWidget){
            gameWidget->setBestRecords(survival,speedrun,kills);
        }

        // 刷新界面显示
        updateRecords();
        return;
    }
    // 处理排行榜数据
    if (obj.contains("rank")) {
        QJsonArray rank = obj.value("rank").toArray();
        QString rankType = obj.value("rankType").toString();
        showRankingDialog(rank, rankType);
        return;
    }
}

void MainWindow::fetchUserStats()
{
    if(currentUsername=="Guest"){
        qDebug()<<"游客模式，不拉取数据";
        return;
    }
    qDebug()<<"===fetchUserStats被调用，用户名："<<currentUsername;
    QJsonObject req;
    req["action"]="GET_USER_STATS";
    req["username"]=currentUsername;
    sendToServer(req);
}

void MainWindow::setSocket(QTcpSocket *socket){
    qDebug()<<"setSocket被调用，传入socket状态:"<<(socket?socket->state():-1);
    if(serverSocket){
        serverSocket->disconnect(this);
        serverSocket->deleteLater();
    }

    serverSocket=socket;

    if(serverSocket){
        serverSocket->setParent(this);
        connect(serverSocket,&QTcpSocket::readyRead,this,&MainWindow::onServerRead);
        connect(serverSocket,&QTcpSocket::disconnected,this,[=](){
            qDebug()<<"服务器断开连接";
        });
    }
}


void MainWindow::updateRecords(){
    if(!recordLabel)return;
    // 从数据库读取数据
    QSqlQuery query(db);
    query.exec("SELECT best_value FROM player_records WHERE mode='survival'");
    float survivalBest = 0;
    if (query.next()) survivalBest = query.value(0).toFloat();
    query.exec("SELECT best_value FROM player_records WHERE mode='speedrun'");
    float speedrunBest = 9999;
    if (query.next()) speedrunBest = query.value(0).toFloat();

    QString text = QString("🏅 最佳记录：生存 %1 秒 | 速通 %2 秒")
                    .arg(survivalBest, 0, 'f', 1)
                    .arg(speedrunBest == 9999 ? "未通关" : QString::number(speedrunBest, 'f', 1));
    recordLabel->setText(text);
}

void MainWindow::initDatabase(){
    //建立数据库连接
    db=QSqlDatabase::addDatabase("QSQLITE","achievement_conn");
    db.setDatabaseName("game_data.db");

    if(!db.open()){
        qDebug()<<"Failed to open the database:"<<db.lastError().text();
        return;
    }

    //创建成就表
    QSqlQuery query(db);
    bool ok=query.exec(R"(
                       CREATE TABLE IF NOT EXISTS achievements(
                            id INTEGER PRIMARY KEY AUTOINCREMENT,
                            name TEXT NOT NULL,
                            description TEXT NOT NULL,
                            condition_type TEXT NOT NULL,
                            condition_value INTEGER NOT NULL,
                            unlocked INTEGER DEFAULT 0
                       )
                   )");
    if(!ok)qDebug()<<"Table creation failed:"<<query.lastError().text();



    //插入预设成就
    query .exec("SELECT COUNT(*) FROM achievements");
    if(query.next()&&query.value(0).toInt()==0){
        QStringList sqls;
        sqls << "INSERT OR IGNORE INTO achievements (id, name, description, condition_type, condition_value) VALUES (1, '初出茅庐', '累计击杀 100 个敌人', 'total_kills', 100)";
        sqls << "INSERT OR IGNORE INTO achievements (id, name, description, condition_type, condition_value) VALUES (2, '杀戮机器', '累计击杀 1000 个敌人', 'total_kills', 1000)";
        sqls << "INSERT OR IGNORE INTO achievements (id, name, description, condition_type, condition_value) VALUES (3, '生存专家', '在生存模式存活 200 秒', 'survival_time', 200)";
        sqls << "INSERT OR IGNORE INTO achievements (id, name, description, condition_type, condition_value) VALUES (4, '极速通关', '在 5 秒内完成速通', 'speedrun_time', 5)";
        sqls << "INSERT OR IGNORE INTO achievements (id, name, description, condition_type, condition_value) VALUES (5, '百发百中', '练习模式连续击杀 100 个敌人（无空枪）', 'practice_streak', 100)";
        sqls << "INSERT OR IGNORE INTO achievements (id, name, description, condition_type, condition_value) VALUES (6, '枪法如神', '只用 10 发子弹通关速通模式', 'speedrun_shots', 10)";
        for(const QString &s:sqls){
            query.exec(s);
        }
        qDebug()<<"成就数据初始化完成";
    }

    query.exec(R"(
            CREATE TABLE IF NOT EXISTS player_records (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                mode TEXT UNIQUE NOT NULL,
                best_value REAL DEFAULT 0
            )
        )");
        query.exec("INSERT OR IGNORE INTO player_records (mode, best_value) VALUES ('survival', 0)");
        query.exec("INSERT OR IGNORE INTO player_records (mode, best_value) VALUES ('speedrun', 9999)");
}

void MainWindow::showAchievements()
{
    QDialog dialog(this);
    dialog.setWindowTitle("🏆 成就列表");
    dialog.resize(450, 350);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QSqlQuery query(db); // 使用自己的 db 连接
    query.exec("SELECT name, description, unlocked FROM achievements ORDER BY id");

    while (query.next()) {
        QString name = query.value(0).toString();
        QString desc = query.value(1).toString();
        bool unlocked = query.value(2).toBool();

        QString status = unlocked ? "✅" : "🔒";
        QLabel *label = new QLabel(QString("%1 %2 - %3").arg(status).arg(name).arg(desc));
        label->setStyleSheet(unlocked ?
                             "color: #4CAF50; font-size: 15px; padding: 5px;" :
                             "color: #888; font-size: 15px; padding: 5px;");
        layout->addWidget(label);
    }
    dialog.exec();
}

void MainWindow::requestRanking(const QString &rankType)
{
    QJsonObject req;
    req["action"]="GET_RANK";
    req["rankType"]=rankType;
    sendToServer(req);
}

void MainWindow::showRankingDialog(const QJsonArray &rank, const QString &rankType)
{
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle(rankType == "survival" ? "🌍 生存排行榜" : "🌍 速通排行榜");
    dialog->resize(500, 400);

    QVBoxLayout *layout = new QVBoxLayout(dialog);

    // 标题
    QLabel *titleLabel = new QLabel(rankType == "survival" ? "🏆 生存模式 - 存活时间排行" : "🏆 速通模式 - 通关时间排行");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; padding: 10px;");
    layout->addWidget(titleLabel);

    // 表格
    QTableWidget *table = new QTableWidget(dialog);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"排名", "玩家", rankType == "survival" ? "存活(秒)" : "通关(秒)", "总击杀"});
    table->setRowCount(rank.size());

    for (int i = 0; i < rank.size(); ++i) {
        QJsonObject entry = rank[i].toObject();
        table->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        table->setItem(i, 1, new QTableWidgetItem(entry["username"].toString()));

        if (rankType == "survival") {
            double survival = entry["bestSurvival"].toDouble();
            table->setItem(i, 2, new QTableWidgetItem(QString::number(survival, 'f', 1)));
        } else {
            double speedrun = entry["bestSpeedrun"].toDouble();
            QString display = (speedrun >= 9999) ? "未通关" : QString::number(speedrun, 'f', 1);
            table->setItem(i, 2, new QTableWidgetItem(display));
        }
        table->setItem(i, 3, new QTableWidgetItem(QString::number(entry["totalKills"].toInt())));
    }
    table->resizeColumnsToContents();
    table->setEditTriggers(QTableWidget::NoEditTriggers);
    layout->addWidget(table);

    // 关闭按钮
    QPushButton *closeBtn = new QPushButton("关闭", dialog);
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(closeBtn);

    dialog->exec();
    delete dialog;
}

//开始游戏
void MainWindow::startGameWithMode(GameMode mode)
{
    gameWidget->setGameMode(mode);
    stackedWidget->setCurrentIndex(1);

    //让GameWidget获取焦点
    gameWidget->setFocus();
    gameWidget->activateWindow();
}

void MainWindow::updateLobbyUI()
{
    if (!lobbyWidget) return;

    int w = width();
    int h = height();

    int titleFontSize = qMax(20, qMin(w, h) / 12);
    int btnFontSize = qMax(14, qMin(w, h) / 25);

    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFontSize);
    titleLabel->setFont(titleFont);

    QFont btnFont;
    btnFont.setPointSize(btnFontSize);
    btnFont.setBold(true);
    practiceBtn->setFont(btnFont);
    survivalBtn->setFont(btnFont);
    speedrunBtn->setFont(btnFont);

    int btnW = qBound(200, w * 30 / 100, 500);
    int btnH = qMax(50, h / 12);

    practiceBtn->setFixedSize(btnW, btnH);
    survivalBtn->setFixedSize(btnW, btnH);
    speedrunBtn->setFixedSize(btnW, btnH);
}
