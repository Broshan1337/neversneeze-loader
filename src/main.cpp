#include <AntiDebug.h>
#include "MainWindow.h"

#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

#include <cstdio>
#include <unistd.h>

int main(int argc, char *argv[])
{
    // ROOT ONLY: everything this app does (ptrace injection, /tmp hygiene, memfd staging)
    // assumes root; running it as a normal user would produce half-broken states. Refuse
    // before any Qt setup (run.sh / pkexec is the supported entry).
    if (geteuid() != 0) {
        std::fprintf(stderr, "Neversnooze loader must run as root (use run.sh / pkexec)\n");
        return 1;
    }
    if (anti_debug::analysisDetected()) {
        std::fprintf(stderr, "refusing to run under a tracer or LD_PRELOAD\n");
        return 1;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NeversnoozeLoader"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // single instance: a second launch just exits - the tray/first window is already running
    QLocalSocket existing;
    existing.connectToServer(QStringLiteral("neversnooze-loader"));
    if (existing.waitForConnected(300))
        return 0;
    QLocalServer::removeServer(QStringLiteral("neversnooze-loader"));
    auto *instanceServer = new QLocalServer(&app);
    instanceServer->listen(QStringLiteral("neversnooze-loader"));

    const QPixmap logo(QStringLiteral(":/assets/nslogo.png"));
    if (!logo.isNull())
        QApplication::setWindowIcon(QIcon(logo));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x0b, 0x0d, 0x10));
    palette.setColor(QPalette::WindowText, QColor(0xdf, 0xe4, 0xea));
    palette.setColor(QPalette::Base, QColor(0x0e, 0x11, 0x14));
    palette.setColor(QPalette::Text, QColor(0xaa, 0xb4, 0xc0));
    palette.setColor(QPalette::Button, QColor(0x1a, 0x1f, 0x26));
    palette.setColor(QPalette::ButtonText, QColor(0xdf, 0xe4, 0xea));
    palette.setColor(QPalette::Highlight, QColor(0x2a, 0x31, 0x3b));
    palette.setColor(QPalette::HighlightedText, QColor(0xdf, 0xe4, 0xea));
    QApplication::setPalette(palette);

    MainWindow window;
    window.show();
    return app.exec();
}
