#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <functional>

class Injector : public QObject
{
    Q_OBJECT
public:
    enum class Level { Info, Ok, Warn, Error };
    using DoneFn = std::function<void(int exitCode, const QString &output)>;

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

    void checkBuild(const std::function<void(const BuildState &)> &done);
    void rebuild(const std::function<void(bool ok)> &done);
    void injectSteam(qint64 pid);
    void injectCs2(qint64 pid, bool debugBuild);
    void unloadCs2(qint64 pid);
    void cleanupArtifacts(qint64 cs2Pid);

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
    void unloadFinished(bool ok);

private:
    void run(const QString &prog, const QStringList &args, DoneFn done, bool logOutput = true);
    void flushLines();
    void log(const QString &text, Level level = Level::Info);
    void gdbFallback();
    void finishCs2();

    QString steamModule() const;
    QString osirisLib(bool debug) const;

    QString m_root;
    QProcess *m_proc = nullptr;
    DoneFn m_done;
    QByteArray m_buf;
    bool m_logOutput = true;

    BuildState m_state;
    qint64 m_targetPid = 0;
    bool m_debug = false;
    QString m_lib;
};
