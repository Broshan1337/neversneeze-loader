#include "MainWindow.h"
#include "Icons.h"
#include "StatusCard.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <QPainter>

namespace {
const QColor kSteamAccent(0x66, 0xc0, 0xf4);
const QColor kCs2Accent(0xe0, 0xa2, 0x3c);
const QColor kLime(0xa3, 0xd4, 0x1f);
const QColor kAccent(0xab, 0x46, 0xff);
} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("Neversneeze Loader"));
    resize(720, 640);
    setMinimumSize(640, 560);

    // Palette lifted from the in-game menu shell (Neverlose.cpp): near-black panels,
    // hairline rgb(26,26,30) borders, lime accent, muted gray text.
    setStyleSheet(QStringLiteral(R"(
        #Root { background:#0c0c0d; }
        #Card { background:#101012; border-radius:14px; }
        #Header { color:#f3f5f7; font-size:19px; font-weight:800; letter-spacing:2px; background:transparent; }
        #HeaderAccent { color:#ab46ff; font-size:19px; font-weight:300; letter-spacing:2px; background:transparent; }
        #LogHeader { color:#565b63; font-size:11px; font-weight:700; letter-spacing:2px; background:transparent; }
        #RootPillOk { color:#ab46ff; background:rgba(171,70,255,0.10);
            border:1px solid rgba(171,70,255,0.45); border-radius:10px; padding:3px 12px; font-weight:700; font-size:11px; }
        #RootPillBad { color:#e5484d; background:rgba(229,72,77,0.12);
            border:1px solid rgba(229,72,77,0.4); border-radius:10px; padding:3px 12px; font-weight:700; font-size:11px; }
        QPushButton { background:#18181a; color:#dfe4ea; border:1px solid #26262a;
            border-radius:6px; padding:9px 14px; font-weight:600; font-size:13px; }
        QPushButton:hover { background:#202024; border-color:#ab46ff; color:#ab46ff; }
        QPushButton:pressed { background:#141416; }
        QPushButton:disabled { color:#565e69; background:#121214; border-color:#1c1c20; }
        QPushButton#SteamBtn { background:#1b2838; border-color:#2a4a66; color:#c7e1f8; }
        QPushButton#SteamBtn:hover { background:#20304a; border-color:#66c0f4; color:#c7e1f8; }
        QPushButton#SteamBtn:disabled { background:#141a20; border-color:#1e2c38; color:#4e5a64; }
        QPushButton#Cs2Btn { background:#18181a; border-color:#26262a; color:#f2d9a6; }
        QPushButton#Cs2Btn:hover { background:#202024; border-color:#ab46ff; color:#f6efdd; }
        QPushButton#Cs2Btn:disabled { background:#121214; border-color:#1c1c20; color:#5a5244; }
        QPushButton#UnloadBtn { background:#1e1416; border-color:#3c2226; color:#e8b8bc; }
        QPushButton#UnloadBtn:hover { background:#2a171b; border-color:#be3c42; color:#f2d3d5; }
        QPushButton#UnloadBtn:disabled { background:#121214; border-color:#1c1c20; color:#5a5244; }
        QPushButton#CleanupBtn { padding:6px 12px; font-size:12px; }
        #VacPanel { background:#101012; border:1px solid #1a1a1e; border-radius:10px; }
        #VacTitle { color:#565b63; font-size:10px; font-weight:700; letter-spacing:2px; background:transparent; }
        #VacRow { font-size:11px; background:transparent; }
        #VacOk { color:#ab46ff; }
        #VacWarn { color:#f0b429; }
        #VacBad { color:#e5484d; }
        #VacMuted { color:#565b63; }
        #StatsLabel { color:#565b63; font-size:11px; background:transparent; }
        #VacPanel QComboBox { padding:3px 8px; font-size:11px; }
        #VacPanel QCheckBox { font-size:11px; spacing:5px; }
        #AccountPill { color:#aaadb8; background:#18181a; border:1px solid #26262a;
            border-radius:10px; padding:3px 12px; font-weight:600; font-size:11px; }
        QComboBox { background:#18181a; color:#dfe4ea; border:1px solid #26262a;
            border-radius:6px; padding:6px 10px; font-size:12px; }
        QComboBox:hover { border-color:#3a3a40; }
        QComboBox::drop-down { border:none; background:transparent; width:20px; }
        QComboBox::down-arrow { image:url(:/assets/chevron.png); width:12px; height:12px; }
        QComboBox QAbstractItemView { background:#101012; color:#dfe4ea;
            selection-background-color:#26262a; selection-color:#ab46ff; border:1px solid #26262a; }
        QCheckBox { color:#aaadb8; font-size:12px; spacing:7px; background:transparent; }
        QCheckBox:hover { color:#dfe4ea; }
        QCheckBox:disabled { color:#565e69; }
        QCheckBox::indicator { width:15px; height:15px; border:1px solid #26262a;
            border-radius:4px; background:#18181a; }
        QCheckBox::indicator:hover { border-color:#ab46ff; }
        QCheckBox::indicator:checked { background:#ab46ff; border-color:#ab46ff; }
        QCheckBox::indicator:disabled { background:#121214; border-color:#1c1c20; }
        QPlainTextEdit#Log { background:#0e0e10; border:1px solid #1a1a1e; border-radius:10px;
            color:#aaadb8; font-family:'JetBrains Mono','DejaVu Sans Mono',monospace; font-size:12px; }
    )"));

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("Root"));
    setCentralWidget(central);

    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(24, 20, 24, 20);
    rootLayout->setSpacing(16);

    auto *header = new QHBoxLayout;
    header->setSpacing(6);
    auto *title = new QLabel(QStringLiteral("NEVERSNEEZE"), this);
    title->setObjectName(QStringLiteral("Header"));
    auto *titleAccent = new QLabel(QStringLiteral("LOADER"), this);
    titleAccent->setObjectName(QStringLiteral("HeaderAccent"));
    header->addWidget(title);
    header->addWidget(titleAccent);
    header->addStretch();
    m_rootPill = new QLabel(this);
    header->addWidget(m_rootPill);
    m_accountPill = new QLabel(this);
    m_accountPill->setObjectName(QStringLiteral("AccountPill"));
    m_accountPill->setVisible(false);
    header->addWidget(m_accountPill);
    rootLayout->addLayout(header);

    auto *cards = new QHBoxLayout;
    cards->setSpacing(16);

    m_steamBtn = new QPushButton(QStringLiteral("Inject Steam Module"), this);
    m_steamBtn->setObjectName(QStringLiteral("SteamBtn"));
    m_steamCard = new StatusCard(Icons::Kind::Steam, QStringLiteral("STEAM"), kSteamAccent, m_steamBtn, this);
    cards->addWidget(m_steamCard, 1);

    auto *cs2Content = new QWidget(this);
    auto *cs2Layout = new QVBoxLayout(cs2Content);
    cs2Layout->setContentsMargins(0, 0, 0, 0);
    cs2Layout->setSpacing(8);
    m_launchBtn = new QPushButton(QStringLiteral("Launch CS2"), cs2Content);
    m_buildBox = new QComboBox(cs2Content);
    m_buildBox->addItem(QStringLiteral("Release build"), false);
    m_buildBox->addItem(QStringLiteral("Debug build"), true);
    m_cs2Btn = new QPushButton(QStringLiteral("Inject into CS2"), cs2Content);
    m_cs2Btn->setObjectName(QStringLiteral("Cs2Btn"));
    m_unloadBtn = new QPushButton(QStringLiteral("Unload from CS2"), cs2Content);
    m_unloadBtn->setObjectName(QStringLiteral("UnloadBtn"));
    cs2Layout->addWidget(m_launchBtn);
    cs2Layout->addWidget(m_buildBox);
    cs2Layout->addWidget(m_cs2Btn);
    cs2Layout->addWidget(m_unloadBtn);
    m_cs2Card = new StatusCard(Icons::Kind::Cs2, QStringLiteral("COUNTER-STRIKE 2"), kCs2Accent, cs2Content, this);
    cards->addWidget(m_cs2Card, 1);

    rootLayout->addLayout(cards, 1);

    auto *autoRow = new QHBoxLayout;
    autoRow->setSpacing(20);
    m_autoBox = new QCheckBox(QStringLiteral("Auto inject"), this);
    m_autoSteamBox = new QCheckBox(QStringLiteral("Auto inject Steam"), this);
    m_autoSteamBox->setEnabled(false);
    autoRow->addWidget(m_autoBox);
    autoRow->addWidget(m_autoSteamBox);
    autoRow->addStretch();
    m_statsLabel = new QLabel(this);
    m_statsLabel->setObjectName(QStringLiteral("StatsLabel"));
    autoRow->addWidget(m_statsLabel);
    rootLayout->addLayout(autoRow);

    // VAC / status readout panel
    auto *vacPanel = new QWidget(this);
    vacPanel->setObjectName(QStringLiteral("VacPanel"));
    auto *vacLayout = new QHBoxLayout(vacPanel);
    vacLayout->setContentsMargins(14, 8, 14, 8);
    vacLayout->setSpacing(10);
    auto *vacTitle = new QLabel(QStringLiteral("STATUS"), vacPanel);
    vacTitle->setObjectName(QStringLiteral("VacTitle"));
    vacLayout->addWidget(vacTitle);
    m_vacLog = new QLabel(vacPanel);
    m_vacLog->setObjectName(QStringLiteral("VacRow"));
    m_vacPtrace = new QLabel(vacPanel);
    m_vacPtrace->setObjectName(QStringLiteral("VacRow"));
    m_vacUid = new QLabel(vacPanel);
    m_vacUid->setObjectName(QStringLiteral("VacRow"));
    vacLayout->addWidget(m_vacLog);
    vacLayout->addWidget(m_vacPtrace);
    vacLayout->addWidget(m_vacUid);
    vacLayout->addStretch();

    m_ptraceBox = new QComboBox(vacPanel);
    m_ptraceBox->setToolTip(QStringLiteral(
        "0: any same-uid process may attach\n"
        "1: only descendants (root still attaches)\n"
        "2: root only (CAP_SYS_PTRACE)\n"
        "3: ptrace disabled - BREAKS INJECTION"));
    m_ptraceBox->addItem(QStringLiteral("ptrace 0 · permissive"));
    m_ptraceBox->addItem(QStringLiteral("ptrace 1 · restricted"));
    m_ptraceBox->addItem(QStringLiteral("ptrace 2 · root only"));
    m_ptraceBox->addItem(QStringLiteral("ptrace 3 · off"));
    vacLayout->addWidget(m_ptraceBox);

    m_ptracePersist = new QCheckBox(QStringLiteral("persist"), vacPanel);
    m_ptracePersist->setToolTip(QStringLiteral(
        "Write the scope to /etc/sysctl.d so it survives reboot"));
    vacLayout->addWidget(m_ptracePersist);
    connect(m_ptraceBox, &QComboBox::activated, this, &MainWindow::onPtraceChanged);
    connect(m_ptracePersist, &QCheckBox::toggled, this, &MainWindow::onPtracePersistToggled);

    vacLayout->addStretch();
    m_cleanupBtn = new QPushButton(QStringLiteral("Cleanup"), vacPanel);
    m_cleanupBtn->setObjectName(QStringLiteral("CleanupBtn"));
    m_cleanupBtn->setToolTip(QStringLiteral("Reset /tmp/dumps blocker, remove stale temp copies, audit mapped paths"));
    vacLayout->addWidget(m_cleanupBtn);
    rootLayout->addWidget(vacPanel);

    auto *logHeaderRow = new QHBoxLayout;
    auto *logHeader = new QLabel(QStringLiteral("LOG"), this);
    logHeader->setObjectName(QStringLiteral("LogHeader"));
    logHeaderRow->addWidget(logHeader);
    logHeaderRow->addStretch();
    rootLayout->addLayout(logHeaderRow);

    m_logView = new QPlainTextEdit(this);
    m_logView->setObjectName(QStringLiteral("Log"));
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    m_logView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_logView, &QPlainTextEdit::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Copy all"), this, [this] {
            QApplication::clipboard()->setText(m_logView->toPlainText());
        });
        menu.addAction(QStringLiteral("Save session log…"), this, [this] {
            const QString path = QDir::homePath() + QStringLiteral("/.local/share/NeversneezeLoader");
            QDir().mkpath(path);
            const QString file = path + QStringLiteral("/session-%1.log")
                                     .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
            QFile f(file);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(m_logView->toPlainText().toUtf8());
                log(QStringLiteral("[Loader] session log saved: %1").arg(file), Injector::Level::Ok);
            }
        });
        menu.exec(m_logView->mapToGlobal(pos));
    });
    rootLayout->addWidget(m_logView, 2);

    if (!resolveProjectRoot())
        log(QStringLiteral("Could not locate the Neversneeze project root (build/, build-steam/, Source/)."),
            Injector::Level::Error);

    log(QStringLiteral("[Loader] %1")
            .arg(QFile::exists(QStringLiteral(":/assets/steam.png"))
                     ? QStringLiteral("embedded icons loaded")
                     : QStringLiteral("embedded icons missing - painted fallback")),
        QFile::exists(QStringLiteral(":/assets/steam.png")) ? Injector::Level::Ok : Injector::Level::Warn);

    m_rootPill->setText(Injector::isRoot() ? QStringLiteral("ROOT") : QStringLiteral("NO ROOT"));
    m_rootPill->setObjectName(Injector::isRoot() ? QStringLiteral("RootPillOk") : QStringLiteral("RootPillBad"));
    m_rootPill->setStyleSheet(m_rootPill->styleSheet());

    connect(&m_injector, &Injector::logMessage, this, &MainWindow::onLog);
    connect(&m_injector, &Injector::steamFinished, this, [this](bool ok) {
        m_steamBusy = false;
        m_steamInjected = ok;
        m_steamFailed = !ok;
        if (!ok && m_autoState != AutoState::Off && m_autoSteamBox->isChecked())
            log(QStringLiteral("[Auto] Steam injection failed - continuing with CS2 only"),
                Injector::Level::Warn);
        updateStates();
    });
    connect(&m_injector, &Injector::cs2Finished, this, [this](bool ok) {
        m_cs2Busy = false;
        m_cs2Injected = ok;
        m_cs2Failed = !ok;
        playSound(ok);
        if (ok && m_injectStartedAt.isValid()) {
            const qint64 waited = m_injectStartedAt.msecsTo(QDateTime::currentDateTime());
            log(QStringLiteral("[Loader] injection round took %1s").arg(waited / 1000), Injector::Level::Info);
            m_injectStartedAt = QDateTime();
        }
        if (!ok && m_autoState != AutoState::Off) {
            stopAuto(QStringLiteral("injection failed"));
        } else if (ok && m_autoState != AutoState::Off) {
            m_autoState = AutoState::Watch;
            log(QStringLiteral("[Auto] injected - watching for CS2 restarts..."), Injector::Level::Ok);
        }
        updateStates();
        updateVacPanel();
    });
    connect(m_steamBtn, &QPushButton::clicked, this, &MainWindow::onSteamButton);
    connect(m_cs2Btn, &QPushButton::clicked, this, &MainWindow::onCs2Button);
    connect(m_unloadBtn, &QPushButton::clicked, this, &MainWindow::onUnloadButton);
    connect(m_launchBtn, &QPushButton::clicked, this, &MainWindow::onLaunchCs2);
    connect(m_cleanupBtn, &QPushButton::clicked, this, &MainWindow::onCleanupButton);
    connect(m_autoBox, &QCheckBox::toggled, this, &MainWindow::onAutoToggled);
    connect(m_autoSteamBox, &QCheckBox::toggled, this, &MainWindow::onAutoSteamToggled);

    connect(&m_injector, &Injector::unloadFinished, this, [this](bool ok) {
        m_cs2Busy = false;
        if (ok) {
            m_cs2Injected = false;
            m_cs2Failed = false;
            playSound(true);
        }
        updateStates();
    });

    connect(&m_pollTimer, &QTimer::timeout, this, &MainWindow::poll);
    loadSettings();
    applyThemeAccent();
    refreshAccountPill();
    updateVacPanel();
    m_pollTimer.start(1000);
    poll();

    if (m_autoBox->isChecked()) {
        m_autoState = m_autoSteamBox->isChecked() ? AutoState::WaitSteam : AutoState::WaitCs2;
        m_autoSettle = 0;
        log(QStringLiteral("[Auto] enabled - %1")
                .arg(m_autoState == AutoState::WaitSteam
                         ? QStringLiteral("waiting for Steam...")
                         : QStringLiteral("waiting for CS2...")),
            Injector::Level::Ok);
    }

    if (Injector::isRoot()) {
        log(QStringLiteral("=========================================="));
        log(QStringLiteral("   Unified Injector"));
        log(QStringLiteral("=========================================="));
    } else {
        log(QStringLiteral("Not running as root - injection will fail."), Injector::Level::Error);
        log(QStringLiteral("Start via run.sh (asks for the password with a GUI prompt)."), Injector::Level::Warn);
    }
    startBuildCheck();
}

bool MainWindow::resolveProjectRoot()
{
    QDir dir(QApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("build/Source/libMangoHud.so")))
            || QFileInfo::exists(dir.filePath(QStringLiteral("build/Source/libOsiris.so")))
            || QFileInfo::exists(dir.filePath(QStringLiteral("Source/CMakeLists.txt")))) {
            m_injector.setProjectRoot(dir.absolutePath());
            return true;
        }
        if (!dir.cdUp())
            break;
    }
    return false;
}

void MainWindow::loadSettings()
{
    QSettings settings(QStringLiteral("Neversneeze"), QStringLiteral("Loader"));
    const bool autoInject = settings.value(QStringLiteral("autoInject"), false).toBool();
    const bool autoSteam = settings.value(QStringLiteral("autoInjectSteam"), false).toBool();
    const bool debug = settings.value(QStringLiteral("debugBuild"), false).toBool();

    m_buildBox->setCurrentIndex(debug ? 1 : 0);
    m_autoBox->blockSignals(true);
    m_autoBox->setChecked(autoInject);
    m_autoSteamBox->setEnabled(autoInject);
    m_autoSteamBox->blockSignals(true);
    m_autoSteamBox->setChecked(autoSteam);
    m_autoSteamBox->blockSignals(false);
    m_autoBox->blockSignals(false);
    if (!autoInject)
        m_autoSteamBox->setEnabled(false);
}

void MainWindow::saveSettings()
{
    QSettings settings(QStringLiteral("Neversneeze"), QStringLiteral("Loader"));
    settings.setValue(QStringLiteral("autoInject"), m_autoBox->isChecked());
    settings.setValue(QStringLiteral("autoInjectSteam"), m_autoSteamBox->isChecked());
    settings.setValue(QStringLiteral("debugBuild"), m_buildBox->currentIndex() == 1);
}

void MainWindow::applyThemeAccent()
{
    // follow the in-game menu accent (Menu.Theme.Acccent in default.cfg, RGBA hex)
    QFile cfg(QDir::homePath() + QStringLiteral("/OsirisCS2/configs/default.cfg"));
    QString accentHex = QStringLiteral("#ab46ff");
    if (cfg.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromUtf8(cfg.readAll());
        QRegularExpression re(QStringLiteral("\"Accent\"\\s+(\\d+)"));
        const auto match = re.match(text);
        if (match.hasMatch()) {
            const uint value = match.captured(1).toUInt();
            const QColor c((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF);
            if (c.isValid())
                accentHex = c.name();
        }
    }
    if (accentHex == m_accentHex)
        return;
    m_accentHex = accentHex;
    m_accentColor = QColor(accentHex);

    // re-tint the accent-driven UI bits
    QString qss = styleSheet();
    qss.replace(QStringLiteral("#a3d41f"), m_accentHex);
    qss.replace(QStringLiteral("#ab46ff"), m_accentHex);
    setStyleSheet(qss);
    update();
    log(QStringLiteral("[Loader] theme accent matched to in-game menu: %1").arg(m_accentHex),
        Injector::Level::Info);
}

void MainWindow::updateVacPanel()
{
    const Injector::VacStatus status = Injector::readVacStatus();
    const qint64 hash = status.guiLogSize * 1315423911ULL + status.guiLogAgeMs
        + status.ptraceScope.size() * 31 + status.cs2Uid * 7 + (status.steamAccount.size() << 8);

    if (m_lastVacHash != 0 && hash == m_lastVacHash)
        return;
    m_lastVacHash = hash;

    auto setRow = [this](QLabel *label, const QString &text, const char *objectName) {
        label->setText(text);
        label->setObjectName(QLatin1String(objectName));
        label->setStyleSheet(label->styleSheet()); // re-apply QSS after objectName change
    };

    if (!status.guiLogExists) {
        setRow(m_vacLog, QStringLiteral("gui.log: absent (clean)"), "VacMuted");
    } else if (status.guiLogAgeMs >= 0 && status.guiLogAgeMs < 60 * 1000) {
        setRow(m_vacLog, QStringLiteral("gui.log: activity %1s ago")
                             .arg(status.guiLogAgeMs / 1000), "VacWarn");
    } else {
        setRow(m_vacLog, QStringLiteral("gui.log: silent (%1 KB)")
                             .arg(status.guiLogSize / 1024), "VacOk");
    }

    if (status.ptraceScope.isEmpty())
        setRow(m_vacPtrace, QStringLiteral("ptrace: n/a"), "VacMuted");
    else if (status.ptraceScope == QStringLiteral("3"))
        setRow(m_vacPtrace, QStringLiteral("ptrace: DISABLED - injection will fail"), "VacBad");
    else if (status.ptraceScope == QStringLiteral("0"))
        setRow(m_vacPtrace, QStringLiteral("ptrace: permissive"), "VacWarn");
    else
        setRow(m_vacPtrace, QStringLiteral("ptrace: scope %1 (root ok)").arg(status.ptraceScope), "VacOk");

    // keep the selector in sync with the live value
    if (m_ptraceBox) {
        bool ok = false;
        const int scope = status.ptraceScope.toInt(&ok);
        if (ok && scope >= 0 && scope <= 3 && m_ptraceBox->currentIndex() != scope) {
            QSignalBlocker blocker(m_ptraceBox);
            m_ptraceBox->setCurrentIndex(scope);
        }
        m_ptraceBox->setVisible(!status.ptraceScope.isEmpty());
        m_ptracePersist->setVisible(!status.ptraceScope.isEmpty());
    }

    if (status.cs2Uid < 0)
        setRow(m_vacUid, QStringLiteral("cs2: not running"), "VacMuted");
    else if (status.cs2SameUserAsRoot)
        setRow(m_vacUid, QStringLiteral("cs2: running as root (!)"), "VacBad");
    else
        setRow(m_vacUid, QStringLiteral("cs2: uid %1").arg(status.cs2Uid), "VacOk");

    // header account pill (added next to the ROOT pill)
    if (!status.steamAccount.isEmpty() && m_accountPill) {
        m_accountPill->setText(status.steamAccount);
        m_accountPill->setObjectName(QStringLiteral("AccountPill"));
        m_accountPill->setStyleSheet(m_accountPill->styleSheet());
        m_accountPill->setVisible(true);
    }
}

void MainWindow::refreshAccountPill()
{
    updateVacPanel();
}

void MainWindow::playSound(bool success)
{
    const QString file = success ? QStringLiteral("/usr/share/sounds/freedesktop/stereo/complete.oga")
                                 : QStringLiteral("/usr/share/sounds/freedesktop/stereo/dialog-error.oga");
    if (!QFileInfo::exists(file))
        return;
    QProcess::startDetached(QStringLiteral("paplay"), {file});
}

void MainWindow::onCleanupButton()
{
    QStringList lines;
    const Injector::CleanupReport report = Injector::auditArtifacts(m_cs2Pid, &lines);
    for (const QString &line : lines)
        log(line, report.clean ? Injector::Level::Info : Injector::Level::Warn);

    if (report.clean)
        log(QStringLiteral("[Cleanup] all clear - no crumbs left"), Injector::Level::Ok);
    else if (!report.mapsResidue.isEmpty())
        log(QStringLiteral("[Cleanup] residue still mapped - unload/restart CS2 to clear it"),
            Injector::Level::Error);
}

void MainWindow::onPtraceChanged(int index)
{
    const QString value = QString::number(index);

    if (index == 3) {
        const auto choice = QMessageBox::warning(this, QStringLiteral("Disable ptrace"),
            QStringLiteral("Scope 3 disables ptrace system-wide - the loader, its memfd round and "
                           "every gdb attach will fail until you revert.\n\nSet it anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes) {
            QSignalBlocker blocker(m_ptraceBox);
            m_ptraceBox->setCurrentIndex(Injector::readVacStatus().ptraceScope.toInt());
            return;
        }
    }

    if (!Injector::writePtraceScope(value)) {
        log(QStringLiteral("[Status] could not write ptrace_scope (need root)"), Injector::Level::Error);
    } else {
        log(QStringLiteral("[Status] ptrace_scope set to %1").arg(value), Injector::Level::Ok);
        if (index == 0)
            log(QStringLiteral("[Status] scope 0: every same-uid process can attach to any other"),
                Injector::Level::Warn);
    }
    updateVacPanel();
}

void MainWindow::onPtracePersistToggled(bool on)
{
    const QString confPath = QStringLiteral("/etc/sysctl.d/99-neversneeze-loader.conf");
    if (!on) {
        if (QFile::exists(confPath) && QFile::remove(confPath))
            log(QStringLiteral("[Status] ptrace scope no longer persisted (file removed)"));
        return;
    }

    const int scope = m_ptraceBox ? m_ptraceBox->currentIndex() : -1;
    if (scope < 0 || scope > 3) {
        m_ptracePersist->setChecked(false);
        return;
    }

    QFile conf(confPath);
    if (conf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        conf.write(QStringLiteral("# managed by Neversneeze Loader\n"
                                  "kernel.yama.ptrace_scope = %1\n")
                       .arg(scope)
                       .toUtf8());
        log(QStringLiteral("[Status] ptrace scope %1 persisted to %2").arg(scope).arg(confPath),
            Injector::Level::Ok);
    } else {
        log(QStringLiteral("[Status] could not write %1 (need root)").arg(confPath),
            Injector::Level::Error);
        m_ptracePersist->setChecked(false);
    }
}

void MainWindow::startBuildCheck(){
    m_injector.checkBuild([this](const Injector::BuildState &state) {
        log(QStringLiteral("[Build] Checking modules..."));
        if (!state.steamModuleExists)
            log(QStringLiteral("[Build] Steam module not found"), Injector::Level::Warn);
        if (!state.osirisExists)
            log(QStringLiteral("[Build] Neversneeze module not found"), Injector::Level::Warn);
        if (state.steamStale || state.osirisStale)
            log(QStringLiteral("[Build] Source files are newer than binaries"), Injector::Level::Warn);

        if (!state.needed()) {
            log(QStringLiteral("[Build] Modules up to date"), Injector::Level::Ok);
            return;
        }

        const auto choice = QMessageBox::question(this, QStringLiteral("Rebuild"),
            QStringLiteral("Modules are missing or outdated.\nRebuild them now?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice != QMessageBox::Yes) {
            log(QStringLiteral("[Build] Skipping rebuild"));
            return;
        }
        m_steamBtn->setEnabled(false);
        m_cs2Btn->setEnabled(false);
        m_injector.rebuild([this](bool) { updateStates(); });
    });
}

void MainWindow::poll()
{
    m_steamPid = Injector::findPid(QStringLiteral("steam"));
    m_cs2Pid = Injector::findPid(QStringLiteral("cs2"));

    if (m_cs2Pid && m_cs2Pid != m_lastCs2Pid) {
        m_alreadyInjectedLogged = false;
        if (!m_lastCs2Pid) {
            log(QStringLiteral("[CS2] Found CS2 (PID: %1)").arg(m_cs2Pid), Injector::Level::Ok);
            m_cs2DetectedAt = QDateTime::currentDateTime();
        }
    }
    if (!m_cs2Pid)
        m_cs2DetectedAt = QDateTime();
    m_lastCs2Pid = m_cs2Pid;

    if (m_cs2Pid && !m_cs2Busy) {
        QFile maps(QStringLiteral("/proc/%1/maps").arg(m_cs2Pid));
        if (maps.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromLocal8Bit(maps.readAll());
            const bool mangoHud = text.contains(QStringLiteral("libMangoHud.so"));
            const bool oldLib = text.contains(QStringLiteral("libutil_helper.so"))
                || text.contains(QStringLiteral("libOsiris.so"));
            if ((mangoHud || oldLib) && !m_cs2Injected) {
                m_cs2Injected = true;
                if (!m_alreadyInjectedLogged) {
                    m_alreadyInjectedLogged = true;
                    m_injectionsToday++;
                    if (m_cs2DetectedAt.isValid()) {
                        m_lastLoadWaitMs = m_cs2DetectedAt.msecsTo(QDateTime::currentDateTime());
                        m_statsLabel->setText(QStringLiteral("%1 injections this session · last load wait %2s")
                                                  .arg(m_injectionsToday)
                                                  .arg(m_lastLoadWaitMs / 1000));
                    } else {
                        m_statsLabel->setText(QStringLiteral("%1 injections this session").arg(m_injectionsToday));
                    }
                    log(QStringLiteral("[CS2] cheat library is already mapped in CS2 (%1)")
                            .arg(m_cs2Pid),
                        Injector::Level::Ok);
                }
                updateStates();
                updateVacPanel();
            } else if (!mangoHud && !oldLib && m_cs2Injected) {
                // library unmapped: in-game unload or deferred unmap completed
                m_cs2Injected = false;
                m_alreadyInjectedLogged = false;
                log(QStringLiteral("[CS2] cheat library unmapped from CS2 - ready to inject again"),
                    Injector::Level::Info);
            }
        }
    }

    updateStates();
    autoTick();

    // cheap periodic refreshes (throttled internally): theme accent + vac panel
    static int tick = 0;
    if (++tick % 15 == 0) {
        applyThemeAccent();
        updateVacPanel();
    }
}

void MainWindow::autoTick()
{
    if (m_autoState == AutoState::Off || m_autoState == AutoState::Done)
        return;

    switch (m_autoState) {
    case AutoState::WaitSteam: {
        if (m_steamInjected
            || (m_steamPid && Injector::mapsContain(m_steamPid, QStringLiteral("libSteamModule.so")))) {
            if (!m_steamInjected)
                log(QStringLiteral("[Auto] Steam module already injected - skipping steam"),
                    Injector::Level::Info);
            m_autoState = AutoState::WaitCs2;
            m_autoSettle = 0;
            log(QStringLiteral("[Auto] Waiting for CS2..."));
            return;
        }
        if (!m_steamPid) {
            m_autoSettle = 0;
            return;
        }
        if (++m_autoSettle >= 3) {
            log(QStringLiteral("[Auto] Injecting Steam module (PID: %1)").arg(m_steamPid),
                Injector::Level::Ok);
            m_steamBusy = true;
            m_steamDecided = true;
            updateStates();
            m_injector.injectSteam(m_steamPid);
            m_autoState = AutoState::WaitCs2;
            m_autoSettle = 0;
            log(QStringLiteral("[Auto] Waiting for CS2..."));
        }
        return;
    }
    case AutoState::WaitCs2:
        if (!m_cs2Pid)
            return;
        if (m_cs2Injected) {
            m_autoState = AutoState::Watch;
            log(QStringLiteral("[Auto] CS2 already injected - watching for restarts..."),
                Injector::Level::Ok);
            return;
        }
        m_autoState = AutoState::WaitCs2Ready;
        m_autoSettle = 0;
        m_lastMissing.clear();
        log(QStringLiteral("[Auto] CS2 detected - waiting for the game to finish loading..."));
        return;
    case AutoState::Watch:
        if (!m_cs2Pid) {
            m_autoState = AutoState::WaitCs2;
            m_autoSettle = 0;
            log(QStringLiteral("[Auto] CS2 closed - waiting for it to come back..."));
            return;
        }
        if (!m_cs2Injected) {
            m_autoState = AutoState::WaitCs2Ready;
            m_autoSettle = 0;
            m_lastMissing.clear();
            log(QStringLiteral("[Auto] CS2 restarted - waiting for the game to finish loading..."));
            return;
        }
        return;
    case AutoState::WaitCs2Ready: {
        if (!m_cs2Pid) {
            m_autoState = AutoState::WaitCs2;
            m_autoSettle = 0;
            m_cpuStableMs = 0;
            m_lastCpuJiffies = -1;
            log(QStringLiteral("[Auto] CS2 disappeared - waiting for it to come back..."));
            return;
        }
        if (m_cs2Injected) {
            m_autoState = AutoState::Watch;
            return;
        }
        const QStringList missing = Injector::missingCs2Modules(m_cs2Pid);
        if (!missing.isEmpty()) {
            m_autoSettle = 0;
            m_cpuStableMs = 0;
            m_lastCpuJiffies = -1;
            const QString signature = missing.join(QStringLiteral(", "));
            if (signature != m_lastMissing) {
                m_lastMissing = signature;
                log(QStringLiteral("[Auto] waiting for modules: %1").arg(signature));
            }
            return;
        }

        // Module map alone is not enough (everything maps early in boot; injecting
        // before the main menu is up leaves the menu grayed out). Wait for the
        // CPU to go quiet: CS2 burns a full core while loading, then idles in the
        // main menu. Require ~1.5s of a stable (non-increasing) jiffies counter.
        const qint64 jiffies = Injector::cpuJiffies(m_cs2Pid);
        if (jiffies < 0) {
            m_cpuStableMs = 0;
            return;
        }
        if (m_lastCpuJiffies >= 0 && jiffies != m_lastCpuJiffies) {
            m_cpuStableMs = 0;
            m_autoSettle = 0;
            static bool loggedBusy = false;
            if (!loggedBusy) {
                loggedBusy = true;
                log(QStringLiteral("[Auto] modules mapped - waiting for load CPU burn to settle..."));
            }
        } else {
            m_cpuStableMs += 1000;
        }
        m_lastCpuJiffies = jiffies;

        if (m_cpuStableMs >= 3000 && ++m_autoSettle >= 2) {
            log(QStringLiteral("[Auto] CS2 fully loaded (modules + CPU quiet) - injecting"),
                Injector::Level::Ok);
            m_cs2Busy = true;
            m_cpuStableMs = 0;
            m_lastCpuJiffies = -1;
            updateStates();
            m_injector.injectCs2(m_cs2Pid, m_buildBox->currentData().toBool());
        }
        return;
    }
    default:
        return;
    }
}

void MainWindow::stopAuto(const QString &reason, Injector::Level level)
{
    log(QStringLiteral("[Auto] stopped: %1").arg(reason), level);
    if (m_autoBox) {
        m_autoBox->blockSignals(true);
        m_autoBox->setChecked(false);
        m_autoBox->blockSignals(false);
    }
    if (m_autoSteamBox) {
        m_autoSteamBox->blockSignals(true);
        m_autoSteamBox->setEnabled(false);
        m_autoSteamBox->blockSignals(false);
    }
    m_autoState = AutoState::Off;
}

void MainWindow::onAutoToggled(bool on)
{
    saveSettings();
    if (on) {
        m_autoSteamBox->setEnabled(true);
        m_autoState = m_autoSteamBox->isChecked() ? AutoState::WaitSteam : AutoState::WaitCs2;
        m_autoSettle = 0;
        log(QStringLiteral("[Auto] enabled - %1")
                .arg(m_autoState == AutoState::WaitSteam
                         ? QStringLiteral("waiting for Steam...")
                         : QStringLiteral("waiting for CS2...")),
            Injector::Level::Ok);
    } else {
        const bool wasRunning = m_autoState != AutoState::Off && m_autoState != AutoState::Done;
        m_autoState = AutoState::Off;
        m_autoSteamBox->setEnabled(false);
        if (wasRunning)
            log(QStringLiteral("[Auto] disabled"));
    }
}

void MainWindow::onAutoSteamToggled(bool on)
{
    saveSettings();
    if (m_autoState == AutoState::Off || m_autoState == AutoState::Done)
        return;

    if (on && m_autoState == AutoState::WaitCs2) {
        m_autoState = AutoState::WaitSteam;
        m_autoSettle = 0;
        log(QStringLiteral("[Auto] waiting for Steam..."));
    } else if (!on && m_autoState == AutoState::WaitSteam) {
        m_autoState = AutoState::WaitCs2;
        m_autoSettle = 0;
        log(QStringLiteral("[Auto] skipping steam - waiting for CS2..."));
    }
}

void MainWindow::updateStates()
{
    m_steamCard->setBusy(m_steamBusy);
    if (m_steamInjected)
        m_steamCard->setStatus(Icons::Status::Check);
    else if (m_steamSkipped)
        m_steamCard->setStatus(Icons::Status::Skip);
    else if (m_steamFailed)
        m_steamCard->setStatus(Icons::Status::Cross);
    else
        m_steamCard->setStatus(Icons::Status::Question);

    if (m_steamInjected)
        m_steamCard->setSubtitle(QStringLiteral("Module injected"));
    else if (m_steamSkipped)
        m_steamCard->setSubtitle(QStringLiteral("Skipped"));
    else if (m_steamFailed)
        m_steamCard->setSubtitle(QStringLiteral("Injection failed"));
    else if (m_steamBusy)
        m_steamCard->setSubtitle(QStringLiteral("Injecting..."));
    else if (m_steamPid)
        m_steamCard->setSubtitle(QStringLiteral("Running - PID %1").arg(m_steamPid));
    else
        m_steamCard->setSubtitle(QStringLiteral("Not running"));

    m_steamBtn->setEnabled(m_steamPid && !m_steamBusy && !m_steamDecided);

    if (m_cs2Injected)
        m_cs2Card->setStatus(Icons::Status::Check);
    else if (m_cs2Failed)
        m_cs2Card->setStatus(Icons::Status::Cross);
    else if (m_cs2Pid)
        m_cs2Card->setStatus(Icons::Status::Question);
    else
        m_cs2Card->setStatus(Icons::Status::Cross);
    m_cs2Card->setBusy(m_cs2Busy);

    if (m_cs2Injected)
        m_cs2Card->setSubtitle(QStringLiteral("Injected - Toggle menu: INSERT"));
    else if (m_cs2Busy)
        m_cs2Card->setSubtitle(QStringLiteral("Working..."));
    else if (m_cs2Pid)
        m_cs2Card->setSubtitle(QStringLiteral("Detected - PID %1").arg(m_cs2Pid));
    else
        m_cs2Card->setSubtitle(QStringLiteral("Waiting for CS2..."));

    m_cs2Btn->setEnabled(m_cs2Pid && !m_cs2Busy && !m_cs2Injected && !m_steamBusy);
    m_unloadBtn->setEnabled(m_cs2Pid && !m_cs2Busy && m_cs2Injected);
    m_launchBtn->setEnabled(true);
}

void MainWindow::onLog(const QString &text, int level)
{
    log(text, static_cast<Injector::Level>(level));
}

void MainWindow::log(const QString &text, Injector::Level level)
{
    if (text.isEmpty()) {
        m_logView->appendPlainText(QString());
        return;
    }

    QString color = QStringLiteral("#aaadb8");
    if (level == Injector::Level::Ok)
        color = QStringLiteral("#ab46ff");
    else if (level == Injector::Level::Warn)
        color = QStringLiteral("#f0b429");
    else if (level == Injector::Level::Error)
        color = QStringLiteral("#e5484d");

    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    m_logView->appendHtml(QStringLiteral(
        "<span style='color:#565b63;'>[%1]</span> <span style='color:%2;'>%3</span>")
                              .arg(stamp, color, text.toHtmlEscaped()));
}

void MainWindow::onSteamButton()
{
    if (m_steamDecided)
        return;

    const bool inject = QMessageBox::question(this, QStringLiteral("Steam"),
        QStringLiteral("Inject module into Steam?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
        == QMessageBox::Yes;
    m_steamDecided = true;

    if (!inject) {
        log(QStringLiteral("[Steam] Skipping"));
        m_steamSkipped = true;
        updateStates();
        return;
    }
    if (!m_steamPid) {
        log(QStringLiteral("[Steam] Steam not running"), Injector::Level::Warn);
        m_steamSkipped = true;
        updateStates();
        return;
    }

    log(QStringLiteral("[Steam] Found Steam (PID: %1)").arg(m_steamPid));
    m_steamBusy = true;
    updateStates();
    m_injector.injectSteam(m_steamPid);
}

void MainWindow::onCs2Button()
{
    if (!m_steamDecided || !m_cs2Pid || m_cs2Busy || m_cs2Injected)
        return;

    const bool debug = m_buildBox->currentData().toBool();
    saveSettings();
    m_cs2Busy = true;
    m_injectStartedAt = QDateTime::currentDateTime();
    updateStates();
    m_injector.injectCs2(m_cs2Pid, debug);
}

void MainWindow::onUnloadButton()
{
    if (!m_cs2Pid || m_cs2Busy)
        return;

    if (m_autoState != AutoState::Off && m_autoState != AutoState::Done) {
        m_autoBox->setChecked(false);
        stopAuto(QStringLiteral("manual unload"), Injector::Level::Info);
    }

    m_cs2Busy = true;
    updateStates();
    m_injector.unloadCs2(m_cs2Pid);
}

void MainWindow::onLaunchCs2()
{
    log(QStringLiteral("[CS2] Launching CS2 via Steam..."));
    QProcess::startDetached(QStringLiteral("xdg-open"), {QStringLiteral("steam://run/730")});
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);

    // violet glow band hugging the panel edge, echoing the in-game menu shell glow
    const QRectF panel = centralWidget() ? centralWidget()->geometry() : QRectF();
    if (panel.isEmpty())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor glow(0xab, 0x46, 0xff);
    const int extent = 9;
    for (int i = extent; i >= 1; --i) {
        QColor c = glow;
        c.setAlpha(int(14 * (1.0 - qreal(i) / (extent + 1))));
        p.setPen(QPen(c, 2.2));
        p.setBrush(Qt::NoBrush);
        const qreal grow = i * 1.1;
        p.drawRoundedRect(panel.adjusted(grow, grow, -grow, -grow), 14 + i, 14 + i);
    }
}
