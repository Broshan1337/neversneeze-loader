#include "Injector.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

#include <sys/stat.h>
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
        if (error == QProcess::FailedToStart) {
            log(QStringLiteral("Error: failed to start '%1'").arg(m_proc->program()), Level::Error);
            DoneFn done;
            done.swap(m_done);
            const QString out = QString::fromLocal8Bit(m_buf);
            m_buf.clear();
            if (done)
                done(-1, out);
        }
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
    return m_root + (debug ? QStringLiteral("/build-dbg/Source/libMangoHud.so")
                           : QStringLiteral("/build/Source/libMangoHud.so"));
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

bool Injector::mapsContain(qint64 pid, const QString &needle)
{
    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    if (!maps.open(QIODevice::ReadOnly))
        return false;
    return QString::fromLocal8Bit(maps.readAll()).contains(needle);
}

QStringList Injector::missingCs2Modules(qint64 pid)
{
    static const QStringList required = {
        QStringLiteral("libclient.so"),
        QStringLiteral("libengine2.so"),
        QStringLiteral("librendersystemvulkan.so"),
        QStringLiteral("libpanorama.so"),
        QStringLiteral("libscenesystem.so"),
        QStringLiteral("libschemasystem.so"),
    };

    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    const QString text = maps.open(QIODevice::ReadOnly)
        ? QString::fromLocal8Bit(maps.readAll())
        : QString();

    QStringList missing;
    for (const QString &module : required) {
        if (!text.contains(module))
            missing.append(module);
    }
    return missing;
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

    auto checkInjected = [this, done] {
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
        checkInjected();
        return;
    }
    run(QStringLiteral("/usr/bin/find"),
        {m_root + QStringLiteral("/Source/SteamModule"), QStringLiteral("-type"), QStringLiteral("f"),
         QStringLiteral("-newer"), steamModule(), QStringLiteral("-print"), QStringLiteral("-quit")},
        [this, checkInjected](int, const QString &out) {
            m_state.steamStale = !out.trimmed().isEmpty();
            checkInjected();
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
    if (maps.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromLocal8Bit(maps.readAll());
        if (text.contains(QStringLiteral("libMangoHud.so"))
            || text.contains(QStringLiteral("libutil_helper.so"))
            || text.contains(QStringLiteral("libOsiris.so"))) {
            log(QStringLiteral("[CS2] WARNING: the cheat library is already mapped in CS2 (%1)").arg(pid),
                Level::Warn);
            log(QStringLiteral("[CS2] Unload first, or wait for deferred unmap to complete"), Level::Warn);
            emit cs2Finished(false);
            return;
        }
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

void Injector::unloadCs2(qint64 pid)
{
    // Find the actual mapped path of the cheat library (memfd injections map
    // /proc/<pid>/fd/N style paths, not the build path), then dlopen it with
    // RTLD_NOLOAD to get its handle and dlclose it repeatedly to drop every
    // reference so the module's deferred-unmap logic can complete.
    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    QString libPath;
    if (maps.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromLocal8Bit(maps.readAll()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (!line.contains(QStringLiteral("libMangoHud.so"))
                && !line.contains(QStringLiteral("libutil_helper.so"))
                && !line.contains(QStringLiteral("libOsiris.so")))
                continue;
            const int spacePos = line.lastIndexOf(QLatin1Char(' '));
            if (spacePos < 0)
                continue;
            libPath = line.mid(spacePos + 1).trimmed();
            if (libPath.startsWith(QLatin1Char('/')))
                break;
            libPath.clear();
        }
    }

    if (libPath.isEmpty()) {
        log(QStringLiteral("[CS2] Library is not mapped in CS2 (%1) - nothing to unload").arg(pid),
            Level::Warn);
        emit unloadFinished(false);
        return;
    }

    log(QStringLiteral("[CS2] Unloading %1 from CS2 (%2)...").arg(libPath).arg(pid));
    run(QStringLiteral("gdb"),
        {QStringLiteral("-p"), QString::number(pid), QStringLiteral("-n"), QStringLiteral("-q"),
         QStringLiteral("-batch"),
         QStringLiteral("-ex"), QStringLiteral("handle SIGSTOP nostop pass noprint SIGCONT nostop pass noprint"),
         QStringLiteral("-ex"),
         QStringLiteral("call ((void*(*)(const char*, int)) dlopen)(\"%1\", 6)").arg(libPath),
         QStringLiteral("-ex"), QStringLiteral("call ((int(*)(void*)) dlclose)($1)"),
         QStringLiteral("-ex"), QStringLiteral("call ((int(*)(void*)) dlclose)($1)"),
         QStringLiteral("-ex"), QStringLiteral("call ((int(*)(void*)) dlclose)($1)"),
         QStringLiteral("-ex"), QStringLiteral("call ((char*(*)(void)) dlerror)()"),
         QStringLiteral("-ex"), QStringLiteral("detach"), QStringLiteral("-ex"), QStringLiteral("quit")},
        [this, pid](int exitCode, const QString &) {
            if (exitCode == 0 && !mapsContain(pid, QStringLiteral("libMangoHud.so"))
                && !mapsContain(pid, QStringLiteral("libutil_helper.so"))
                && !mapsContain(pid, QStringLiteral("libOsiris.so"))) {
                log(QStringLiteral("[CS2] Unloaded"), Level::Ok);
                emit unloadFinished(true);
                return;
            }
            log(QStringLiteral("[CS2] Unload did not complete - the module may still be running "
                               "its deferred unmap, or it has active hooks"),
                Level::Warn);
            emit unloadFinished(false);
        });
}

qint64 Injector::cpuJiffies(qint64 pid)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return -1;
    // comm can contain spaces/parens - parse after the last ')'
    const QString text = QString::fromLocal8Bit(stat.readAll());
    const int close = text.lastIndexOf(QLatin1Char(')'));
    if (close < 0)
        return -1;
    const QStringList fields = text.mid(close + 2).split(QLatin1Char(' '));
    // fields[11]=utime fields[12]=stime (0-based after state, which is fields[0] here)
    if (fields.size() < 13)
        return -1;
    bool okU = false, okS = false;
    const qint64 utime = fields[11].toLongLong(&okU);
    const qint64 stime = fields[12].toLongLong(&okS);
    if (!okU || !okS)
        return -1;
    return utime + stime;
}

Injector::CleanupReport Injector::auditArtifacts(qint64 cs2Pid, QStringList *logLines)
{
    CleanupReport report;
    const auto say = [logLines](const QString &line) {
        if (logLines)
            logLines->append(line);
    };

    // 1. /tmp/dumps must exist with mode 000 (crash-dump blocker)
    struct stat st;
    if (::stat("/tmp/dumps", &st) == 0 && S_ISDIR(st.st_mode)) {
        const mode_t perms = st.st_mode & 0777;
        if (perms != 0) {
            say(QStringLiteral("[Cleanup] /tmp/dumps had permissions %1 - resetting to 000")
                    .arg(QString::number(perms, 8), 3, QLatin1Char('0')));
            if (::chmod("/tmp/dumps", 0) == 0) {
                report.dumpsFixed = true;
                report.dumpsOk = true;
            }
        } else {
            report.dumpsOk = true;
        }
    } else if (::mkdir("/tmp/dumps", 0000) == 0) {
        say(QStringLiteral("[Cleanup] recreated /tmp/dumps (mode 000)"));
        report.dumpsOk = true;
        report.dumpsFixed = true;
    } else {
        say(QStringLiteral("[Cleanup] WARNING: could not ensure /tmp/dumps blocker"));
    }

    // 2. leftover module copies in the spray dirs
    const QDateTime now = QDateTime::currentDateTime();
    const QStringList dirs = {QStringLiteral("/tmp/.X11-unix"), QStringLiteral("/tmp/.font-unix")};
    for (const QString &dir : dirs) {
        QDir d(dir);
        const QStringList entries =
            d.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QString &name : entries) {
            if (!name.startsWith(QStringLiteral(".Xauthority-"))
                && !name.startsWith(QStringLiteral(".fc-cache-")))
                continue;
            const QString full = d.filePath(name);
            const QFileInfo info(full);
            // anything older than 10 minutes is stale debris; fresh files may be an in-flight run
            if (info.lastModified().msecsTo(now) < 10 * 60 * 1000)
                continue;
            if (QFile::remove(full)) {
                report.memfdCopies.append(full);
                say(QStringLiteral("[Cleanup] removed stale temp copy %1").arg(full));
            } else {
                say(QStringLiteral("[Cleanup] WARNING: could not remove %1").arg(full));
            }
        }
    }

    // 3. residue check: suspicious paths in cs2 maps beyond the libs themselves
    if (cs2Pid > 0) {
        QFile maps(QStringLiteral("/proc/%1/maps").arg(cs2Pid));
        if (maps.open(QIODevice::ReadOnly)) {
            const QStringList lines = QString::fromLocal8Bit(maps.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                if (!line.contains(QStringLiteral("/tmp/.X11-unix/"))
                    && !line.contains(QStringLiteral("/tmp/.font-unix/")))
                    continue;
                const int spacePos = line.lastIndexOf(QLatin1Char(' '));
                if (spacePos < 0)
                    continue;
                const QString path = line.mid(spacePos + 1).trimmed();
                if (!path.startsWith(QLatin1Char('/')))
                    continue;
                // the module deletes its own copies on inject; anything still mapped is residue
                if (!report.mapsResidue.contains(path)) {
                    report.mapsResidue.append(path);
                    say(QStringLiteral("[Cleanup] WARNING: still mapped in CS2: %1").arg(path));
                }
            }
        }
    }

    report.clean = report.dumpsOk && report.memfdCopies.isEmpty() && report.mapsResidue.isEmpty();
    return report;
}

Injector::VacStatus Injector::readVacStatus(const QString &steamRoot)
{
    VacStatus status;

    // 1. in-game anomaly log health (silent = healthy)
    QFile guiLog(QStringLiteral("/tmp/gamesense_gui.log"));
    status.guiLogExists = guiLog.exists();
    if (status.guiLogExists) {
        QFileInfo info(guiLog);
        status.guiLogSize = info.size();
        status.guiLogAgeMs = info.lastModified().msecsTo(QDateTime::currentDateTime());
    }

    // 2. ptrace scope
    QFile ptrace(QStringLiteral("/proc/sys/kernel/yama/ptrace_scope"));
    if (ptrace.open(QIODevice::ReadOnly))
        status.ptraceScope = QString::fromLocal8Bit(ptrace.readAll()).trimmed();

    // 3. cs2 owner uid
    const qint64 cs2Pid = findPid(QStringLiteral("cs2"));
    if (cs2Pid > 0) {
        struct stat st;
        if (::stat(QStringLiteral("/proc/%1").arg(cs2Pid).toUtf8().constData(), &st) == 0) {
            status.cs2Uid = st.st_uid;
            status.cs2SameUserAsRoot = (st.st_uid == 0);
        }
    }

    // 4. steam account (most recent login timestamp wins)
    QString root = steamRoot;
    if (root.isEmpty()) {
        const QString localSteam = QDir::homePath() + QStringLiteral("/.local/share/Steam");
        const QString dotSteam = QDir::homePath() + QStringLiteral("/.steam/steam");
        root = QDir(localSteam).exists() ? localSteam : dotSteam;
    }
    QFile loginUsers(root + QStringLiteral("/config/loginusers.vdf"));
    if (loginUsers.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromUtf8(loginUsers.readAll());
        QRegularExpression userRe(QStringLiteral("\"(\\d{17})\"\n\t\\{"));
        QRegularExpression mostRecentRe(QStringLiteral("\"mostrecenttimestamp\"\\s+\"(\\d+)\""));
        // per-account blocks: find each account and its mostrecenttimestamp
        QRegularExpressionMatchIterator it(userRe.globalMatch(text));
        qint64 bestTs = 0;
        QString bestName;
        QRegularExpression nameRe(QStringLiteral("\"AccountName\"\\s+\"([^\"]+)\""));
        QRegularExpression personaRe(QStringLiteral("\"PersonaName\"\\s+\"([^\"]+)\""));
        while (it.hasNext()) {
            const auto match = it.next();
            const QString block = text.mid(match.capturedStart(), 800);
            const auto tsMatch = mostRecentRe.match(block);
            if (!tsMatch.hasMatch())
                continue;
            const qint64 ts = tsMatch.captured(1).toLongLong();
            if (ts <= bestTs)
                continue;
            bestTs = ts;
            const auto persona = personaRe.match(block);
            bestName = persona.hasMatch() ? persona.captured(1) : nameRe.match(block).captured(1);
        }
        if (!bestName.isEmpty())
            status.steamAccount = bestName;
    }

    return status;
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
