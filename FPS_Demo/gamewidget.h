#ifndef GAMEWIDGET_H
#define GAMEWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QVector3D>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QRectF>
#include <QSqlDatabase>
#include <QSqlQuery>

class MainWindow;

enum class GameMode{
    Practice,Survival,Speedrun
};

enum class PickupType{
    Ammo,Health
};

struct Target{
    QVector3D position;
    float halfSize;
    bool alive;
    float respawnTimer;//复活计时器（秒）
};

struct Pickup{
    QVector3D position;
    PickupType type;
    bool active;
    float respawnTimer;
};

class GameWidget : public QOpenGLWidget,protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit GameWidget(QWidget *parent=nullptr);
    ~GameWidget();
    void setGameMode(GameMode mode);
    void setBestRecords(float survival,float speedrun,int kills);
    void setMainWindow(MainWindow *mainWin){m_mainWindow=mainWin;}

protected:
    //三个核心函数
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void updateTargets(float deltaTime);
    void updateTargetsSurvival(float delta);
    void updateTargetsPractice(float delta);
    void updateTargetsSpeedrun(float delta);


    //鼠标和键盘事件
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev)override;
    void keyPressEvent(QKeyEvent *ev)override;
    void keyReleaseEvent(QKeyEvent *ev)override;

private:
    struct BanlanceConfig{
        float baseEnemySpeed;
        float currentSpeedMultiplier;
    }balance;

    QOpenGLShaderProgram program;
    QOpenGLBuffer vbo;
    QOpenGLBuffer vboColor;
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer groundVbo;
    QOpenGLVertexArrayObject groundVao;
    QTimer *m_timer;
    QElapsedTimer speedrunTimer;

    GameMode gameMode;

    MainWindow *m_mainWindow=nullptr;

    QVector<Target> targets;       //目标列表
    QVector<Pickup> pickups;
    int maxPickups=25;

    int practiceStreak;
    int totalKills;
    float bestSurvivalTime;
    float bestSpeedrunTime;
    bool ignoreMouseMove;
    bool spacePressed;
    bool ctrlPressed;

    float survivalTime;
    bool survivalTiming;

    const float STAND_HEIGHT=0.5f;
    const float CROUCH_HEIGHT=0.15f;

    bool isCrouching;
    float gameTime;     //计时
    int score;
    float yVelocity;//垂直速度
    bool enemiesMove;//敌人是否移动

    bool isPausedForTip;

    bool isTiming;//是否正在计时
    float currentTime;//当前耗时
    bool gameFinished;//速通是否完成
    bool speedrunFailed;//是否超时失败
    float finishTime;
    QRectF btnRestartRect;//"再来一局"
    QRectF btnBackRect;//"返回大厅"

    int health;
    int ammo;//弹药数量
    int maxHealth;
    int maxAmmo;
    int reserveAmmo;
    int maxReserveAmmo;
    int shotsFired;
    bool firstSurvival=true;
    bool isReloading;
    bool isGameOver;

    //相机参数
    QVector3D cameraPos;    //摄像机位置
    float yaw;              //左右旋转（弧度）
    float pitch;            //上下旋转（弧度）

    //键盘状态
    bool keys[256];

    //鼠标捕获
    bool captureMouse;
    QPoint lastMousePos;

    void uploadStats();

    void checkAchievements(const QString &type,int value);
    void unlockAchievement(int id);

    void processInput();
    void setupShaders();
    void setupBuffers();

    void reload();
    void shoot();
    void spawnTargets(int count);
    void spawnPickup(const QVector3D &pos,PickupType type);
    void checkPickups();
    void gameFinishShow();
    void gameFailShow();
    void speedrunRestart();
    void speedrunBackHome();
signals:
    void goBackToLobby();
    void uploadStatsSignal(const QString &username,float bestSurvival,float bestSpeedrun,int totalKills);
};

#endif // GAMEWIDGET_H
