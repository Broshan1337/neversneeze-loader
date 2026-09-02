#include "Injector.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <unistd.h>

Injector::Injector(QObject *parent)
    : QObject(parent)
{
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        m_buf += m_proc->readAllStandardOutput();
        flushLines();
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            log(QStringLiteral("Error: failed to start '%1'").arg(m_proc->program()), Level::Error);
    });
    connect(m_proc, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        m_buf += m_proc->readAll();
        flushLines();
        DoneFn done;
        done.swap(m_done);
        const QString out = QString::fromLocal8Bit(m_buf);
        m_buf.clear();
        if (done)
            done(exitCode, out);
    });
}

void Injector::log(const QString &text, Level level)
{
    emit logMessage(text, static_cast<int>(level));
}

void Injector::flushLines()
{
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        const QString text = QString::fromLocal8Bit(line).trimmed();
        if (text.isEmpty())
            continue;
        if (m_logOutput)
            log(text);
    }
}

QString Injector::steamModule() const
{
    return m_root + QStringLiteral("/build-steam/Source/libSteamModule.so");
}

QString Injector::osirisLib(bool debug) const
{
    return m_root + (debug ? QStringLiteral("/build-dbg/Source/libOsiris.so")
                           : QStringLiteral("/build/Source/libOsiris.so"));
}

qint64 Injector::findPid(const QString &name)
{
    qint64 best = 0;
    const QStringList entries = QDir(QStringLiteral("/proc"))
                                    .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 0)
            continue;
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        if (!comm.open(QIODevice::ReadOnly))
            continue;
        if (QString::fromLocal8Bit(comm.readAll()).trimmed() == name && (!best || pid < best))
            best = pid;
    }
    return best;
}

bool Injector::isRoot()
{
    return geteuid() == 0;
}

void Injector::run(const QString &prog, const QStringList &args, DoneFn done, bool logOutput)
{
    if (m_proc->state() != QProcess::NotRunning) {
        log(QStringLiteral("Error: injector busy"), Level::Error);
        return;
    }
    m_done = std::move(done);
    m_logOutput = logOutput;
    m_buf.clear();
    m_proc->start(prog, args);
}

void Injector::checkBuild(const std::function<void(const BuildState &)> &done)
{
    m_state = BuildState{};
    m_state.steamModuleExists = QFileInfo::exists(steamModule());
    m_state.osirisExists = QFileInfo::exists(osirisLib(false));

    auto checkOsiris = [this, done] {
        if (!m_state.osirisExists) {
            done(m_state);
            return;
        }
        run(QStringLiteral("/usr/bin/find"),
            {m_root + QStringLiteral("/Source"), QStringLiteral("-type"), QStringLiteral("f"),
             QStringLiteral("-newer"), osirisLib(false), QStringLiteral("-print"), QStringLiteral("-quit")},
            [this, done](int, const QString &out) {
                m_state.osirisStale = !out.trimmed().isEmpty();
                done(m_state);
            },
            false);
    };

    if (!m_state.steamModuleExists) {
        checkOsiris();
        return;
    }
    run(QStringLiteral("/usr/bin/find"),
        {m_root + QStringLiteral("/Source/SteamModule"), QStringLiteral("-type"), QStringLiteral("f"),
         QStringLiteral("-newer"), steamModule(), QStringLiteral("-print"), QStringLiteral("-quit")},
        [this, checkOsiris](int, const QString &out) {
            m_state.steamStale = !out.trimmed().isEmpty();
            checkOsiris();
        },
        false);
}

void Injector::rebuild(const std::function<void(bool ok)> &done)
{
    log(QStringLiteral("[Build] Building Steam module (32-bit)..."));
    run(QStringLiteral("cmake"),
        {QStringLiteral("--build"), m_root + QStringLiteral("/build-steam"), QStringLiteral("--target"),
         QStringLiteral("SteamModule32")},
        [this, done](int code, const QString &) {
            if (code != 0) {
                log(QStringLiteral("[Build] Steam module build failed"), Level::Error);
                done(false);
                return;
            }
            log(QStringLiteral("[Build] Building Neversneeze (64-bit)..."));
            run(QStringLiteral("cmake"),
                {QStringLiteral("--build"), m_root + QStringLiteral("/build"), QStringLiteral("--target"),
                 QStringLiteral("Neversneeze")},
                [this, done](int code2, const QString &) {
                    if (code2 != 0)
                        log(QStringLiteral("[Build] Neversneeze build failed"), Level::Error);
                    else
                        log(QStringLiteral("[Build] Done"), Level::Ok);
                    done(code2 == 0);
                });
        });
}

void Injector::injectSteam(qint64 pid)
{
    const QString module = steamModule();
    if (!QFileInfo::exists(module)) {
        log(QStringLiteral("[Steam] Error: Module not found"), Level::Error);
        log(QStringLiteral("[Steam] Build it first: cmake --build build-steam --target SteamModule32"),
            Level::Warn);
        emit steamFinished(false);
        return;
    }

    const QString tmp =
        QStringLiteral("/tmp/.X11-unix/.Xauthority-%1").arg(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(QStringLiteral("/tmp/.X11-unix"));
    QFile::remove(tmp);
    QFile::copy(module, tmp);
    QFile::setPermissions(tmp, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup
                                   | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);

    log(QStringLiteral("[Steam] Injecting..."));
    run(QStringLiteral("gdb"),
        {QStringLiteral("-p"), QString::number(pid), QStringLiteral("-n"), QStringLiteral("-q"),
         QStringLiteral("-batch"),
         QStringLiteral("-ex"), QStringLiteral("handle SIGSTOP nostop pass noprint SIGCONT nostop pass noprint"),
         QStringLiteral("-ex"),
         QStringLiteral("call ((void*(*)(const char*, int)) dlopen)(\"%1\", 1)").arg(tmp),
         QStringLiteral("-ex"), QStringLiteral("call ((char*(*)(void)) dlerror)()"),
         QStringLiteral("-ex"), QStringLiteral("detach"), QStringLiteral("-ex"), QStringLiteral("quit")},
        [this, tmp](int exitCode, const QString &) {
            QFile::remove(tmp);
            log(QStringLiteral("[Steam] Done"), Level::Ok);
            emit steamFinished(exitCode == 0);
        });
}

void Injector::injectCs2(qint64 pid, bool debugBuild)
{
    m_targetPid = pid;
    m_debug = debugBuild;
    m_lib = osirisLib(debugBuild);

    if (!QFileInfo::exists(m_lib)) {
        log(QStringLiteral("[CS2] Error: Built library not found at '%1'").arg(m_lib), Level::Error);
        log(QStringLiteral("[CS2] Build it first: cmake --build build --target Neversneeze"), Level::Warn);
        emit cs2Finished(false);
        return;
    }

    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    if (maps.open(QIODevice::ReadOnly)
        && QString::fromLocal8Bit(maps.readAll()).contains(QStringLiteral("libOsiris.so"))) {
        log(QStringLiteral("[CS2] WARNING: libOsiris.so is already mapped in CS2 (%1)").arg(pid), Level::Warn);
        log(QStringLiteral("[CS2] Unload first, or wait for deferred unmap to complete"), Level::Warn);
        emit cs2Finished(false);
        return;
    }

    run(QStringLiteral("/usr/bin/find"),
        {m_root + QStringLiteral("/Source"), QStringLiteral("-type"), QStringLiteral("f"),
         QStringLiteral("("), QStringLiteral("-name"), QStringLiteral("*.h"), QStringLiteral("-o"),
         QStringLiteral("-name"), QStringLiteral("*.cpp"), QStringLiteral(")"), QStringLiteral("-newer"),
         m_lib, QStringLiteral("-print"), QStringLiteral("-quit")},
        [this](int, const QString &out) {
            const QString newer = out.trimmed();
            if (!newer.isEmpty()) {
                log(QStringLiteral("[CS2] WARNING: source is newer than the built library"), Level::Warn);
                log(QStringLiteral("[CS2] first newer file: %1").arg(newer), Level::Warn);
            }

            log(QStringLiteral("[CS2] Injecting: %1").arg(m_lib));
            QFile lib(m_lib);
            if (lib.open(QIODevice::ReadOnly))
                log(QStringLiteral("[CS2] Hash: %1")
                        .arg(QString::fromLatin1(
                            QCryptographicHash::hash(lib.readAll(), QCryptographicHash::Sha256).toHex())),
                    Level::Info);

            run(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral("rm -rf /tmp/dumps; mkdir --mode=000 /tmp/dumps 2>/dev/null || true")},
                [this](int, const QString &) {
                    const QString injector = m_root + QStringLiteral("/inject_memfd");
                    if (!QFileInfo::exists(injector)) {
                        log(QStringLiteral("[CS2] memfd injector binary missing, falling back to GDB..."),
                            Level::Warn);
                        gdbFallback();
                        return;
                    }
                    run(injector, {QString::number(m_targetPid), m_lib}, [this](int exitCode, const QString &) {
                        if (exitCode == 0) {
                            finishCs2();
                            return;
                        }
                        log(QStringLiteral("[CS2] memfd injection failed, falling back to GDB..."), Level::Warn);
                        gdbFallback();
                    });
                },
                false);
        },
        false);
}

void Injector::gdbFallback()
{
    const QString tmp =
        QStringLiteral("/tmp/.font-unix/.fc-cache-%1").arg(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(QStringLiteral("/tmp/.font-unix"));
    QFile::remove(tmp);
    QFile::copy(m_lib, tmp);
    QFile::setPermissions(tmp, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup
                                   | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);

    run(QStringLiteral("gdb"),
        {QStringLiteral("-p"), QString::number(m_targetPid), QStringLiteral("-n"), QStringLiteral("-q"),
         QStringLiteral("-batch"),
         QStringLiteral("-ex"), QStringLiteral("handle SIGSTOP nostop pass noprint SIGCONT nostop pass noprint"),
         QStringLiteral("-ex"), QStringLiteral("call ((void*(*)(char*, int)) dlopen)(\"%1\", 1)").arg(tmp),
         QStringLiteral("-ex"), QStringLiteral("call ((char*(*)(void)) dlerror)()"),
         QStringLiteral("-ex"), QStringLiteral("detach"), QStringLiteral("-ex"), QStringLiteral("quit")},
        [this, tmp](int exitCode, const QString &) {
            QFile::remove(tmp);
            if (exitCode == 0)
                finishCs2();
            else
                emit cs2Finished(false);
        });
}

void Injector::finishCs2()
{
    log(QStringLiteral(""));
    log(QStringLiteral("=========================================="));
    log(QStringLiteral(" Injection complete!"), Level::Ok);
    log(QStringLiteral(" Toggle menu: INSERT"), Level::Ok);
    log(QStringLiteral("=========================================="));
    emit cs2Finished(true);
}
