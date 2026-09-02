#include "MainWindow.h"
#include "Icons.h"
#include "StatusCard.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTime>
#include <QVBoxLayout>

namespace {
const QColor kSteamAccent(0x66, 0xc0, 0xf4);
const QColor kCs2Accent(0xe0, 0xa2, 0x3c);
} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("Neversneeze Loader"));
    resize(720, 640);
    setMinimumSize(640, 560);

    setStyleSheet(QStringLiteral(R"(
        #Root { background:#0b0d10; }
        #Card { background:#14171c; border:1px solid #232830; border-radius:14px; }
        #Header { color:#f3f5f7; font-size:19px; font-weight:800; letter-spacing:2px; background:transparent; }
        #HeaderAccent { color:#e0a23c; font-size:19px; font-weight:300; letter-spacing:2px; background:transparent; }
        #LogHeader { color:#5c6672; font-size:11px; font-weight:700; letter-spacing:2px; background:transparent; }
        #RootPillOk { color:#46a758; background:rgba(70,167,88,0.12);
            border:1px solid rgba(70,167,88,0.4); border-radius:10px; padding:3px 12px; font-weight:700; font-size:11px; }
        #RootPillBad { color:#e5484d; background:rgba(229,72,77,0.12);
            border:1px solid rgba(229,72,77,0.4); border-radius:10px; padding:3px 12px; font-weight:700; font-size:11px; }
        QPushButton { background:#1a1f26; color:#dfe4ea; border:1px solid #2a313b;
            border-radius:9px; padding:9px 14px; font-weight:600; font-size:13px; }
        QPushButton:hover { background:#20262e; border-color:#39414d; }
        QPushButton:pressed { background:#171c22; }
        QPushButton:disabled { color:#565e69; background:#14181d; border-color:#1f242c; }
        QPushButton#SteamBtn { background:#1b2838; border-color:#2a4a66; color:#c7e1f8; }
        QPushButton#SteamBtn:hover { background:#20304a; border-color:#3a6a94; }
        QPushButton#SteamBtn:disabled { background:#141a20; border-color:#1e2c38; color:#4e5a64; }
        QPushButton#Cs2Btn { background:#2b2115; border-color:#5c4a22; color:#f2d9a6; }
        QPushButton#Cs2Btn:hover { background:#382a18; border-color:#7d6630; }
        QPushButton#Cs2Btn:disabled { background:#1a1712; border-color:#33291a; color:#5a5244; }
        QComboBox { background:#1a1f26; color:#dfe4ea; border:1px solid #2a313b;
            border-radius:9px; padding:6px 10px; font-size:12px; }
        QComboBox:hover { border-color:#39414d; }
        QComboBox QAbstractItemView { background:#14171c; color:#dfe4ea;
            selection-background-color:#2a313b; border:1px solid #2a313b; }
        QPlainTextEdit#Log { background:#0e1114; border:1px solid #20262e; border-radius:10px;
            color:#aab4c0; font-family:'JetBrains Mono','DejaVu Sans Mono',monospace; font-size:12px; }
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
    m_buildBox = new QComboBox(cs2Content);
    m_buildBox->addItem(QStringLiteral("Release build"), false);
    m_buildBox->addItem(QStringLiteral("Debug build"), true);
    m_cs2Btn = new QPushButton(QStringLiteral("Inject into CS2"), cs2Content);
    m_cs2Btn->setObjectName(QStringLiteral("Cs2Btn"));
    cs2Layout->addWidget(m_buildBox);
    cs2Layout->addWidget(m_cs2Btn);
    m_cs2Card = new StatusCard(Icons::Kind::Cs2, QStringLiteral("COUNTER-STRIKE 2"), kCs2Accent, cs2Content, this);
    cards->addWidget(m_cs2Card, 1);

    rootLayout->addLayout(cards, 1);

    auto *logHeader = new QLabel(QStringLiteral("LOG"), this);
    logHeader->setObjectName(QStringLiteral("LogHeader"));
    rootLayout->addWidget(logHeader);

    m_logView = new QPlainTextEdit(this);
    m_logView->setObjectName(QStringLiteral("Log"));
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    rootLayout->addWidget(m_logView, 2);

    if (!resolveProjectRoot())
        log(QStringLiteral("Could not locate the Neversneeze project root (build/, build-steam/, Source/)."),
            Injector::Level::Error);

    m_rootPill->setText(Injector::isRoot() ? QStringLiteral("ROOT") : QStringLiteral("NO ROOT"));
    m_rootPill->setObjectName(Injector::isRoot() ? QStringLiteral("RootPillOk") : QStringLiteral("RootPillBad"));
    m_rootPill->setStyleSheet(m_rootPill->styleSheet());

    connect(&m_injector, &Injector::logMessage, this, &MainWindow::onLog);
    connect(&m_injector, &Injector::steamFinished, this, [this](bool ok) {
        m_steamBusy = false;
        m_steamInjected = ok;
        m_steamFailed = !ok;
        updateStates();
    });
    connect(&m_injector, &Injector::cs2Finished, this, [this](bool ok) {
        m_cs2Busy = false;
        m_cs2Injected = ok;
        m_cs2Failed = !ok;
        updateStates();
    });
    connect(m_steamBtn, &QPushButton::clicked, this, &MainWindow::onSteamButton);
    connect(m_cs2Btn, &QPushButton::clicked, this, &MainWindow::onCs2Button);

    connect(&m_pollTimer, &QTimer::timeout, this, &MainWindow::poll);
    m_pollTimer.start(1000);
    poll();

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
        if (QFileInfo::exists(dir.filePath(QStringLiteral("build/Source/libOsiris.so")))
            || QFileInfo::exists(dir.filePath(QStringLiteral("Source/CMakeLists.txt")))) {
            m_injector.setProjectRoot(dir.absolutePath());
            return true;
        }
        if (!dir.cdUp())
            break;
    }
    return false;
}

void MainWindow::startBuildCheck()
{
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
        if (!m_lastCs2Pid)
            log(QStringLiteral("[CS2] Found CS2 (PID: %1)").arg(m_cs2Pid), Injector::Level::Ok);
    }
    m_lastCs2Pid = m_cs2Pid;

    if (m_cs2Pid && !m_cs2Injected && !m_cs2Busy) {
        QFile maps(QStringLiteral("/proc/%1/maps").arg(m_cs2Pid));
        if (maps.open(QIODevice::ReadOnly)
            && QString::fromLocal8Bit(maps.readAll()).contains(QStringLiteral("libOsiris.so"))) {
            m_cs2Injected = true;
            if (!m_alreadyInjectedLogged) {
                m_alreadyInjectedLogged = true;
                log(QStringLiteral("[CS2] libOsiris.so is already mapped in CS2 (%1)").arg(m_cs2Pid),
                    Injector::Level::Ok);
            }
        }
    }

    updateStates();
}

void MainWindow::updateStates()
{
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
    else if (m_steamDecided)
        m_cs2Card->setStatus(Icons::Status::Question);
    else
        m_cs2Card->setStatus(Icons::Status::Cross);

    if (m_cs2Injected)
        m_cs2Card->setSubtitle(QStringLiteral("Injected - Toggle menu: INSERT"));
    else if (m_cs2Busy)
        m_cs2Card->setSubtitle(QStringLiteral("Injecting..."));
    else if (!m_steamDecided)
        m_cs2Card->setSubtitle(QStringLiteral("Waiting for Steam step"));
    else if (m_cs2Pid)
        m_cs2Card->setSubtitle(QStringLiteral("Detected - PID %1").arg(m_cs2Pid));
    else
        m_cs2Card->setSubtitle(QStringLiteral("Waiting for CS2..."));

    m_cs2Btn->setEnabled(m_steamDecided && m_cs2Pid && !m_cs2Busy && !m_cs2Injected);
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

    QString color = QStringLiteral("#aab4c0");
    if (level == Injector::Level::Ok)
        color = QStringLiteral("#6fd08a");
    else if (level == Injector::Level::Warn)
        color = QStringLiteral("#f0b429");
    else if (level == Injector::Level::Error)
        color = QStringLiteral("#e5484d");

    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    m_logView->appendHtml(QStringLiteral(
        "<span style='color:#566072;'>[%1]</span> <span style='color:%2;'>%3</span>")
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
    m_cs2Busy = true;
    updateStates();
    m_injector.injectCs2(m_cs2Pid, debug);
}
