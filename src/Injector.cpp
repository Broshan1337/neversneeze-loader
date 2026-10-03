#include "Injector.h"
#include <QThread>
#include <QCoreApplication>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <algorithm>
#include <QProcess>
#include <QVector>

#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cstring>
#include <memory>

#include "AntiDebug.h"
#include "Payloads.h"
#include "SessionTrailer.h"
#include "ObfAnnotations.h"

namespace
{
// Exclusive-ownership wrapper: the underlying QByteArray is never COW-shared (callers use
// it through the shared_ptr), so the deleter's memset actually zeroes the plaintext.
Injector::SecureBytes secureShared(QByteArray &&src)
{
    return Injector::SecureBytes(new QByteArray(std::move(src)), [](QByteArray *b) {
        if (b && b->size() > 0)
            std::memset(b->data(), 0, static_cast<std::size_t>(b->size()));
        delete b;
    });
}
} // namespace

Injector::Injector(QObject *parent)
    : QObject(parent)
{
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray chunk = m_proc->readAllStandardOutput();
        m_buf += chunk;
        // Full-output accumulation: flushLines() CONSUMES m_buf while logging, so the
        // DoneFn's `out` must come from a separate ever-growing buffer (gdbMemfdFallback
        // parses "$N = <fd>" out of the gdb session - it saw only the empty leftover
        // fragment when this fed it m_buf, and the 2026-09-22 steam injection failed
        // despite gdb having created the memfd fine).
        m_fullOutput += chunk;
        flushLines();
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            log(QStringLiteral("Error: failed to start '%1'").arg(m_proc->program()), Level::Error);
            DoneFn done;
            done.swap(m_done);
            const QString out = QString::fromLocal8Bit(m_fullOutput);
            m_buf.clear();
            m_fullOutput.clear();
            if (done)
                done(-1, out);
        }
    });
    connect(m_proc, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        const QByteArray tail = m_proc->readAll();
        m_buf += tail;
        m_fullOutput += tail;
        flushLines();
        DoneFn done;
        done.swap(m_done);
        const QString out = QString::fromLocal8Bit(m_fullOutput);
        m_buf.clear();
        m_fullOutput.clear();
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
    return m_root + QStringLiteral("/cs2/build-steam/Source/libSteamModule.so");
}

QString Injector::osirisLib(bool debug) const
{
    return m_root + (debug ? QStringLiteral("/cs2/build-dbg/Source/libMangoHud.so")
                           : QStringLiteral("/cs2/build/Source/libMangoHud.so"));
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
    m_fullOutput.clear();
    m_proc->start(prog, args);
}

void Injector::runWithStdin(const QString &prog, const QStringList &args, const QByteArray &stdinData,
                            DoneFn done, bool logOutput)
{
    if (m_proc->state() != QProcess::NotRunning) {
        log(QStringLiteral("Error: injector busy"), Level::Error);
        return;
    }
    m_done = std::move(done);
    m_logOutput = logOutput;
    m_buf.clear();
    m_fullOutput.clear();
    // Deep copy (not COW): m_stdinData is zeroized right after the write, so it must own
    // its buffer exclusively.
    m_stdinData = QByteArray(stdinData.constData(), stdinData.size());
    QObject::connect(m_proc, &QProcess::started, this, [this] {
        if (!m_stdinData.isEmpty()) {
            m_proc->write(m_stdinData);
            std::memset(m_stdinData.data(), 0, static_cast<std::size_t>(m_stdinData.size()));
            m_stdinData.clear();
        }
        m_proc->closeWriteChannel();
    }, Qt::SingleShotConnection);
    m_proc->start(prog, args);
}

NS_OBF_FLATTEN void Injector::runWithStdinStream(const QString &prog, const QStringList &args,
                                  payloads::StreamPtr stream, DoneFn done, bool logOutput,
                                  const QString &tag)
{
    if (m_proc->state() != QProcess::NotRunning) {
        log(QStringLiteral("Error: injector busy"), Level::Error);
        return;
    }
    m_done = std::move(done);
    m_logOutput = logOutput;
    m_buf.clear();
    m_fullOutput.clear();
    m_stream = std::move(stream);
    m_streamPending = 0;
    m_streamTrailerQueued = false;
    m_streamTag = tag;
    m_streamHash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);

    // Pump while the child drains (backpressure): never queue more than ~2 chunks ahead,
    // so the plaintext in Qt's write buffer stays at one 64 KB chunk.
    QObject::connect(m_proc, &QProcess::started, this, [this] { pumpStream(); },
        Qt::SingleShotConnection);
    QObject::connect(m_proc, &QProcess::bytesWritten, this, [this](qint64 n) {
        m_streamPending -= n;
        pumpStream();
    });
    m_proc->start(prog, args);
}

NS_OBF_FLATTEN void Injector::pumpStream()
{
    if (!m_stream)
        return;

    // Window: at most one chunk queued beyond what the child has consumed.
    constexpr qint64 kWindow = 96 * 1024;
    while (m_streamPending < kWindow && !m_streamTrailerQueued) {
        QByteArray chunk;
        QString error;
        if (!m_stream->nextChunk(chunk, &error)) {
            if (!error.isEmpty()) {
                log(QStringLiteral("[CS] payload stream failed: %1").arg(error), Level::Error);
                m_proc->kill();
                m_stream.reset();
                return;
            }
            // stream exhausted: the session trailer rides as the final chunk
            chunk = session_trailer::build(QCoreApplication::applicationPid(), m_targetPid);
            m_streamTrailerQueued = true;
        }
        if (chunk.isEmpty())
            continue;
        if (m_streamHash)
            m_streamHash->addData(chunk);
        m_proc->write(chunk);
        m_streamPending += chunk.size();
        std::memset(chunk.data(), 0, static_cast<std::size_t>(chunk.size()));
    }

    if (m_streamTrailerQueued) {
        // Qt drains the buffer, then signals EOF to the child
        m_proc->closeWriteChannel();
        if (m_streamHash) {
            log(QStringLiteral("[%1] payload sha256: %2")
                    .arg(m_streamTag,
                         QString::fromLatin1(m_streamHash->result().toHex())),
                Level::Info);
            m_streamHash.reset();
        }
        m_stream.reset();
    }
}

// inject_memfd location: the project tree's copy when present, otherwise the embedded
// payload extracted to a disguised /tmp path (ship mode - no source tree on the machine).
// The extract lives only for the duration of the injection and is removed after.
NS_OBF_FLATTEN QString Injector::memfdInjector()
{
    const QString treeCopy = m_root + QStringLiteral("/cs2/inject_memfd");
    if (QFileInfo::exists(treeCopy))
        return treeCopy;

    if (!payloads::hasInjector()) {
        log(QStringLiteral("[CS2] inject_memfd not found (no tree, no embedded copy)"), Level::Error);
        return QString();
    }
    QString error;
    const QByteArray bytes = payloads::extractInjector(&error);
    if (bytes.isEmpty()) {
        log(QStringLiteral("[CS] embedded injector extract failed: %1").arg(error), Level::Error);
        return QString();
    }
    const QString path =
        QStringLiteral("/tmp/.font-unix/.fc-inject-%1").arg(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(QStringLiteral("/tmp/.font-unix"));
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size()) {
        log(QStringLiteral("[CS] cannot write embedded injector to %1").arg(path), Level::Error);
        return QString();
    }
    out.close();
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

void Injector::checkBuild(const std::function<void(const BuildState &)> &done)
{
    m_state = BuildState{};
    m_state.steamModuleExists = QFileInfo::exists(steamModule());
    m_state.osirisExists = QFileInfo::exists(osirisLib(false));

    // Ship mode: no source tree (or no local builds) but the payloads are embedded in this
    // binary - the embedded builds ARE the current builds, so nothing is "missing".
    const bool shipMode = (m_root.isEmpty() || !QDir(m_root).exists())
        && (payloads::hasCs2() || payloads::hasTf2() || payloads::hasSteamModule());
    if (shipMode) {
        m_state.steamModuleExists = m_state.steamModuleExists || payloads::hasSteamModule();
        m_state.osirisExists = m_state.osirisExists || payloads::hasCs2();
        done(m_state);
        return;
    }

    auto checkInjected = [this, done] {
        if (!m_state.osirisExists) {
            done(m_state);
            return;
        }
    run(QStringLiteral("/usr/bin/find"),
        {m_root + QStringLiteral("/cs2/Source"), QStringLiteral("-type"), QStringLiteral("f"),
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
        {m_root + QStringLiteral("/cs2/Source/SteamModule"), QStringLiteral("-type"), QStringLiteral("f"),
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
        {QStringLiteral("--build"), m_root + QStringLiteral("/cs2/build-steam"), QStringLiteral("--target"),
         QStringLiteral("SteamModule32")},
        [this, done](int code, const QString &) {
            if (code != 0) {
                log(QStringLiteral("[Build] Steam module build failed"), Level::Error);
                done(false);
                return;
            }
            log(QStringLiteral("[Build] Building Neversnooze (64-bit)..."));
            run(QStringLiteral("cmake"),
                {QStringLiteral("--build"), m_root + QStringLiteral("/cs2/build"), QStringLiteral("--target"),
                 QStringLiteral("Neversnooze")},
                [this, done](int code2, const QString &) {
                    if (code2 != 0)
                        log(QStringLiteral("[Build] Neversnooze build failed"), Level::Error);
                    else
                        log(QStringLiteral("[Build] Done"), Level::Ok);
                    done(code2 == 0);
                });
        });
}

void Injector::injectSteam(qint64 pid)
{
    m_targetPid = pid;

    // Already-mapped guard: the steam module lands in a memfd named libMangoHud.so (the
    // injector's one memfd name for every target class), so that substring in the target's
    // maps = our module is live. Without this, a second click double-injects (two hooked
    // copies in one process - seen live 2026-09-22: fd 122 + fd 135 both mapped in steam).
    if (mapsContain(pid, QStringLiteral("libMangoHud.so"))) {
        log(QStringLiteral("[Steam] the module is already mapped in Steam (%1) - unload or restart Steam first").arg(pid),
            Level::Warn);
        emit steamFinished(false);
        return;
    }

    // Payload bytes: the embedded steam module only when there is no local build (a local
    // build always wins on a dev machine), else the local build itself.

    // Payload bytes: the embedded steam module only when there is no local build (a local
    // build always wins on a dev machine), else the local build itself.
    payloads::StreamPtr stream;
    SecureBytes bytes;
    if (!QFileInfo::exists(steamModule()) && payloads::hasSteamModule()) {
        if (anti_debug::analysisDetected()) {
            log(QStringLiteral("[Steam] refusing to decrypt under a tracer"), Level::Error);
            emit steamFinished(false);
            return;
        }
        QString error;
        stream = payloads::openSteamStream(&error);
        if (!stream) {
            log(QStringLiteral("[Steam] embedded payload rejected: %1").arg(error), Level::Error);
            emit steamFinished(false);
            return;
        }
        log(QStringLiteral("[Steam] Using embedded payload (%1 bytes, signature verified)").arg(stream->plainSize()));
    } else {
        const QString module = steamModule();
        if (!QFileInfo::exists(module)) {
            log(QStringLiteral("[Steam] Error: Module not found"), Level::Error);
            log(QStringLiteral("[Steam] Build it first: cmake --build cs2/build-steam --target SteamModule32"),
                Level::Warn);
            emit steamFinished(false);
            return;
        }
        QFile lib(module);
        if (!lib.open(QIODevice::ReadOnly)) {
            log(QStringLiteral("[Steam] Error: cannot read module"), Level::Error);
            emit steamFinished(false);
            return;
        }
        bytes = secureShared(lib.readAll());
    }

    log(QStringLiteral("[Steam] Injecting..."));
    const QString injector = memfdInjector();
    if (!injector.isEmpty()) {
        // Primary: the same memfd+stdin machinery as the game modules - no plaintext file
        // is ever written to disk (the old gdb flow staged a temp copy for dlopen).
        if (stream) {
            // session trailer rides as the stream's final chunk (pumpStream appends it)
            runWithStdinStream(injector, {QString::number(pid), QStringLiteral("-")}, std::move(stream),
                [this](int exitCode, const QString &) {
                    if (exitCode == 0) {
                        log(QStringLiteral("[Steam] Done"), Level::Ok);
                        emit steamFinished(true);
                        return;
                    }
                    log(QStringLiteral("[Steam] memfd injection failed, falling back to GDB..."), Level::Warn);
                    gdbFallbackSteam();
                }, true, QStringLiteral("Steam"));
        } else {
            bytes->append(session_trailer::build(QCoreApplication::applicationPid(), m_targetPid));
            runWithStdin(injector, {QString::number(pid), QStringLiteral("-")}, *bytes,
                [this, bytes](int exitCode, const QString &) {
                    if (exitCode == 0) {
                        log(QStringLiteral("[Steam] Done"), Level::Ok);
                        emit steamFinished(true);
                        return;
                    }
                    log(QStringLiteral("[Steam] memfd injection failed, falling back to GDB..."), Level::Warn);
                    gdbFallbackSteam();
                });
        }
        return;
    }
    gdbFallbackSteam();
}

// gdb fallback (steam module): the memfd flavor - the module's verifier reads the trailer
// off a /memfd:libMangoHud.so fd, so the bytes must land in an in-target memfd, not a temp
// file (see gdbMemfdFallback).
void Injector::gdbFallbackSteam()
{
    SecureBytes bytes;
    if (!QFileInfo::exists(steamModule()) && payloads::hasSteamModule()) {
        QString error;
        bytes = secureShared(payloads::extractSteamModule(&error));
        if (bytes->isEmpty()) {
            log(QStringLiteral("[Steam] embedded payload rejected: %1").arg(error), Level::Error);
            emit steamFinished(false);
            return;
        }
    } else {
        QFile lib(steamModule());
        if (lib.open(QIODevice::ReadOnly))
            bytes = secureShared(lib.readAll());
    }
    if (bytes->isEmpty()) {
        emit steamFinished(false);
        return;
    }
    bytes->append(session_trailer::build(QCoreApplication::applicationPid(), m_targetPid));
    gdbMemfdFallback(bytes, [this](bool ok) { emit steamFinished(ok); });
}

void Injector::injectCs2(qint64 pid, bool debugBuild)
{
    m_targetPid = pid;
    m_debug = debugBuild;
    m_lib = osirisLib(debugBuild);

    // Embedded-payload path (ship mode): the newest release module is packed inside this
    // binary - decrypt in memory and stream it into the target's memfd via the injector's
    // stdin mode. No plaintext file ever touches disk. A LOCAL build always wins when it
    // exists (the developer wants exactly what they just built); embedded is the fallback
    // for machines without the tree. Debug builds always use the local tree.
    if (!debugBuild && !QFileInfo::exists(m_lib) && payloads::hasCs2()) {
        if (anti_debug::analysisDetected()) {
            log(QStringLiteral("[CS2] refusing to decrypt under a tracer"), Level::Error);
            emit cs2Finished(false);
            return;
        }
        QString error;
        auto stream = payloads::openCs2Stream(&error);
        if (!stream) {
            log(QStringLiteral("[CS2] embedded payload rejected: %1").arg(error), Level::Error);
            emit cs2Finished(false);
            return;
        }

        // STALE-REQUEST CLEANUP (same as the disk path - see below)
        ::unlink(QStringLiteral("/tmp/ns_unload_request").toLocal8Bit().constData());

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

        log(QStringLiteral("[CS2] Injecting embedded payload (%1 bytes, signature verified)").arg(stream->plainSize()));
        QFile lib(m_lib);
        if (lib.open(QIODevice::ReadOnly))
            log(QStringLiteral("[CS2] Local build hash: %1")
                    .arg(QString::fromLatin1(
                        QCryptographicHash::hash(lib.readAll(), QCryptographicHash::Sha256).toHex())),
                Level::Info);

        // Session binding: the heartbeat trailer rides as the stream's final chunk
        // (pumpStream appends it after the decrypted stream is exhausted).
        // (shared_ptr wrapper: std::function targets must be copyable, the stream is not)
        auto streamHolder = std::make_shared<payloads::StreamPtr>(std::move(stream));
        run(QStringLiteral("/bin/sh"),
            {QStringLiteral("-c"),
             QStringLiteral("rm -rf /tmp/dumps; mkdir --mode=000 /tmp/dumps 2>/dev/null || true")},
            [this, streamHolder](int, const QString &) {
                const QString injector = memfdInjector();
                if (injector.isEmpty()) {
                    log(QStringLiteral("[CS2] no injector available, falling back to GDB..."), Level::Warn);
                    gdbFallbackExtracted("cs2", [this](bool) { finishCs2(); });
                    return;
                }
                runWithStdinStream(injector, {QString::number(m_targetPid), QStringLiteral("-")},
                    std::move(*streamHolder),
                    [this](int exitCode, const QString &) {
                        if (exitCode == 0) {
                            finishCs2();
                            return;
                        }
                        log(QStringLiteral("[CS2] memfd injection failed, falling back to GDB..."), Level::Warn);
                        gdbFallbackExtracted("cs2", [this](bool) { finishCs2(); });
                    }, true, QStringLiteral("CS2"));
            },
            false);
        return;
    }

    if (!QFileInfo::exists(m_lib)) {
        log(QStringLiteral("[CS2] Error: Built library not found at '%1'").arg(m_lib), Level::Error);
        log(QStringLiteral("[CS2] Build it first: cmake --build build --target Neversnooze"), Level::Warn);
        emit cs2Finished(false);
        return;
    }

    // STALE-REQUEST CLEANUP: a leftover /tmp/ns_unload_request from a failed unload must be
    // wiped before injecting - the fresh module would otherwise consume it on its first frame
    // and instantly self-unload (seen live 2026-09-12: "I can't inject" - injected, unloaded).
    ::unlink(QStringLiteral("/tmp/ns_unload_request").toLocal8Bit().constData());

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
        {m_root + QStringLiteral("/cs2/Source"), QStringLiteral("-type"), QStringLiteral("f"),
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
                    const QString injector = m_root + QStringLiteral("/cs2/inject_memfd");
                    if (!QFileInfo::exists(injector)) {
                        log(QStringLiteral("[CS2] memfd injector binary missing, falling back to GDB..."),
                            Level::Warn);
                        gdbFallback();
                        return;
                    }
                    // The module verifies the session trailer on EVERY injection - stream the
                    // local build + trailer through stdin (a raw path injection would go inert).
                    QFile src(m_lib);
                    SecureBytes bytes;
                    if (src.open(QIODevice::ReadOnly))
                        bytes = secureShared(src.readAll());
                    if (bytes->isEmpty()) {
                        log(QStringLiteral("[CS2] cannot read the local build"), Level::Error);
                        emit cs2Finished(false);
                        return;
                    }
                    bytes->append(session_trailer::build(QCoreApplication::applicationPid(), m_targetPid));
                    runWithStdin(injector, {QString::number(m_targetPid), QStringLiteral("-")}, *bytes,
                        [this, bytes](int exitCode, const QString &) {
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

// Resolves which fd in the target process refers to the given memfd (readlink scan of
// /proc/<pid>/fd/*). Returns "/proc/<pid>/fd/<n>" or empty.
static QString memfdFdPath(qint64 pid, const QString &memfdName)
{
    const QDir fdDir(QStringLiteral("/proc/%1/fd").arg(pid));
    const QStringList fds = fdDir.entryList(QDir::NoDotAndDotDot);
    for (const QString &fd : fds) {
        const QString target = QFile::symLinkTarget(fdDir.filePath(fd));
        if (target == memfdName)
            return QStringLiteral("/proc/%1/fd/%2").arg(pid).arg(fd);
    }
    return QString();
}

void Injector::unloadCs2(qint64 pid)
{
    // Primary path (2026-09-12): a request FILE. The module's VAC hardening unlinked its
    // link_map node, so the old dlopen(RTLD_NOLOAD)+dlclose gdb dance can no longer reach it
    // (NOLOAD consults the link_map; an unlinked module is invisible to it). The module instead
    // polls /tmp/ns_unload_request on its present thread and runs its own teardown + deferred
    // unmap. Wait for the maps to clear, then fall back to the gdb dlclose for pre-unlink builds.
    const QString requestFile = QStringLiteral("/tmp/ns_unload_request");
    {
        QFile request(requestFile);
        if (!request.open(QIODeviceBase::WriteOnly | QIODeviceBase::Truncate)) {
            log(QStringLiteral("[CS2] Failed to write unload request file"), Level::Warn);
            emit unloadFinished(false);
            return;
        }
        request.write("unload\n");
    }

    log(QStringLiteral("[CS2] Unload requested - waiting for the module to unmap..."), Level::Info);
    for (int attempt = 0; attempt < 24; ++attempt) {
        ::QThread::msleep(500);
        if (!mapsContain(pid, QStringLiteral("libMangoHud.so"))
            && !mapsContain(pid, QStringLiteral("libutil_helper.so"))
            && !mapsContain(pid, QStringLiteral("libOsiris.so"))) {
            log(QStringLiteral("[CS2] Unloaded"), Level::Ok);
            emit unloadFinished(true);
            return;
        }
    }
    log(QStringLiteral("[CS2] Module still mapped after the request file - trying the gdb dlclose fallback"), Level::Warn);
    unloadCs2Legacy(pid);
}

// Legacy path (pre-unlink builds): find the mapped path, dlopen(RTLD_NOLOAD) for the handle,
// dlclose repeatedly so the module's deferred-unmap logic can complete.
void Injector::unloadCs2Legacy(qint64 pid)
{
    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    QString libPath;
    if (maps.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromLocal8Bit(maps.readAll()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (!line.contains(QStringLiteral("libMangoHud.so"))
                && !line.contains(QStringLiteral("libutil_helper.so"))
                && !line.contains(QStringLiteral("libOsiris.so")))
                continue;
            // memfd mappings end with "(deleted)" - strip it: the memfd file is still open via
            // /proc/<pid>/fd/<n>, and the memfd name itself is openable as /proc/<pid>/fd/<n>.
            QString path = line.mid(line.lastIndexOf(QLatin1Char(' ')) + 1).trimmed();
            if (path == QStringLiteral("(deleted)")) {
                path = line.mid(line.lastIndexOf(QStringLiteral("/memfd:")));
                const int space = path.indexOf(QLatin1Char(' '));
                if (space >= 0)
                    path.truncate(space);
                // resolve which fd the game holds for this memfd
                const QString fdPath = memfdFdPath(pid, path);
                if (!fdPath.isEmpty()) {
                    libPath = fdPath;
                    break;
                }
                continue;
            }
            if (path.startsWith(QLatin1Char('/'))) {
                libPath = path;
                break;
            }
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

qint64 Injector::cpuJiffies(qint64 pid){
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

bool Injector::writePtraceScope(const QString &value)
{
    QFile proc(QStringLiteral("/proc/sys/kernel/yama/ptrace_scope"));
    if (!proc.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    return proc.write((value + QLatin1Char('\n')).toUtf8()) > 0;
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
    // memfd flavor (see gdbMemfdFallback): the local build + trailer straight into an
    // in-target memfd - the module verifies the trailer off the memfd either way.
    QFile src(m_lib);
    SecureBytes bytes;
    if (src.open(QIODevice::ReadOnly))
        bytes = secureShared(src.readAll());
    if (bytes->isEmpty()) {
        log(QStringLiteral("[CS2] cannot read the local build"), Level::Error);
        emit cs2Finished(false);
        return;
    }
    bytes->append(session_trailer::build(QCoreApplication::applicationPid(), m_targetPid));
    gdbMemfdFallback(bytes, [this](bool ok) {
        if (ok)
            finishCs2();
        else
            emit cs2Finished(false);
    });
}

// gdb fallback for the streamed-payload paths: the stream was (partially) consumed by the
// failed memfd attempt, so re-extract the whole buffer, stamp it and run the memfd-flavored
// gdb fallback (see gdbMemfdFallback).
void Injector::gdbFallbackExtracted(const char *game, std::function<void(bool)> done)
{
    QString error;
    SecureBytes bytes;
    if (std::strcmp(game, "cs2") == 0)
        bytes = secureShared(payloads::extractCs2(&error));
    else if (std::strcmp(game, "tf2") == 0)
        bytes = secureShared(payloads::extractTf2(&error));
    else
        bytes = secureShared(payloads::extractSteamModule(&error));
    if (bytes->isEmpty()) {
        log(QStringLiteral("[CS] gdb fallback extract rejected: %1").arg(error), Level::Error);
        done(false);
        return;
    }
    bytes->append(session_trailer::build(QCoreApplication::applicationPid(), m_targetPid));
    gdbMemfdFallback(bytes, std::move(done));
}

// Memfd-flavored gdb fallback: two gdb rounds around an in-target memfd.
//   round 1: memfd_create("libMangoHud.so", 0) on the target thread (glibc wrapper - arch
//            agnostic, exported since glibc 2.27)
//   loader:  write the stamped module bytes straight into /proc/<pid>/fd/<n> - no plaintext
//            temp file on disk anymore (the last such step in the whole pipeline is gone)
//   round 2: dlopen("/proc/self/fd/<n>", RTLD_LAZY) - the TARGET's own fd, so the path
//            resolves inside the target's mount namespace (pressure-vessel /tmp is a
//            different filesystem: the old plain-file temp copy dlopen'd to NULL there, and
//            even when it mapped, the module's session verifier rejected the non-memfd
//            backing and self-released on the first present frame - the friend-machine
//            inject->unmap auto-reinject loop)
// The memfd stays in the target's fd table after detach, exactly like the primary path.
void Injector::gdbMemfdFallback(const SecureBytes &stampedBytes, std::function<void(bool)> done)
{
    if (stampedBytes->isEmpty() || m_targetPid <= 0) {
        done(false);
        return;
    }
    const QString handleLine =
        QStringLiteral("handle SIGSTOP nostop pass noprint SIGCONT nostop pass noprint");
    log(QStringLiteral("[CS] gdb fallback: memfd_create in the target..."));
    run(QStringLiteral("gdb"),
        {QStringLiteral("-p"), QString::number(m_targetPid), QStringLiteral("-n"), QStringLiteral("-q"),
         QStringLiteral("-batch"),
         QStringLiteral("-ex"), handleLine,
         QStringLiteral("-ex"),
         QStringLiteral("call ((int(*)(const char*, unsigned int)) memfd_create)(\"libMangoHud.so\", 0)"),
         QStringLiteral("-ex"), QStringLiteral("detach"), QStringLiteral("-ex"), QStringLiteral("quit")},
        [this, stampedBytes, done, handleLine](int exitCode, const QString &out) {
            const auto match = QRegularExpression(QStringLiteral("\\$\\d+ = (\\d+)")).match(out);
            bool ok = false;
            const qint64 fd = match.hasMatch() ? match.captured(1).toLongLong(&ok) : -1;
            if (exitCode != 0 || !ok || fd < 0) {
                log(QStringLiteral("[CS] gdb could not create the target memfd (exit %1, output: %2)")
                        .arg(exitCode)
                        .arg(out.trimmed().left(200)), Level::Error);
                done(false);
                return;
            }
            const QString fdPath = QStringLiteral("/proc/%1/fd/%2").arg(m_targetPid).arg(fd);
            QFile memfd(fdPath);
            if (!memfd.open(QIODevice::WriteOnly) || memfd.write(*stampedBytes) != stampedBytes->size()) {
                log(QStringLiteral("[CS] cannot write the module into the target's memfd (%1)").arg(fdPath), Level::Error);
                done(false);
                return;
            }
            memfd.close();
            log(QStringLiteral("[CS] wrote %1 bytes into the target's memfd (fd %2)")
                    .arg(stampedBytes->size()).arg(fd), Level::Info);

            run(QStringLiteral("gdb"),
                {QStringLiteral("-p"), QString::number(m_targetPid), QStringLiteral("-n"), QStringLiteral("-q"),
                 QStringLiteral("-batch"),
                 QStringLiteral("-ex"), handleLine,
                 QStringLiteral("-ex"),
                 QStringLiteral("call ((void*(*)(const char*, int)) dlopen)(\"/proc/self/fd/%1\", 1)").arg(fd),
                 QStringLiteral("-ex"), QStringLiteral("call ((char*(*)(void)) dlerror)()"),
                 QStringLiteral("-ex"), QStringLiteral("detach"), QStringLiteral("-ex"), QStringLiteral("quit")},
                [handleLine, done](int dlopenExit, const QString &) {
                    done(dlopenExit == 0);
                });
        }, true);
}

// ---- module-integrity watchdog (hardening 2026-09-24) ------------------------------

static quint64 watchdogFnv1a(const QByteArray &data)
{
    quint64 hash = 0xcbf29ce484222325ULL;
    for (unsigned char c : data) {
        hash ^= static_cast<quint64>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Discovers the memfd-backed executable ranges of the module in the target from ITS maps
// (the session-visible "/memfd:libMangoHud.so (deleted)" r-xp lines). Returns false when
// the module is not mapped (unloaded / game gone).
static bool watchdogTextRanges(qint64 pid, QVector<QPair<quint64, quint64>> *ranges)
{
    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    if (!maps.open(QIODevice::ReadOnly))
        return false;
    const QRegularExpression lineRe(
        QStringLiteral("^([0-9a-f]+)-([0-9a-f]+) (....) .* (/memfd:libMangoHud\.so.*)$"));
    for (const QString &line : QString::fromLocal8Bit(maps.readAll()).split(QLatin1Char('\n'))) {
        const auto match = lineRe.match(line);
        if (!match.hasMatch())
            continue;
        const QString perms = match.captured(3);
        if (!perms.contains(QLatin1Char('x')))
            continue;
        bool okStart = false, okEnd = false;
        const quint64 start = match.captured(1).toULongLong(&okStart, 16);
        const quint64 end = match.captured(2).toULongLong(&okEnd, 16);
        if (okStart && okEnd && end > start)
            ranges->append({start, end});
    }
    return !ranges->isEmpty();
}

NS_OBF_FLATTEN void Injector::startIntegrityWatch()
{
    if (m_targetPid <= 0)
        return;
    m_watchPid = m_targetPid;
    m_integrityBaseline = 0;
    m_integrityStrikes = 0;
    QObject::disconnect(&m_integrityTimer, nullptr, nullptr, nullptr);
    QObject::connect(&m_integrityTimer, &QTimer::timeout, this, [this] { integrityTick(); });
    m_integrityTimer.start(60'000);
    log(QStringLiteral("[CS] integrity watchdog armed (pid %1)").arg(m_watchPid), Level::Info);
}

NS_OBF_FLATTEN void Injector::integrityTick()
{
    if (m_watchPid <= 0)
        return;
    if (::kill(m_watchPid, 0) != 0) {
        // the game is gone - nothing to watch
        m_watchPid = 0;
        m_integrityTimer.stop();
        return;
    }
    QVector<QPair<quint64, quint64>> ranges;
    if (!watchdogTextRanges(m_watchPid, &ranges)) {
        // module unmapped (menu unload / self-release) - nothing to watch
        m_watchPid = 0;
        m_integrityTimer.stop();
        return;
    }

    const int memFd = ::open(QStringLiteral("/proc/%1/mem").arg(m_watchPid).toLocal8Bit().constData(), O_RDONLY);
    if (memFd < 0) {
        log(QStringLiteral("[CS] integrity watchdog: cannot open /proc/%1/mem").arg(m_watchPid), Level::Warn);
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    bool readError = false;
    for (const auto &range : ranges) {
        for (quint64 offset = range.first; offset < range.second && !readError; ) {
            char buffer[256 * 1024];
            const quint64 chunk = std::min<quint64>(sizeof(buffer), range.second - offset);
            const auto got = ::pread(memFd, buffer, static_cast<std::size_t>(chunk),
                                     static_cast<off_t>(offset));
            if (got <= 0) {
                readError = true;
                break;
            }
            hash.addData(buffer, static_cast<int>(got));
            offset += static_cast<quint64>(got);
        }
    }
    ::close(memFd);
    if (readError) {
        // transient (module mid-unmap) - not a drift signal
        return;
    }

    const QByteArray digest = hash.result();
    if (m_integrityBaseline == 0) {
        m_integrityBaseline = watchdogFnv1a(digest);
        return;
    }
    if (watchdogFnv1a(digest) == m_integrityBaseline) {
        m_integrityStrikes = 0;
        return;
    }
    // two consecutive drifts = real tamper (a single transient read hiccup is filtered above)
    if (++m_integrityStrikes < 2)
        return;
    m_integrityStrikes = 0;
    log(QStringLiteral("[CS] integrity watchdog: MODULE TEXT DRIFT in the target - requesting unload"), Level::Error);
    QFile request(QStringLiteral("/tmp/ns_unload_request"));
    if (request.open(QIODeviceBase::WriteOnly | QIODeviceBase::Truncate)) {
        request.write("unload\n");
        request.close();
    }
}

void Injector::finishCs2()
{
    log(QStringLiteral(""));
    log(QStringLiteral("=========================================="));
    log(QStringLiteral(" Injection complete!"), Level::Ok);
    log(QStringLiteral("=========================================="));
    startIntegrityWatch();
    emit cs2Finished(true);
}

QString Injector::tf2Lib(bool debug) const
{
    return m_root + (debug ? QStringLiteral("/tf2/build-dbg/Source/libMangoHud.so")
                           : QStringLiteral("/tf2/build/Source/libMangoHud.so"));
}

void Injector::injectTf2(qint64 pid)
{
    m_targetPid = pid;
    m_debug = false;
    m_lib = tf2Lib(false);

    // Embedded-payload path (ship mode) - same scheme as the CS2 branch; a local build
    // always wins when one exists.
    if (!QFileInfo::exists(m_lib) && payloads::hasTf2()) {
        if (anti_debug::analysisDetected()) {
            log(QStringLiteral("[TF2] refusing to decrypt under a tracer"), Level::Error);
            emit tf2Finished(false);
            return;
        }
        QString error;
        auto stream = payloads::openTf2Stream(&error);
        if (!stream) {
            log(QStringLiteral("[TF2] embedded payload rejected: %1").arg(error), Level::Error);
            emit tf2Finished(false);
            return;
        }

        QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
        if (maps.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromLocal8Bit(maps.readAll());
            if (text.contains(QStringLiteral("libMangoHud.so"))) {
                log(QStringLiteral("[TF2] WARNING: the module is already mapped in TF2 (%1)").arg(pid),
                    Level::Warn);
                log(QStringLiteral("[TF2] Unload first"), Level::Warn);
                emit tf2Finished(false);
                return;
            }
        }

        log(QStringLiteral("[TF2] Injecting embedded payload (%1 bytes, signature verified)").arg(stream->plainSize()));
        const QString injector = memfdInjector();
        if (injector.isEmpty()) {
            log(QStringLiteral("[TF2] no injector available"), Level::Error);
            emit tf2Finished(false);
            return;
        }
        // session trailer rides as the stream's final chunk (pumpStream appends it)
        runWithStdinStream(injector, {QString::number(pid), QStringLiteral("-")}, std::move(stream),
            [this](int exitCode, const QString &) {
                if (exitCode != 0) {
                    log(QStringLiteral("[TF2] memfd injection failed"), Level::Error);
                    emit tf2Finished(false);
                    return;
                }
                finishTf2();
            }, true, QStringLiteral("TF2"));
        return;
    }

    if (!QFileInfo::exists(m_lib)) {
        log(QStringLiteral("[TF2] Error: Built library not found at '%1'").arg(m_lib), Level::Error);
        log(QStringLiteral("[TF2] Build it first: cmake --build tf2/build --target NeversnoozeTF2"),
            Level::Warn);
        emit tf2Finished(false);
        return;
    }

    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    if (maps.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromLocal8Bit(maps.readAll());
        if (text.contains(QStringLiteral("libMangoHud.so"))) {
            log(QStringLiteral("[TF2] WARNING: the module is already mapped in TF2 (%1)").arg(pid),
                Level::Warn);
            log(QStringLiteral("[TF2] Unload first"), Level::Warn);
            emit tf2Finished(false);
            return;
        }
    }

    run(QStringLiteral("/usr/bin/find"),
        {m_root + QStringLiteral("/tf2/Source"), QStringLiteral("-type"), QStringLiteral("f"),
         QStringLiteral("("), QStringLiteral("-name"), QStringLiteral("*.h"), QStringLiteral("-o"),
         QStringLiteral("-name"), QStringLiteral("*.cpp"), QStringLiteral(")"), QStringLiteral("-newer"),
         m_lib, QStringLiteral("-print"), QStringLiteral("-quit")},
        [this, pid](int, const QString &out) {
            const QString newer = out.trimmed();
            if (!newer.isEmpty()) {
                log(QStringLiteral("[TF2] WARNING: source is newer than the built library"), Level::Warn);
                log(QStringLiteral("[TF2] first newer file: %1").arg(newer), Level::Warn);
            }

            log(QStringLiteral("[TF2] Injecting: %1").arg(m_lib));
            QFile lib(m_lib);
            if (lib.open(QIODevice::ReadOnly))
                log(QStringLiteral("[TF2] Hash: %1")
                        .arg(QString::fromLatin1(
                            QCryptographicHash::hash(lib.readAll(), QCryptographicHash::Sha256).toHex())),
                    Level::Info);

            // The memfd injector is game-agnostic (PID + library path); the cs2/ tree owns it.
            const QString injector = m_root + QStringLiteral("/cs2/inject_memfd");
            if (!QFileInfo::exists(injector)) {
                log(QStringLiteral("[TF2] memfd injector binary missing, falling back to GDB..."),
                    Level::Warn);
                gdbFallbackTf2();
                return;
            }
            // Session trailer on every injection (a raw path injection would go inert).
            QFile src(m_lib);
            SecureBytes bytes;
            if (src.open(QIODevice::ReadOnly))
                bytes = secureShared(src.readAll());
            if (bytes->isEmpty()) {
                log(QStringLiteral("[TF2] cannot read the local build"), Level::Error);
                emit tf2Finished(false);
                return;
            }
            bytes->append(session_trailer::build(QCoreApplication::applicationPid(), m_targetPid));
            runWithStdin(injector, {QString::number(pid), QStringLiteral("-")}, *bytes,
                [this, bytes](int exitCode, const QString &) {
                    if (exitCode != 0) {
                        log(QStringLiteral("[TF2] memfd injection failed, falling back to GDB..."), Level::Warn);
                        gdbFallbackTf2();
                        return;
                    }
                    finishTf2();
                });
        },
        false);
}

void Injector::gdbFallbackTf2()
{
    // memfd flavor (see gdbMemfdFallback): the local build + trailer straight into an
    // in-target memfd - the TF2 verifier reads the trailer off the memfd either way.
    QFile src(m_lib);
    SecureBytes bytes;
    if (src.open(QIODevice::ReadOnly))
        bytes = secureShared(src.readAll());
    if (bytes->isEmpty()) {
        log(QStringLiteral("[TF2] cannot read the local build"), Level::Error);
        emit tf2Finished(false);
        return;
    }
    bytes->append(session_trailer::build(QCoreApplication::applicationPid(), m_targetPid));
    gdbMemfdFallback(bytes, [this](bool ok) {
        if (ok)
            finishTf2();
        else
            emit tf2Finished(false);
    });
}

void Injector::finishTf2()
{
    log(QStringLiteral(""));
    log(QStringLiteral("=========================================="));
    log(QStringLiteral(" TF2 injection complete!"), Level::Ok);
    log(QStringLiteral("=========================================="));
    emit tf2Finished(true);
}

void Injector::unloadTf2(qint64 pid)
{
    // TF2 module does not hide its link_map (that hardening is CS2-specific), so the
    // dlopen(RTLD_NOLOAD)+dlclose gdb dance works directly. The library was injected via
    // memfd, so the on-disk path is NOT what is mapped - resolve the real mapped path
    // from /proc/<pid>/maps (memfd entries resolve through /proc/<pid>/fd/<n>).
    if (!Injector::mapsContain(pid, QStringLiteral("libMangoHud.so"))) {
        log(QStringLiteral("[TF2] Module is not mapped in TF2 (%1) - nothing to unload").arg(pid),
            Level::Warn);
        emit unloadFinished(false);
        return;
    }

    QString libPath;
    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    if (maps.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromLocal8Bit(maps.readAll()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (!line.contains(QStringLiteral("libMangoHud.so")))
                continue;
            QString path = line.mid(line.lastIndexOf(QLatin1Char(' ')) + 1).trimmed();
            if (path == QStringLiteral("(deleted)")) {
                path = line.mid(line.lastIndexOf(QStringLiteral("/memfd:")));
                const int space = path.indexOf(QLatin1Char(' '));
                if (space >= 0)
                    path.truncate(space);
                const QString fdPath = memfdFdPath(pid, path);
                if (!fdPath.isEmpty()) {
                    libPath = fdPath;
                    break;
                }
                continue;
            }
            if (path.startsWith(QLatin1Char('/'))) {
                libPath = path;
                break;
            }
        }
    }

    if (libPath.isEmpty()) {
        log(QStringLiteral("[TF2] Could not resolve the mapped module path in TF2 (%1)").arg(pid),
            Level::Warn);
        emit unloadFinished(false);
        return;
    }

    log(QStringLiteral("[TF2] Unloading from TF2 (%1)...").arg(pid));
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
            if (exitCode == 0 && !mapsContain(pid, QStringLiteral("libMangoHud.so"))) {
                log(QStringLiteral("[TF2] Unloaded"), Level::Ok);
                emit unloadFinished(true);
                return;
            }
            log(QStringLiteral("[TF2] Unload did not complete - the module may have active hooks"),
                Level::Warn);
            emit unloadFinished(false);
        });
}

// ---- ANY PROCESS (experimental) ------------------------------------------------------------

// Injectable processes: every /proc entry with a resolvable exe (kernel threads and zombies
// have none), minus the loader itself. The name is the exe's basename - what the user expects
// to see for picking a game.
QList<Injector::ProcessEntry> Injector::listInjectableProcesses()
{
    QList<ProcessEntry> out;
    const qint64 self = QCoreApplication::applicationPid();
    const QString selfExe = QFile::symLinkTarget(QStringLiteral("/proc/self/exe"));

    QDir proc(QStringLiteral("/proc"));
    const QStringList ids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : ids) {
        bool ok = false;
        const qint64 pid = id.toLongLong(&ok);
        if (!ok || pid <= 0 || pid == self)
            continue;
        const QString exe = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
        if (exe.isEmpty() || exe == selfExe)
            continue;
        out.append({pid, QFileInfo(exe).fileName()});
    }
    std::sort(out.begin(), out.end(), [](const ProcessEntry &a, const ProcessEntry &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0 || (a.name == b.name && a.pid < b.pid);
    });
    return out;
}

void Injector::injectAny(qint64 pid, const QString &libPath)
{
    m_targetPid = pid;
    m_lib = libPath;

    if (!QFileInfo::exists(m_lib)) {
        log(QStringLiteral("[ANY] Error: library not found at '%1'").arg(m_lib), Level::Error);
        emit anyFinished(false);
        return;
    }

    // stale unload request would instantly self-unload a fresh injection (2026-09-12 lesson)
    ::unlink(QStringLiteral("/tmp/ns_unload_request").toLocal8Bit().constData());

    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    if (maps.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromLocal8Bit(maps.readAll());
        if (text.contains(QStringLiteral("libMangoHud.so"))
            || text.contains(QStringLiteral("libutil_helper.so"))
            || text.contains(QStringLiteral("libOsiris.so"))) {
            log(QStringLiteral("[ANY] WARNING: one of our libraries is already mapped in %1 - unload first").arg(pid),
                Level::Warn);
            emit anyFinished(false);
            return;
        }
    }

    log(QStringLiteral("[ANY] Injecting into pid %1: %2").arg(pid).arg(m_lib));
    QFile lib(m_lib);
    if (lib.open(QIODevice::ReadOnly))
        log(QStringLiteral("[ANY] Hash: %1")
                .arg(QString::fromLatin1(
                    QCryptographicHash::hash(lib.readAll(), QCryptographicHash::Sha256).toHex())),
            Level::Info);

    const QString injector = m_root + QStringLiteral("/cs2/inject_memfd");
    if (!QFileInfo::exists(injector)) {
        log(QStringLiteral("[ANY] Error: memfd injector missing at '%1'").arg(injector), Level::Error);
        emit anyFinished(false);
        return;
    }

    run(injector, {QString::number(pid), m_lib}, [this](int exitCode, const QString &) {
        if (exitCode == 0)
            log(QStringLiteral("[ANY] Injection complete"), Level::Ok);
        else
            log(QStringLiteral("[ANY] Injection failed (exit %1)").arg(exitCode), Level::Error);
        emit anyFinished(exitCode == 0);
    });
}

// ---- PROTON (experimental) -----------------------------------------------------------------

// Reads a NUL-separated /proc file (cmdline/environ) into a token list.
static QStringList procNullTokens(qint64 pid, const char *file)
{
    QStringList out;
    QFile f(QStringLiteral("/proc/%1/%2").arg(pid).arg(file));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray all = f.readAll();
    for (const QByteArray &token : all.split('\0')) {
        if (!token.isEmpty())
            out.append(QString::fromLocal8Bit(token));
    }
    return out;
}

// Running Proton games: a process whose cmdline carries a *.exe under a steamapps common dir
// (the game) plus a Proton wine binary, and whose environ has the compat-data path (-> prefix).
// Everything the injection needs travels with the process: the game exe name (the WINDOWS
// process name), the wine to run the injector with, and the prefix to run it in.
QList<Injector::ProtonProcess> Injector::listProtonProcesses()
{
    QList<ProtonProcess> out;
    const qint64 self = QCoreApplication::applicationPid();

    QDir proc(QStringLiteral("/proc"));
    const QStringList ids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : ids) {
        bool ok = false;
        const qint64 pid = id.toLongLong(&ok);
        if (!ok || pid <= 0 || pid == self)
            continue;

        const QStringList cmdline = procNullTokens(pid, "cmdline");
        if (cmdline.isEmpty())
            continue;

        // game exe: a cmdline token ending in .exe. No "must contain steamapps" filter - the
        // game can launch from a different drive mapping (SteamTinkerLaunch's pressure-vessel
        // runs GTA5_Enhanced.exe as S:\common\... - live-verified), so instead blacklist the
        // prefix's ride-along services that would otherwise pollute the picker.
        QString gameExe;
        static const char *const kSystemExes[] = {
            "steam.exe", "services.exe", "svchost.exe", "explorer.exe", "winedevice.exe",
            "plugplay.exe", "rundll32.exe", "conhost.exe", "tabtip.exe", "cmd.exe",
            "wineboot.exe", "winedbg.exe", "start.exe", "mscorsvw.exe", "msiexec.exe"};
        for (const QString &token : cmdline) {
            if (!token.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
                continue;
            // basename after the LAST slash of either flavour - Qt on Linux does not treat
            // backslash as a separator, and compat tools pass windows-style paths
            // (S:\common\...\GTA5_Enhanced.exe - live-verified)
            const int slash = qMax(token.lastIndexOf(QLatin1Char('/')), token.lastIndexOf(QLatin1Char('\\')));
            const QString base = slash >= 0 ? token.mid(slash + 1) : token;
            bool system = false;
            for (const char *const sys : kSystemExes) {
                if (base.compare(QLatin1String(sys), Qt::CaseInsensitive) == 0) {
                    system = true;
                    break;
                }
            }
            if (!system) {
                gameExe = base;
                break;
            }
        }
        if (gameExe.isEmpty())
            continue;

        // wine binary: the proton dist this game runs under (cmdline[0] region)
        QString wineBinary;
        for (const QString &token : cmdline) {
            if (token.contains(QStringLiteral("/files/bin/wine"), Qt::CaseInsensitive)
                && !token.endsWith(QStringLiteral(".so"))) {
                wineBinary = token;
                break;
            }
        }
        if (wineBinary.isEmpty()) {
            // wine preloader fallback: /proc/pid/exe points into the proton dist (either
            // files/bin/wine-preloader or files/lib/wine/...-preloader) - the dist root is
            // everything before the "/files/" marker, and the runnable wine is files/bin/wine
            const QString exe = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
            const int filesMarker = exe.indexOf(QStringLiteral("/files/"), Qt::CaseInsensitive);
            if (filesMarker > 0) {
                const QString wine = exe.left(filesMarker) + QStringLiteral("/files/bin/wine");
                if (QFileInfo::exists(wine))
                    wineBinary = wine;
            }
        }
        if (wineBinary.isEmpty())
            continue;

        // prefix: the compat env the proton launcher set for this game
        QString prefix;
        const QStringList environ = procNullTokens(pid, "environ");
        for (const QString &kv : environ) {
            if (kv.startsWith(QStringLiteral("STEAM_COMPAT_DATA_PATH="))) {
                prefix = kv.mid(kv.indexOf('=') + 1) + QStringLiteral("/pfx");
                break;
            }
        }
        if (prefix.isEmpty()) {
            for (const QString &kv : environ) {
                if (kv.startsWith(QStringLiteral("WINEPREFIX="))) {
                    prefix = kv.mid(kv.indexOf('=') + 1);
                    break;
                }
            }
        }
        if (prefix.isEmpty())
            continue;

        ProtonProcess entry{pid, gameExe, wineBinary, prefix};
        bool dup = false;
        for (const auto &known : out) {
            if (known.gameExe == entry.gameExe && known.prefix == entry.prefix) {
                dup = true;
                break;
            }
        }
        if (!dup)
            out.append(entry);
    }
    return out;
}

void Injector::injectProtonDll(const ProtonProcess &game, const QString &dllPath)
{
    // locate ns_inject.exe: next to the loader binary, its parent dir, or the project tree
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        appDir + QStringLiteral("/ns_inject.exe"),
        appDir + QStringLiteral("/../ns_inject.exe"),
        m_root + QStringLiteral("/Loader/build/ns_inject.exe"),
        m_root + QStringLiteral("/build/Loader/ns_inject.exe"),
    };
    QString injector;
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            injector = QFileInfo(candidate).canonicalFilePath();
            break;
        }
    }
    if (injector.isEmpty()) {
        log(QStringLiteral("[PROTON] Error: ns_inject.exe not found (needs MinGW to build - see CMake)"), Level::Error);
        emit protonFinished(false);
        return;
    }
    if (!QFileInfo::exists(game.wineBinary)) {
        log(QStringLiteral("[PROTON] Error: the game's wine binary is gone (game closed?): %1").arg(game.wineBinary), Level::Error);
        emit protonFinished(false);
        return;
    }
    if (!QFileInfo::exists(dllPath)) {
        log(QStringLiteral("[PROTON] Error: DLL not found at '%1'").arg(dllPath), Level::Error);
        emit protonFinished(false);
        return;
    }

    // linux path -> wine path (proton prefixes map Z:\ to /). Strip the path's leading slash
    // first: "Z:\" + "\home\..." would double the backslash (Z:\\home - the 21:38 failure).
    QString wineDll = dllPath;
    wineDll.replace(QLatin1Char('/'), QLatin1Char('\\'));
    while (wineDll.startsWith(QLatin1Char('\\')))
        wineDll.remove(0, 1);
    wineDll = QStringLiteral("Z:\\") + wineDll;

    log(QStringLiteral("[PROTON] Injecting '%1' into %2 (prefix %3)")
            .arg(QFileInfo(dllPath).fileName(), game.gameExe, game.prefix));

    // Wine refuses prefixes owned by another user - and the Loader normally runs as ROOT
    // (pkexec) while the Steam game + prefix usually belong to the desktop user. Run the
    // injector as the prefix's owner when that differs (root may sudo -u without a password).
    const auto prefixOwner = QFileInfo(game.prefix).ownerId();
    const bool dropToOwner = prefixOwner != static_cast<uid_t>(geteuid());
    const QString owner = dropToOwner ? QFileInfo(game.prefix).owner() : QString();

    auto *proc = new QProcess(this);
    QStringList args;
    const QString lib64 = QFileInfo(game.wineBinary).absolutePath() + QStringLiteral("/../lib64");
    const QString lib = QFileInfo(game.wineBinary).absolutePath() + QStringLiteral("/../lib");
    if (dropToOwner) {
        if (owner.isEmpty()) {
            log(QStringLiteral("[PROTON] Error: cannot determine the prefix owner (uid %1) to run wine as").arg(prefixOwner), Level::Error);
            emit protonFinished(false);
            return;
        }
        log(QStringLiteral("[PROTON] prefix owned by '%1' - running wine as that user (loader is root)").arg(owner));
        proc->setProgram(QStringLiteral("sudo"));
        args << QStringLiteral("-u") << owner << QStringLiteral("env")
             << QStringLiteral("WINEPREFIX=") + game.prefix
             << QStringLiteral("WINEDEBUG=-all")
             << QStringLiteral("LD_LIBRARY_PATH=") + lib64 + QStringLiteral(":") + lib
             << game.wineBinary << injector
             << QStringLiteral("inject") << game.gameExe << wineDll;
    } else {
        proc->setProgram(game.wineBinary);
        args << injector << QStringLiteral("inject") << game.gameExe << wineDll;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("WINEPREFIX"), game.prefix);
        env.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));
        // proton's wine links against libs inside its own dist - make sure it can find them
        const QString existing = env.value(QStringLiteral("LD_LIBRARY_PATH"));
        env.insert(QStringLiteral("LD_LIBRARY_PATH"), lib64 + QStringLiteral(":") + lib
                       + (existing.isEmpty() ? QString() : QStringLiteral(":") + existing));
        proc->setProcessEnvironment(env);
    }
    proc->setArguments(args);

    QObject::connect(proc, &QProcess::finished, this,
                     [this, proc, game](int exitCode, QProcess::ExitStatus) {
        const QString out = QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed();
        const QString err = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
        proc->deleteLater();

        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (line.startsWith(QStringLiteral("PID ")))
                log(QStringLiteral("[PROTON] windows process: %1").arg(line.mid(4)));
        }
        const bool ok = exitCode == 0 && out.contains(QStringLiteral("OK"));
        if (ok) {
            log(QStringLiteral("[PROTON] Injection complete"), Level::Ok);
        } else {
            log(QStringLiteral("[PROTON] Injection failed (exit %1)").arg(exitCode), Level::Error);
            // full forensics: the exact command (copy-pasteable) + everything the child said
            log(QStringLiteral("[PROTON] cmd: %1 %2").arg(proc->program(), proc->arguments().join(QLatin1Char(' '))), Level::Warn);
            for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                log(QStringLiteral("[PROTON] out: %1").arg(line), Level::Warn);
            for (const QString &line : err.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                log(QStringLiteral("[PROTON] err: %1").arg(line), Level::Error);
        }
        emit protonFinished(ok);
    });
    proc->start();
}
