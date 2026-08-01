#include "gamewidget.h"
#include "mainwindow.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMatrix4x4>
#include <QtMath>
#include <QDebug>
#include <QStackedWidget>
#include <QPainter>
#include <QVector>
#include <QtMath>
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonObject>


//顶点数据（一个立方体）
static const float vertices[]={
    -0.5f,-0.5f,-0.5f,0.5f,-0.5f,-0.5f,
    0.5f,0.5f,-0.5f,-0.5f,0.5f,-0.5f,
    -0.5f,-0.5f,0.5f,0.5f,-0.5f,0.5f,
    0.5f,0.5f,0.5f,-0.5f,0.5f,0.5f
};

static const float colors[]{
    1,0,0, 1,0,0, 1,0,0, 1,0,0,
    0,1,0, 0,1,0, 0,1,0, 0,1,0,
    0,0,1, 0,0,1, 0,0,1, 0,0,1,
    1,1,0, 1,1,0, 1,1,0, 1,1,0,
};

static const unsigned int indices[]={
    0,1,2, 0,2,3, 4,5,6, 4,6,7,
    0,1,5, 0,5,4, 2,3,7, 2,7,6,
    0,3,7, 0,7,4, 1,2,6, 1,6,5,
};

//构造函数：初始化相机位置和角度
GameWidget::GameWidget(QWidget *parent):QOpenGLWidget(parent),gameMode(GameMode::Practice)
  ,score(0),cameraPos(0.0f,1.6f,5.0f),yaw(-90.0f),//初始朝Z方向
    pitch(0.0f),captureMouse(false),isTiming(false),currentTime(0.0f),
    isCrouching(false),ignoreMouseMove(false),spacePressed(false),ctrlPressed(false),
    health(100),maxHealth(100),ammo(30),maxAmmo(30),isGameOver(false),
    survivalTime(0.0f),survivalTiming(false),reserveAmmo(0),maxReserveAmmo(0),
    isReloading(false),totalKills(0),gameFinished(false),finishTime(0.0f),speedrunFailed(false),
    isPausedForTip(false),practiceStreak(0)

{
    yVelocity=0.0f;

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    memset(keys,0,sizeof (keys));

    //创建定时器，间隔16ms(即60 FPS)
    m_timer=new QTimer(this);
    connect(m_timer,&QTimer::timeout,this,QOverload<>::of(&QOpenGLWidget::update));
    m_timer->start(16);

    gameFinished=false;
    finishTime=0.0f;
}

GameWidget::~GameWidget(){
    makeCurrent();
    vao.destroy();
    vbo.destroy();
    vboColor.destroy();
    doneCurrent();
}

void GameWidget::checkAchievements(const QString &type, int value){
    QSqlDatabase db=QSqlDatabase::database("achievement_conn");
    if(!db.isOpen()){
        qDebug()<<"数据库未打开，无法检查成就";
        db.open();
    }

    QSqlQuery query(db);
    if(type=="speedrun_time"||type=="speedrun_shots"){
        query.prepare("SELECT id, name FROM achievements "
                      "WHERE condition_type = ? AND condition_value >= ? AND unlocked = 0");
    }else{
        query.prepare("SELECT id, name FROM achievements "
                      "WHERE condition_type = ? AND condition_value <= ? AND unlocked = 0");

    }
    query.bindValue(0, type);
    query.bindValue(1, value);
    if (!query.exec()) {
        qDebug() << "检查成就查询失败:" << query.lastError().text();
        return;
    }

    int count=0;
    while (query.next()) {
        count++;
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();
        unlockAchievement(id);
        qDebug() << "成就解锁：" << name;
    }
}

void GameWidget::unlockAchievement(int id)
{
    //本地更新
    QSqlDatabase db = QSqlDatabase::database("achievement_conn");
    if(!db.isOpen())return;
    QSqlQuery query(db);
    query.prepare("UPDATE achievements SET unlocked = 1 WHERE id = ?");
    query.bindValue(0, id);
    if (!query.exec()) {
        qDebug() << "解锁成就失败:" << query.lastError().text();
        return;
    }

    //同步到服务器
    if (!m_mainWindow) return;
    QString username = m_mainWindow->getCurrentUsername();
    if (username.isEmpty() || username == "Guest") return;

    QJsonObject obj;
    obj["action"] = "UNLOCK_ACHIEVEMENT";
    obj["username"] = username;
    obj["achievementId"] = id;
    m_mainWindow->sendToServer(obj);
}

//设置模式
void GameWidget::setGameMode(GameMode mode){
    pickups.clear();
    targets.clear();

    cameraPos = QVector3D(0.0f,1.6f,5.0f);
    yaw=-90.0f;
    pitch=0.0f;
    yVelocity=0.0f;
    isCrouching=false;
    gameTime=0.0f;
    gameMode=mode;
    score=0;
    isTiming=false;
    currentTime=0.0f;
    gameFinished=false;
    finishTime=0.0f;
    isGameOver=false;
    health=maxHealth;
    ammo=maxAmmo;
    survivalTime=0.0f;
    survivalTiming=false;

    if (mode == GameMode::Survival) {
        shotsFired=0;
        balance.baseEnemySpeed=0.025f;
        balance.currentSpeedMultiplier=1.0f;
        maxReserveAmmo = 90;   // 生存模式备弹 90 发（共 120 发）
        reserveAmmo = maxReserveAmmo;
        isPausedForTip=true;
    } else {
            maxReserveAmmo = 9999; // 练习/速通模式备弹无限
            reserveAmmo = maxReserveAmmo;
        }

    if(mode==GameMode::Practice){
        practiceStreak=0;
        enemiesMove=false;
        spawnTargets(5);
    }else if (mode == GameMode::Survival) {
        enemiesMove = true;
        spawnTargets(5);
        survivalTiming = true;
    } else if (mode == GameMode::Speedrun) {
        enemiesMove = true;
        spawnTargets(10);
        gameFinished=false;
        speedrunFailed=false;
        shotsFired=0;
        isTiming=true;
        speedrunTimer.start();
    }
}

//初始化OpenGL
void GameWidget::initializeGL(){
    qDebug() << "initializeGL called";
    initializeOpenGLFunctions();

    glClearColor(0.5f,0.7f,0.9f,1.0f);
    glEnable(GL_DEPTH_TEST);

    setupShaders();
    setupBuffers();

    QSqlDatabase db = QSqlDatabase::database("achievement_conn");
    if (db.isOpen()) {
        QSqlQuery query(db);
        query.exec("SELECT best_value FROM player_records WHERE mode='survival'");
        if (query.next()) bestSurvivalTime = query.value(0).toFloat();
        query.exec("SELECT best_value FROM player_records WHERE mode='speedrun'");
        if (query.next()) bestSpeedrunTime = query.value(0).toFloat();
    }

    setGameMode(gameMode);
}

void GameWidget::setupShaders() {
    if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex,
         "#version 330 core\n"
         "in vec3 position;\n"
         "in vec3 color;\n"
         "out vec3 fragColor;\n"
         "uniform mat4 model;\n"
         "uniform mat4 view;\n"
         "uniform mat4 projection;\n"
         "void main(){\n"
         "      gl_Position = projection * view * model * vec4(position, 1.0);\n"
         "      fragColor = color;\n"
         "}"
    )) {
        qDebug() << "Vertex shader compilation failed:" << program.log();
        return;
    }

    if (!program.addShaderFromSourceCode(QOpenGLShader::Fragment,
         "#version 330 core\n"
         "in vec3 fragColor;\n"
         "out vec4 outputColor;\n"
         "void main(){\n"
         "      outputColor = vec4(fragColor, 1.0);\n"
         "}"
    )) {
        qDebug() << "Fragment shader compilation failed:" << program.log();
        return;
    }

    if (!program.link()) {
        qDebug() << "Shader linking failed:" << program.log();
        return;
    }

    if (!program.bind()) {
        qDebug() << "Shader binding failed:" << program.log();
        return;
    }
};

void GameWidget::setupBuffers(){
    vao.create();
    vao.bind();

    vbo.create();
    vbo.bind();
    vbo.allocate(vertices,sizeof(vertices));

    program.enableAttributeArray("position");
    program.setAttributeBuffer("position",GL_FLOAT,0,3,0);

    vboColor.create();
    vboColor.bind();
    vboColor.allocate(colors,sizeof(colors));

    program.enableAttributeArray("color");
    program.setAttributeBuffer("color",GL_FLOAT,0,3,0);

    //索引缓冲区
    vao.release();

    // 地面顶点（4个顶点，组成一个大矩形）
    static const float groundVertices[] = {
        // 位置 (x, y, z)      // 颜色 (r, g, b)
        -10.0f, -0.5f, -10.0f,  0.4f, 0.4f, 0.4f,
         10.0f, -0.5f, -10.0f,  0.4f, 0.4f, 0.4f,
        -10.0f, -0.5f,  10.0f,  0.4f, 0.4f, 0.4f,
         10.0f, -0.5f,  10.0f,  0.4f, 0.4f, 0.4f
    };

    groundVao.create();
    groundVao.bind();
    groundVbo.create();
    groundVbo.bind();
    groundVbo.allocate(groundVertices, sizeof(groundVertices));

    // 位置属性 (步长 = 6个浮点，偏移0)
    program.enableAttributeArray("position");
    program.setAttributeBuffer("position", GL_FLOAT, 0, 3, 6 * sizeof(float));

    // 颜色属性 (步长 = 6个浮点，偏移3个浮点)
    program.enableAttributeArray("color");
    program.setAttributeBuffer("color", GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));

    groundVao.release();
}


void GameWidget::reload(){
    if(isReloading)return;
    if(ammo==maxAmmo){
        qDebug()<<"Magazine is full";
        return;
    }
    if(reserveAmmo<=0){
        qDebug()<<"Insufficient reserveAmmo";
        return;
    }

    isReloading=true;

    int needed=maxAmmo-ammo;
    int toLoad=qMin(needed,reserveAmmo);

    ammo+=toLoad;
    reserveAmmo-=toLoad;

    qDebug()<<"Magzine change complete!Current magzine:"<<ammo<<"reserveAmmo:"<<reserveAmmo;

    isReloading=false;
}

void GameWidget::shoot()
{
    if(isGameOver||gameFinished||speedrunFailed)return;
    if(ammo<=0){
        qDebug()<<"Out of ammo!Press R please!";
        return;
    }
    ammo--;
    shotsFired++;

    //1.计算当前摄像头的“前方向”向量
    float pitchRad=qDegreesToRadians(pitch);
    float yawRad=qDegreesToRadians(yaw);
    QVector3D front(cos(pitchRad)*cos(yawRad),sin(pitchRad),cos(pitchRad)*sin(yawRad));
    front.normalize();

    //2.定义射线：起点cameraPos,方向front
    QVector3D rayOrigin=cameraPos;
    QVector3D rayDirection=front;

    //3.遍历目标，找到最近被击中的
    float closestDist=1e9;
    int hitIndex=-1;

    for(int i=0;i<targets.size();i++){
        const Target &t=targets[i];
        if(!t.alive)continue;

        //4.碰撞检测
        float tMin=-1e9,tMax=1e9;
        bool hit=true;
        for(int i=0;i<3;i++){
            float origin =rayOrigin[i];
            float dir=rayDirection[i];
            float min=t.position[i]-t.halfSize;
            float max=t.position[i]+t.halfSize;

            if(qAbs(dir)<1e-6){
                if(origin<min||origin>max){
                    hit=false;
                    break;
                }//射线平行且不在盒内
            }else{
                float t1=(min-origin)/dir;
                float t2=(max-origin)/dir;
                if(t1>t2)qSwap(t1,t2);
                //取最晚进入时间和最早出去时间
                if(t1>tMin)tMin=t1;
                if(t2<tMax)tMax=t2;
                if(tMin>tMax){
                    hit=false;
                    break;//未命中
                }
            }
        }
        //如果命中，命中点距离大于0
        if(hit&&tMax>0&&tMin<closestDist){
            closestDist=tMin;
            hitIndex=i;
        }
    }

    //处理命中结果
    if(hitIndex!=-1){
        targets[hitIndex].alive=false;
        targets[hitIndex].respawnTimer=2.0f;
        qDebug()<<"You got#"<<hitIndex<<"!";
        score++;
        if (gameMode == GameMode::Speedrun && isTiming) {
                bool allDead = true;
                for (const Target &t : targets) {
                    if (t.alive) { allDead = false; break; }
                }
                if (allDead) {
                    isTiming = false;
                    gameFinished=true;
                    finishTime=currentTime;//记录完成耗时

                    if (finishTime < bestSpeedrunTime) {
                        bestSpeedrunTime = finishTime;
                        QSqlQuery query(QSqlDatabase::database("achievement_conn"));
                        query.prepare("UPDATE player_records SET best_value = ? WHERE mode = 'speedrun'");
                        query.bindValue(0, bestSpeedrunTime);
                        query.exec();
                    }
                    uploadStats();

                    checkAchievements("speedrun_time",(int)finishTime);
                    checkAchievements("speedrun_shots",shotsFired);
                    qDebug() << "🎉 速通完成！耗时：" << currentTime << "秒";
                    // 可以在这里显示通关信息

                    if(captureMouse){
                        captureMouse=false;
                        setCursor(Qt::ArrowCursor);
                    }
                }
            }
        if(gameMode ==GameMode::Survival){
            int r=rand()%10;
            if(r<3){
                spawnPickup(targets[hitIndex].position,PickupType::Ammo);
            }else if(r<5){
                spawnPickup(targets[hitIndex].position,PickupType::Health);
            }
        }

        totalKills++;

        checkAchievements("total_kills",totalKills);
        if(gameMode==GameMode::Practice){
            practiceStreak++;
            checkAchievements("kill_count",score);
        }
    }else{
        if(gameMode==GameMode::Practice){
            practiceStreak=0;
        }
        qDebug()<<"Missed";
    }
}

void GameWidget::spawnTargets(int count) {
    targets.clear();

    if(gameMode==GameMode::Speedrun){
        int rows = 3;
        int cols = 4;
        float spacingX = 2.5f;
        float spacingZ = 2.0f;
        int idx = 0;
        for (int r = 0; r < rows && idx < count; ++r) {
            for (int c = 0; c < cols && idx < count; ++c) {
                Target t;
                // 在网格位置加上随机小偏移，避免完全对齐
                float offsetX = (rand() % 40 - 20) / 100.0f;  // -0.2 ~ 0.2
                float offsetZ = (rand() % 40 - 20) / 100.0f;
                t.position = QVector3D(
                    -3.5f + c * spacingX + offsetX,
                    (rand() % 30) / 10.0f - 0.5f,  // y 保持随机
                    -3.0f - r * spacingZ + offsetZ
                );
                t.halfSize = 0.5f;
                t.alive = true;
                t.respawnTimer = 0.0f;
                targets.append(t);
                idx++;
            }
        }
    }else{
        for (int i = 0; i < count; ++i) {
            Target t;
            t.position = QVector3D(
                (rand() % 60 - 30) / 10.0f,
                (rand() % 30) / 10.0f - 0.5f,
                -(rand() % 40 + 20) / 10.0f
            );
            t.halfSize = 0.5f;
            t.alive = true;
            t.respawnTimer = 0.0f;
            targets.append(t);
        }
    }

}

void GameWidget::spawnPickup(const QVector3D &pos, PickupType type)
{
    if(pickups.size()>=maxPickups){
        for(int i=0;i<pickups.size();++i){
            if(!pickups[i].active){
                pickups.removeAt(i);
                break;
            }
        }
        if (pickups.size()>=maxPickups)return;
    }

    Pickup p;
    p.position=QVector3D(
                pos.x()+(rand()%20-10)/100.0f,
                0.2f,
                pos.z()+(rand()%20-10)/100.0f
                );
    p.type=type;
    p.active=true;
    p.respawnTimer=-1.0f;//不会重生，拾取就消失
    pickups.append(p);
}

void GameWidget::checkPickups()
{
    for (int i = pickups.size() - 1; i >= 0; --i) {
            Pickup &p = pickups[i];
            if (!p.active) continue;

            // 计算玩家与掉落物的距离
            float dist = (cameraPos - p.position).length();
            if (dist < 1.5f) { // 拾取半径
                if (p.type == PickupType::Ammo) {
                    int addAmount=5;
                    int spaceInMag=maxAmmo-ammo;
                    if(spaceInMag>0){
                        int toMag=qMin(spaceInMag,addAmount);
                        ammo+=toMag;
                        int leftover=addAmount-toMag;
                        if(leftover>0){
                            reserveAmmo=qMin(reserveAmmo+leftover,maxReserveAmmo);
                        }
                    }else{
                        reserveAmmo=qMin(reserveAmmo+addAmount,maxReserveAmmo);
                    }
                    qDebug() << "拾取弹药 +5";
                } else if (p.type == PickupType::Health) {
                    health = qMin(health + 20, maxHealth);
                    qDebug() << "拾取血包 +20";
                }
                p.active = false; // 消失
            }
        }

        // 清理非活跃的掉落物（延迟移除，防止频繁分配）
        pickups.erase(std::remove_if(pickups.begin(), pickups.end(),
                                     [](const Pickup &p) { return !p.active; }),
                      pickups.end());
}

void GameWidget::gameFinishShow()
{
    QPainter painter(this);
    // 半透明黑色背景
    painter.fillRect(rect(), QColor(0, 0, 0, 180));

    // 标题文字
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 36, QFont::Bold));
    QString msg = "🎉 速通完成！";
    QFontMetrics fmTitle(painter.font());
    int titleWidth = fmTitle.horizontalAdvance(msg);
    painter.drawText((width() - titleWidth) / 2, height()/2 - 60, msg);

    // 耗时信息
    painter.setFont(QFont("Arial", 28));
    QString timeMsg = "耗时: " + QString::number(finishTime, 'f', 2) + " 秒";
    QFontMetrics fmTime(painter.font());
    int timeWidth = fmTime.horizontalAdvance(timeMsg);
    painter.drawText((width() - timeWidth) / 2, height()/2 + 10, timeMsg);

    // 按钮：再来一局
    int btnW = 200, btnH = 60;
    int spacing = 30;
    int totalWidth = btnW * 2 + spacing;
    int startX = (width() - totalWidth) / 2;
    int btnY = height()/2 + 70;

    QRectF rectRestart(startX, btnY, btnW, btnH);
    QRectF rectBack(startX + btnW + spacing, btnY, btnW, btnH);
    btnRestartRect = rectRestart;
    btnBackRect = rectBack;

    // 绘制按钮背景
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(76, 175, 80));  // 绿色
    painter.drawRoundedRect(rectRestart, 10, 10);
    painter.setBrush(QColor(33, 150, 243)); // 蓝色
    painter.drawRoundedRect(rectBack, 10, 10);

    // 按钮文字
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 18, QFont::Bold));
    painter.drawText(rectRestart, Qt::AlignCenter, "🔄 再来一局");
    painter.drawText(rectBack, Qt::AlignCenter, "🏠 返回大厅");
}

void GameWidget::gameFailShow()
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 180));

    painter.setPen(Qt::red);
    painter.setFont(QFont("Arial", 36, QFont::Bold));
    QString msg = "⏱️ 速通失败！";
    QFontMetrics fmTitle(painter.font());
    int titleWidth = fmTitle.horizontalAdvance(msg);
    painter.drawText((width() - titleWidth) / 2, height()/2 - 80, msg);

    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 28));
    QString timeMsg = "超过 15 秒限制！";
    QFontMetrics fmTime(painter.font());
    int timeWidth = fmTime.horizontalAdvance(timeMsg);
    painter.drawText((width() - timeWidth) / 2, height()/2 - 20, timeMsg);

    painter.setPen(Qt::yellow);
    painter.setFont(QFont("Arial", 24));
    QString shotMsg = "本次使用子弹: " + QString::number(shotsFired) + " 发";
    QFontMetrics fmShot(painter.font());
    int shotWidth = fmShot.horizontalAdvance(shotMsg);
    painter.drawText((width() - shotWidth) / 2, height()/2 + 40, shotMsg);

    // 按钮：重新挑战和返回大厅（复用之前的按钮区域）
    int btnW = 200, btnH = 60;
    int spacing = 30;
    int totalWidth = btnW * 2 + spacing;
    int startX = (width() - totalWidth) / 2;
    int btnY = height()/2 + 90;

    QRectF rectRestart(startX, btnY, btnW, btnH);
    QRectF rectBack(startX + btnW + spacing, btnY, btnW, btnH);
    btnRestartRect = rectRestart;
    btnBackRect = rectBack;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(76, 175, 80));
    painter.drawRoundedRect(rectRestart, 10, 10);
    painter.setBrush(QColor(33, 150, 243));
    painter.drawRoundedRect(rectBack, 10, 10);

    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 18, QFont::Bold));
    painter.drawText(rectRestart, Qt::AlignCenter, "🔄 重新挑战");
    painter.drawText(rectBack, Qt::AlignCenter, "🏠 返回大厅");

}

void GameWidget::speedrunRestart()
{
    setGameMode(GameMode::Speedrun);
    isTiming=true;
    speedrunTimer.start();
    gameFinished=false;
    update();
    return;
}

void GameWidget::speedrunBackHome()
{
    if(captureMouse){
        captureMouse=false;
        setCursor(Qt::ArrowCursor);
    }
    emit goBackToLobby();
    return;
}

//每一帧绘制
void GameWidget::paintGL()
{
    QPainter painter(this);

    //qDebug() << "paintGL called";
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    //先处理输入（移动和旋转）
    processInput();
    //更新相机矩阵
    static QElapsedTimer timer;
    if(!timer.isValid())timer.start();
    float delta=timer.elapsed()/1000.0f;
    timer.restart();

    updateTargets(delta);

    QMatrix4x4 projection;
    projection.perspective(60.0f,(float)width()/height(),0.1f,100.0f);

    // 视图矩阵（相机视角）
    float pitchRad = qDegreesToRadians(pitch);
    float yawRad = qDegreesToRadians(yaw);
    QVector3D front(cos(pitchRad)*cos(yawRad),
                    sin(pitchRad),
                    cos(pitchRad)*sin(yawRad));
    front.normalize();
    QMatrix4x4 view;
    view.lookAt(cameraPos, cameraPos + front, QVector3D(0,1,0));

    // 模型矩阵（把物体放在眼前）
    QMatrix4x4 model;
    model.translate(0, 0, -2);

    // 绑定着色器并分别传递三个矩阵
    program.bind();
    groundVao.bind();
    QMatrix4x4 groundModel; // 单位矩阵，顶点已经定义在绝对坐标
    program.setUniformValue("model", groundModel);
    program.setUniformValue("view", view);
    program.setUniformValue("projection", projection);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    groundVao.release();
    vao.bind();

    for(const Target &t:targets){
        if(!t.alive)continue;

        QMatrix4x4 model;
        model.translate(t.position);

        program.setUniformValue("projection", projection);
        program.setUniformValue("view", view);
        program.setUniformValue("model", model);

        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, indices);
    }
    for(const Pickup &p:pickups){
        if(!p.active)continue;

        static float angle=0.0f;
        angle+=0.02f;//每帧旋转角度

        QMatrix4x4 model;
        model.translate(p.position);
        model.rotate(angle,0,1,0);//绕Y轴旋转
        model.scale(0.3f);
    }

    vao.release();
    program.release();

    //2D UI绘制
    painter.setRenderHint(QPainter::Antialiasing);

    //准星
    painter.setPen(QPen(Qt::white,2));
    int centerX=width()/2;
    int centerY=height()/2;
    painter.drawLine(centerX-15,centerY,centerX-5,centerY);
    painter.drawLine(centerX+5,centerY,centerX+15,centerY);
    painter.drawLine(centerX,centerY-15,centerX,centerY-5);
    painter.drawLine(centerX,centerY+5,centerX,centerY+15);

    painter.setPen(Qt::red);
    painter.drawPoint(centerX,centerY);

    //得分
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial",24));
    painter.drawText(20,50,"Score: "+QString::number(score));

    //血量和弹药
    painter.setPen(Qt::green);
    painter.setFont(QFont("Arial", 20));
    painter.drawText(20, 80, "Health: " + QString::number(health) + "/" + QString::number(maxHealth));
    if (gameMode == GameMode::Survival) {
        // 生存模式显示：弹匣/30 | 备弹数量
        QString ammoText = QString("Ammo: %1/%2 | Reserve: %3")
                               .arg(ammo)
                               .arg(maxAmmo)
                               .arg(reserveAmmo);
        painter.drawText(20, 110, ammoText);
    } else {
        // 练习/速通模式显示：弹匣/30 (∞)
        QString ammoText = QString("Ammo: %1/%2 (∞)")
                               .arg(ammo)
                               .arg(maxAmmo);
        painter.drawText(20, 110, ammoText);
    }

    //计时器
    if(gameMode==GameMode::Speedrun && isTiming){
        currentTime = speedrunTimer.elapsed() / 1000.0f; // 毫秒转秒
            painter.setPen(Qt::cyan);
            painter.setFont(QFont("Arial", 20));
            QString timeStr = "Time: " + QString::number(currentTime, 'f', 2) + "s";
            QFontMetrics fm(painter.font());
            int textWidth = fm.horizontalAdvance(timeStr);
            // 距离右边缘20像素，距离顶边50像素
            painter.drawText(width() - textWidth - 20, 50, timeStr);
    }

    // 掉落物
    if (gameMode == GameMode::Survival) {
        for (const Pickup &p : pickups) {
            if (!p.active) continue;

            QMatrix4x4 mvp = projection * view;
            QVector4D screenPos = mvp * QVector4D(p.position, 1.0f);
            if (screenPos.z() < 0) continue;

            float x = (screenPos.x() / screenPos.w() + 1.0f) / 2.0f * width();
            float y = (1.0f - (screenPos.y() / screenPos.w() + 1.0f) / 2.0f) * height();


            float size = 40.0f;

            if (p.type == PickupType::Ammo) {
                QRectF rect(x - size/2, y - size/2, size, size);

                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 220, 50)); // 鲜亮黄
                painter.drawRoundedRect(rect, 10, 10);

                painter.setBrush(QColor(255, 255, 200, 150));
                painter.drawRoundedRect(rect.adjusted(4, 4, -20, -20), 5, 5);

                // 画三个小竖条（模拟子弹/弹药标记）
                painter.setPen(QPen(QColor(255, 180, 0), 3));
                painter.setBrush(Qt::NoBrush);
                for (int i = -1; i <= 1; ++i) {
                    painter.drawLine(x + i * 8, y - 10, x + i * 8, y + 10);
                }

                // 在上面加一个“★”星星（代表弹药补给）
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 255, 0));
                painter.drawEllipse(x, y - 10, 8, 8);

                // 底下加个小字“Ammo”
                painter.setPen(QColor(80, 60, 0));
                painter.setFont(QFont("Arial", 10, QFont::Bold));
                painter.drawText(rect.adjusted(0, 20, 0, 0), Qt::AlignCenter, "弹药");

            } else {
                QRectF rect(x - size/2, y - size/2, size, size);

                // 主背景（圆角矩形，鲜红色）
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 80, 80));
                painter.drawRoundedRect(rect, 10, 10);

                // 高光
                painter.setBrush(QColor(255, 200, 200, 150));
                painter.drawRoundedRect(rect.adjusted(4, 4, -20, -20), 5, 5);

                // 画一个白色十字（医疗标志）
                painter.setPen(QPen(Qt::white, 4));
                painter.drawLine(x, y - 12, x, y + 12);
                painter.drawLine(x - 12, y, x + 12, y);

                // 4. 中间加一个小心心 ❤️
                painter.setPen(QPen(Qt::white, 2));
                painter.setBrush(QColor(255, 255, 255, 80));
                // 画个圆点代表“治疗”
                painter.drawEllipse(x, y, 6, 6);

                painter.setPen(QColor(120, 0, 0));
                painter.setFont(QFont("Arial", 10, QFont::Bold));
                painter.drawText(rect.adjusted(0, 20, 0, 0), Qt::AlignCenter, "医疗");
            }
        }
    }

    //提示
    static float tipTimer=0.0f;
    if(gameMode==GameMode::Survival&&firstSurvival){
        tipTimer+=delta;
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial",20));
        painter.drawText(rect(),Qt::AlignCenter,"击杀敌人掉落补给，走进自动拾取");
        if(tipTimer>1.5f){
            firstSurvival=false;
            isPausedForTip=false;
            tipTimer=0.0f;
        }
    }

    //判断游戏进程
    if(gameFinished){
        gameFinishShow();
    }else if(speedrunFailed){
        gameFailShow();
    }
    if (isGameOver) {
        // 半透明遮罩
        painter.fillRect(rect(), QColor(0, 0, 0, 160));

        painter.setPen(Qt::red);
        painter.setFont(QFont("Arial", 48, QFont::Bold));
        QString overText = "💀 GAME OVER";
        QFontMetrics fm(painter.font());
        painter.drawText((width() - fm.horizontalAdvance(overText)) / 2, height()/2 - 40, overText);

        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 28));
        QString timeText = "你存活了 " + QString::number(survivalTime, 'f', 1) + " 秒";
        QFontMetrics fm2(painter.font());
        painter.drawText((width() - fm2.horizontalAdvance(timeText)) / 2, height()/2 + 40, timeText);

        // 返回大厅按钮
        int btnW = qBound(180,width()/4,320);
        int btnH = qBound(50,height()/12,80);
        int spacing=30;

        int totalWidth=btnW*2+spacing;
        int startX=(width()-totalWidth)/2;
        int btnY =height()/2+80;

        QRect btnRestartSurvival(startX,btnY, btnW, btnH);
        QRect btnBackSurvival(startX+btnW+spacing,btnY, btnW, btnH);

        painter.setBrush(QColor(76, 175, 80)); // 绿色
        painter.drawRoundedRect(btnRestartSurvival, 10, 10);
        painter.setBrush(QColor(33, 150, 243)); // 蓝色
        painter.drawRoundedRect(btnBackSurvival, 10, 10);
        // 存储区域以便鼠标事件检测
        btnRestartRect = btnRestartSurvival;
        btnBackRect = btnBackSurvival;
        // 文字
        painter.setFont(QFont("Arial",18,QFont::Bold));
        painter.drawText(btnRestartSurvival, Qt::AlignCenter, "🔄 重新挑战");
        painter.drawText(btnBackSurvival, Qt::AlignCenter, "🏠 返回大厅");
    }

    painter.end();
}

void GameWidget::updateTargets(float deltaTime){
    switch (gameMode) {
        case GameMode::Practice:
            updateTargetsPractice(deltaTime);
            break;
        case GameMode::Survival:
            updateTargetsSurvival(deltaTime);
            checkPickups();
            break;
        case GameMode::Speedrun:
            updateTargetsSpeedrun(deltaTime);
            break;
        }
}

void GameWidget::updateTargetsSurvival(float delta) {
    if(isPausedForTip && firstSurvival) return;
    if(isGameOver)return;

    for (Target &t : targets) {
        // 复活
        if (!t.alive) {
            t.respawnTimer -= delta;
            if (t.respawnTimer <= 0) {
                t.alive = true;
                // 在随机位置重生
                t.position = QVector3D(
                    (rand() % 60 - 30) / 10.0f,
                    (rand() % 30) / 10.0f - 0.5f,
                    -(rand() % 40 + 20) / 10.0f
                );
            }
            continue;
        }

        // 追踪玩家：计算从目标到玩家的方向向量
        QVector3D dir = cameraPos - t.position;
        float distance = dir.length();

        float speedFactor = 1.0f;
            if (ammo > 20) speedFactor *= 1.3f;   // 弹药充足，敌人加速
            if (health < 30) speedFactor *= 0.7f; // 血量低，敌人减速（仁慈机制）
            // 让速度在合理范围内波动
            float finalSpeed = balance.baseEnemySpeed * qBound(0.5f, speedFactor, 1.8f);

        if (distance > 0.1f) {
            dir.normalize();
            t.position += dir * finalSpeed * delta * 60; // 保持帧率独立
        }

        // 如果敌人靠近玩家（距离 < 1.0），攻击玩家
        if (distance < 1.0f && t.alive) {
            // 攻击玩家
            health -= 10;   // 每次攻击扣10血
            qDebug() << "Enemy attacked! Health:" << health;
            // 攻击后敌人消失或重置（也可以让敌人弹开）
            t.alive = false;
            t.respawnTimer = 1.0f; // 1秒后复活

            // 检查玩家是否死亡
            if (health <= 0) {
                health = 0;
                isGameOver = true;

                if (survivalTime > bestSurvivalTime) {
                    bestSurvivalTime = survivalTime;
                    QSqlQuery query(QSqlDatabase::database("achievement_conn"));
                    query.prepare("UPDATE player_records SET best_value = ? WHERE mode = 'survival'");
                    query.bindValue(0, bestSurvivalTime);
                    query.exec();
                }
                uploadStats();

                survivalTiming=false;
                if(captureMouse){
                    captureMouse=false;
                    setCursor(Qt::ArrowCursor);
                }
                qDebug() << "Game Over!";
                return;
            }
        }
    }

    // 增加敌人数量（生存模式特性）：每10秒增加一个敌人
    static float spawnTimer = 0.0f;
    spawnTimer += delta;
    if (spawnTimer > 10.0f && targets.size() < 20) {
        Target newTarget;
        // 生成在视野前方较远的位置
        newTarget.position = QVector3D(
            (rand() % 60 - 30) / 10.0f,
            (rand() % 30) / 10.0f - 0.5f,
            -(rand() % 60 + 30) / 10.0f  // 更远的距离
        );
        newTarget.halfSize = 0.5f;
        newTarget.alive = true;
        newTarget.respawnTimer = 0.0f;
        targets.append(newTarget);
        spawnTimer = 0.0f;
    }
    if (!isGameOver) {
            survivalTime += delta;
            checkAchievements("survival_time",(int)survivalTime);
        }
}

void GameWidget::updateTargetsPractice(float delta)
{
    for(Target &t:targets){
        if(!t.alive){
            t.respawnTimer-=delta;
            if(t.respawnTimer<=0){
                t.alive=true;
                t.position=QVector3D(
                            (rand()%60-30)/10.0f,
                            (rand()%30)/10.0f-0.5f,
                            -(rand()%40+20)/10.0f
                            );
            }
        }
    }
    gameTime+=delta;
}

void GameWidget::updateTargetsSpeedrun(float delta)
{
    for(Target &t:targets){
        if(enemiesMove && t.alive){
            t.position.setX(t.position.x()+0.01f*sin(gameTime+t.position.y()));
        }
    }
    gameTime+=delta;
    if(isTiming && !gameFinished){
        currentTime=speedrunTimer.elapsed()/1000.0f;
        if(currentTime >15.0f){
            //超时失败
            isTiming=false;
            speedrunFailed=true;
            if(captureMouse){
                captureMouse=false;
                setCursor(Qt::ArrowCursor);
            }
            qDebug()<<"速通超时！";
        }
    }
}



//窗口大小变化时调整投影
void GameWidget::resizeGL(int w, int h){
    glViewport(0,0,w,h);
}

void GameWidget::processInput(){
    if(gameFinished||speedrunFailed||isGameOver) return;

    if (spacePressed && qAbs(yVelocity) < 0.001f) {
            yVelocity = 0.08f;
            if (isCrouching) {
                        isCrouching = false;
                        cameraPos.setY(cameraPos.y() + (STAND_HEIGHT - CROUCH_HEIGHT));
                        yVelocity = 0.0f;   // 重置，再赋予跳跃速度
                        yVelocity = 0.08f;  // 重新应用跳跃速度
                    }
        }

    // 蹲下切换
    if (ctrlPressed && !isCrouching) {
            isCrouching = true;
            cameraPos.setY(cameraPos.y() - (STAND_HEIGHT-CROUCH_HEIGHT));
            yVelocity=0.0f;
        } else if (!ctrlPressed && isCrouching) {
            isCrouching = false;
            cameraPos.setY(cameraPos.y() + (STAND_HEIGHT-CROUCH_HEIGHT));
            yVelocity=0.0f;
        }

    // 计算方向向量
    float speed = isCrouching ? 0.025f : 0.05f; // 蹲下速度减半
    QVector3D front(cos(qDegreesToRadians(pitch)) * cos(qDegreesToRadians(yaw)),
                    sin(qDegreesToRadians(pitch)),
                    cos(qDegreesToRadians(pitch)) * sin(qDegreesToRadians(yaw)));
    front.normalize();
    QVector3D right = QVector3D::crossProduct(front, QVector3D(0, 1, 0)).normalized();

    // 移动
    if (keys['W']) cameraPos += front * speed;
    if (keys['S']) cameraPos -= front * speed;
    if (keys['A']) cameraPos -= right * speed;
    if (keys['D']) cameraPos += right * speed;

    // 重力
    float gravity = -0.002f;
    yVelocity += gravity;
    cameraPos.setY(cameraPos.y() + yVelocity);

    float groundY = -0.5f;
    float minHeight=groundY+(isCrouching?CROUCH_HEIGHT:STAND_HEIGHT);
    if (cameraPos.y() < minHeight) {
        cameraPos.setY(minHeight);
        yVelocity = 0.0f;
    }
    float ceilingY = 10.0f;
    if (cameraPos.y() > ceilingY) {
        cameraPos.setY(ceilingY);

    }
}

void GameWidget::mousePressEvent(QMouseEvent *ev)
{
    if (isGameOver) {
        QPoint pos = ev->pos();
        if (btnBackRect.contains(pos)) {
            speedrunBackHome();
        }else if(btnRestartRect.contains(pos)){
            setGameMode(GameMode::Survival);
            update();
        }
        return;
    }
    if(gameFinished||speedrunFailed){
        QPoint pos=ev->pos();
        if(btnRestartRect.contains(pos)){
            speedrunRestart();
        }else if(btnBackRect.contains(pos)){
            speedrunBackHome();
        }
        return;
    }
    if(ev->button()==Qt::LeftButton){
        if(!captureMouse){
            captureMouse=true;
            setCursor(Qt::BlankCursor);
            lastMousePos=ev->pos();
        }else{
         shoot();
        }
    }   
}

void GameWidget::mouseMoveEvent(QMouseEvent *ev)
{
    if(!captureMouse||ignoreMouseMove)return;

    ignoreMouseMove=true;

    int dx=ev->x()-lastMousePos.x();
    int dy=ev->y()-lastMousePos.y();
    yaw+=dx*0.1f;
    pitch-=dy*0.1f;
    if(pitch>89)pitch=89;
    if(pitch<-89)pitch=-89;
    QPoint center=QPoint(width()/2,height()/2);
    QCursor::setPos(mapToGlobal(center));
    lastMousePos=center;

    ignoreMouseMove =false;
}

void GameWidget::keyPressEvent(QKeyEvent *ev)
{
    int key=ev->key();
    if(key<256){
        keys[key]=true;
    }
    if (key == Qt::Key_Space) spacePressed = true;
    if (key == Qt::Key_Control) ctrlPressed = true;
    if(key==Qt::Key_Escape){
        //释放鼠标
        if(captureMouse){
            captureMouse=false;
            setCursor(Qt::ArrowCursor);
        }
        //返回大厅
        emit goBackToLobby();
        return;
    }
    if(key==Qt::Key_R){
        if(gameFinished)return;
        reload();
    }
}

void GameWidget::keyReleaseEvent(QKeyEvent *ev)
{
    int key=ev->key();
    if(key<256){
        keys[key]=false;
    }
    if (key == Qt::Key_Space) spacePressed = false;
    if (key == Qt::Key_Control) ctrlPressed = false;
}

void GameWidget::uploadStats()
{
    if (!m_mainWindow) {
        qDebug() << "m_mainWindow 为空，无法上传";
        return;
    }
    QString username = m_mainWindow->getCurrentUsername();
    if (username.isEmpty() || username == "Guest") {
        qDebug() << "游客模式，不上传数据";
        return;
    }
    qDebug() << "上传数据：survival=" << bestSurvivalTime
             << " speedrun=" << bestSpeedrunTime
             << " kills=" << totalKills;
    emit uploadStatsSignal(username, bestSurvivalTime, bestSpeedrunTime, totalKills);
}


void GameWidget::setBestRecords(float survival, float speedrun, int kills){
    bestSurvivalTime=survival;
    bestSpeedrunTime=speedrun;
    totalKills=kills;
}


