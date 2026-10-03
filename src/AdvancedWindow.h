#pragma once

#include "Injector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

// The Advanced window: the Stand-Launchpad-style control surface for the experimental
// injection pipelines (native .so into any process, Windows DLL into any running Proton
// game). A checkable DLL list + auto-inject-on-target-appear with a delay, mirroring the
// friend-side launchpads.
//
// Pipeline is picked by SUFFIX: .dll -> Proton (ns_inject.exe through the game's own wine),
// anything else -> native (memfd, PID + library). Auto-inject watches the selected target and
// fires the checked entries (in list order) once per target appearance, after the delay.
class AdvancedWindow : public QDialog
{
    Q_OBJECT
public:
    explicit AdvancedWindow(Injector &injector, const QString &projectRoot, QWidget *parent = nullptr);
    void refreshTargets(); // public: the MainWindow opener triggers a fresh listing

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void addDll();
    void removeSelected();
    void moveSelected(int delta);
    void injectNow();
    void onAutoToggled(bool on);
    void autoTick();
    void onJobFinished(bool ok);

private:
    void buildUi();
    void saveSettings();
    void loadSettings();
    void updateStatus();
    void startNextJob();
    [[nodiscard]] QString targetKey() const;

    Injector &m_injector;
    QString m_root;

    QComboBox *m_targetBox = nullptr;
    QListWidget *m_dllList = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;
    QPushButton *m_injectBtn = nullptr;
    QCheckBox *m_autoBox = nullptr;
    QSpinBox *m_delaySpin = nullptr;
    QLabel *m_statusLabel = nullptr;

    QTimer m_autoTimer;
    QList<QPair<QString, QString>> m_jobQueue; // (pipeline, path) - "proton"/"native"
    QString m_targetKey;
    QString m_lastError;
    int m_delayLeft = 0;
    int m_injectedCount = 0;
    bool m_injectedThisAppearance = false;
    bool m_queueBusy = false;
};
