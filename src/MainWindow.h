#pragma once

#include "Injector.h"

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QLabel>

class StatusCard;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();

private slots:
    void poll();
    void onLog(const QString &text, int level);
    void onSteamButton();
    void onCs2Button();
    void onUnloadButton();
    void onLaunchCs2();
    void onCleanupButton();
    void onAutoToggled(bool on);
    void onAutoSteamToggled(bool on);

private:
    enum class AutoState { Off, WaitSteam, WaitCs2, WaitCs2Ready, Watch, Done };

    bool resolveProjectRoot();
    void startBuildCheck();
    void loadSettings();
    void saveSettings();
    void updateStates();
    void updateVacPanel();
    void refreshAccountPill();
    void applyThemeAccent();
    void log(const QString &text, Injector::Level level = Injector::Level::Info);
    void autoTick();
    void stopAuto(const QString &reason, Injector::Level level = Injector::Level::Warn);
    void playSound(bool success);

protected:
    void paintEvent(QPaintEvent *event) override;

    Injector m_injector;
    StatusCard *m_steamCard = nullptr;
    StatusCard *m_cs2Card = nullptr;
    QPushButton *m_steamBtn = nullptr;
    QPushButton *m_cs2Btn = nullptr;
    QPushButton *m_unloadBtn = nullptr;
    QPushButton *m_launchBtn = nullptr;
    QPushButton *m_cleanupBtn = nullptr;
    QLabel *m_vacLog = nullptr;
    QLabel *m_vacPtrace = nullptr;
    QLabel *m_vacUid = nullptr;
    QLabel *m_statsLabel = nullptr;
    QComboBox *m_buildBox = nullptr;
    QCheckBox *m_autoBox = nullptr;
    QCheckBox *m_autoSteamBox = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QLabel *m_rootPill = nullptr;
    QLabel *m_accountPill = nullptr;
    QTimer m_pollTimer;

    qint64 m_steamPid = 0;
    qint64 m_cs2Pid = 0;
    bool m_steamDecided = false;
    bool m_steamInjected = false;
    bool m_steamSkipped = false;
    bool m_steamFailed = false;
    bool m_steamBusy = false;
    bool m_cs2Injected = false;
    bool m_cs2Failed = false;
    bool m_cs2Busy = false;
    bool m_alreadyInjectedLogged = false;
    qint64 m_lastCs2Pid = 0;

    AutoState m_autoState = AutoState::Off;
    int m_autoSettle = 0;
    QString m_lastMissing;
    qint64 m_lastCpuJiffies = -1;
    qint64 m_cpuStableMs = 0;
    qint64 m_modulesReadyMs = 0;

    QString m_accentHex = QStringLiteral("#ab46ff");
    QColor m_accentColor = QColor(0xab, 0x46, 0xff);
    QString m_sessionLogPath;
    int m_injectionsToday = 0;
    qint64 m_lastLoadWaitMs = -1;
    QDateTime m_cs2DetectedAt;
    QDateTime m_injectStartedAt;
    qint64 m_lastVacHash = 0;
};
