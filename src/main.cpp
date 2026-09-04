#include "MainWindow.h"

#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NeversneezeLoader"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

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
