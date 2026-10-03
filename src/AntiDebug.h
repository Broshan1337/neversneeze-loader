#pragma once

#include <QFile>
#include "ObfAnnotations.h"
#include <QStringList>

#include <cstring>
#include <unistd.h>

// Refuse to operate under analysis:
//  - a debugger/ptrace tracer (TracerPid) - the loader's memory holds the decrypted
//    payloads during injection, and dynamic analysis of the decrypt path is exactly what
//    this blocks. Read-only check (no ptrace side effects). Checked at startup and again
//    before each payload decrypt.
//  - LD_PRELOAD (function-hooking shims - the classic "AI wrote me a write() logger" move
//    to capture the payload stream). pkexec strips the env anyway; this catches plain sudo.
namespace anti_debug
{

[[nodiscard]] inline NS_OBF_FLATTEN bool tracerAttached()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly))
        return false;
    const QStringList lines = QString::fromLatin1(status.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("TracerPid:"))) {
            const int pid = line.mid(10).trimmed().toInt();
            return pid > 0;
        }
    }
    return false;
}

[[nodiscard]] inline NS_OBF_FLATTEN bool preloaded()
{
    // empty/unset LD_PRELOAD = clean
    const QByteArray preload = qgetenv("LD_PRELOAD");
    for (char c : preload) {
        if (c != ' ' && c != '\t' && c != ':' && c != '\0')
            return true;
    }
    return false;
}

[[nodiscard]] inline NS_OBF_FLATTEN bool analysisDetected()
{
    return tracerAttached() || preloaded();
}

} // namespace anti_debug
