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

    void checkBuild(const std::function<void(const BuildState &)> &done);
    void rebuild(const std::function<void(bool ok)> &done);
    void injectSteam(qint64 pid);
    void injectCs2(qint64 pid, bool debugBuild);
    void unloadCs2(qint64 pid);

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
