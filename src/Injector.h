#pragma once

#include <QCryptographicHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>

#include "Payloads.h"

class Injector : public QObject
{
    Q_OBJECT
public:
    enum class Level { Info, Ok, Warn, Error };
    using DoneFn = std::function<void(int exitCode, const QString &output)>;
    // Owning handle for decrypted payload bytes: the heap QByteArray is zeroized on
    // last release (no COW copies of plaintext linger).
    using SecureBytes = std::shared_ptr<QByteArray>;

    struct BuildState {
        bool steamModuleExists = false;
        bool osirisExists = false;
        bool steamStale = false;
        bool osirisStale = false;
        bool needed() const { return !steamModuleExists || !osirisExists || steamStale || osirisStale; }
    };

    explicit Injector(QObject *parent = nullptr);

    void setProjectRoot(const QString &root) { m_root = root; }
    QString projectRoot() const { return m_root; }

    static qint64 findPid(const QString &name);
    static bool isRoot();
    static bool mapsContain(qint64 pid, const QString &needle);
    static QStringList missingCs2Modules(qint64 pid);
    static qint64 cpuJiffies(qint64 pid);   // utime+stime clock ticks, -1 if unreadable
    static bool writePtraceScope(const QString &value);

    void checkBuild(const std::function<void(const BuildState &)> &done);
    void rebuild(const std::function<void(bool ok)> &done);
    void injectSteam(qint64 pid);
    void injectCs2(qint64 pid, bool debugBuild);
    void unloadCs2(qint64 pid);
    void unloadCs2Legacy(qint64 pid);
    void injectTf2(qint64 pid);
    void unloadTf2(qint64 pid);
    void cleanupArtifacts(qint64 cs2Pid);

    // ANY PROCESS (experimental): inject an arbitrary .so into an arbitrary pid through the
    // game-agnostic memfd injector (PID + library path). No build checks, no game logic, no
    // gdb fallback - the caller picks the target, the caller owns the outcome.
    struct ProcessEntry {
        qint64 pid = 0;
        QString name;
    };
    static QList<ProcessEntry> listInjectableProcesses();
    void injectAny(qint64 pid, const QString &libPath);

    // PROTON (experimental): DLL injection into Windows games running under Proton. The Windows
    // half is ns_inject.exe (Loader/proton/, cross-compiled by CMake) - the Loader runs it
    // through the game's OWN Proton wine inside the game's prefix; ns_inject does the classic
    // OpenProcess -> VirtualAllocEx -> CreateRemoteThread(LoadLibraryW) against the game.
    struct ProtonProcess {
        qint64 pid = 0;      // Linux-side pid of the game process (informational)
        QString gameExe;     // Windows process name ("hl2.exe")
        QString wineBinary;  // the Proton wine this game runs under
        QString prefix;      // WINEPREFIX (compatdata/<appid>/pfx)
    };
    static QList<ProtonProcess> listProtonProcesses();
    void injectProtonDll(const ProtonProcess &game, const QString &dllPath);

    struct CleanupReport {
        bool dumpsFixed = false;
        bool dumpsOk = false;
        QStringList memfdCopies;   // leftover temp copies found & removed
        QStringList mapsResidue;   // suspicious paths still mapped in cs2
        bool clean = false;
    };
    static CleanupReport auditArtifacts(qint64 cs2Pid, QStringList *logLines);

    // VAC/status readout
    struct VacStatus {
        bool guiLogExists = false;
        qint64 guiLogSize = 0;       // anomaly-only contract: healthy = silent (no recent lines)
        qint64 guiLogAgeMs = -1;     // since last modification; -1 = unknown
        QString ptraceScope;         // /proc/sys/kernel/yama/ptrace_scope
        qint64 cs2Uid = -1;          // owner of the cs2 process
        bool cs2SameUserAsRoot = false;
        QString steamAccount;        // most recent login from loginusers.vdf
    };
    static VacStatus readVacStatus(const QString &steamRoot = QString());

signals:
    void logMessage(const QString &text, int level);
    void steamFinished(bool ok);
    void cs2Finished(bool ok);
    void tf2Finished(bool ok);
    void unloadFinished(bool ok);
    void anyFinished(bool ok);
    void protonFinished(bool ok);

private:
    void run(const QString &prog, const QStringList &args, DoneFn done, bool logOutput = true);
    void runWithStdin(const QString &prog, const QStringList &args, const QByteArray &stdinData,
                      DoneFn done, bool logOutput = true);
    // Streaming variant for embedded payloads: decrypts + writes 64 KB chunks under
    // backpressure, so no full multi-megabyte plaintext image ever exists in this
    // process's address space (see Payloads.h). The session trailer is appended as the
    // final chunk.
    void runWithStdinStream(const QString &prog, const QStringList &args,
                            payloads::StreamPtr stream, DoneFn done, bool logOutput = true,
                            const QString &tag = QString());
    void pumpStream();
    void flushLines();
    // ---- module-integrity watchdog (hardening 2026-09-24) --------------------------
    // Runs as a root background timer while a CS2 session is live: re-hashes the module's
    // executable segments by reading the TARGET's memory (/proc/<pid>/mem) and compares
    // against a baseline taken by THIS process right after injection. An in-process patcher
    // can NOP the module's own SelfIntegrity check, but cannot rewrite what a separate root
    // process reads out of the game. Two consecutive drifts -> <exchangeRoot>/ns_unload_request (the
    // module's proven fail-closed teardown path) + a log line.
    void startIntegrityWatch();
    void integrityTick();
    void log(const QString &text, Level level = Level::Info);
    // memfd-flavored gdb fallback: memfd_create in the target (gdb round 1), the stamped
    // module bytes written straight into /proc/<pid>/fd/<n> (no plaintext temp file on
    // disk), then dlopen("/proc/self/fd/<n>") (gdb round 2) - the target-visible,
    // mount-namespace-proof path (see gdbMemfdFallback).
    void gdbMemfdFallback(const SecureBytes &stampedBytes, std::function<void(bool)> done);
    void gdbFallback();
    // gdb fallback for the streamed-payload paths (stream partially consumed): full-buffer
    // re-extract + trailer + temp copy. `game` selects the payload, `done` finishes.
    void gdbFallbackExtracted(const char *game, std::function<void(bool)> done);
    void gdbFallbackSteam();
    void finishCs2();
    void gdbFallbackTf2();
    void finishTf2();
    // inject_memfd location: the project tree's copy when present, otherwise the embedded
    // payload extracted to a disguised /tmp path (ship mode - no source tree on the machine).
    QString memfdInjector();

    QString steamModule() const;
    QString osirisLib(bool debug) const;
    QString tf2Lib(bool debug) const;

    QString m_root;
    // integrity watchdog state
    QTimer m_integrityTimer{this};
    qint64 m_watchPid = 0;
    quint64 m_integrityBaseline = 0;
    int m_integrityStrikes = 0;
    QProcess *m_proc = nullptr;
    DoneFn m_done;
    QByteArray m_buf;          // line-consumed by flushLines (logging)
    QByteArray m_fullOutput;   // complete child output, handed to DoneFn (parsed by gdbMemfdFallback)
    QByteArray m_stdinData;
    bool m_logOutput = true;

    // streaming stdin state
    payloads::StreamPtr m_stream;
    qint64 m_streamPending = 0;
    bool m_streamTrailerQueued = false;
    QString m_streamTag;                       // log prefix ("CS2"/"TF2"/"Steam")
    std::unique_ptr<QCryptographicHash> m_streamHash; // plaintext fingerprint, logged at end

    BuildState m_state;
    qint64 m_targetPid = 0;
    bool m_debug = false;
    QString m_lib;
};
