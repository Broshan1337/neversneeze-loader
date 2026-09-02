#include "Icons.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace {

QColor statusColor(Icons::Status status)
{
    switch (status) {
    case Icons::Status::Question: return QColor(0xf0, 0xb4, 0x29);
    case Icons::Status::Cross: return QColor(0xe5, 0x48, 0x4d);
    case Icons::Status::Check: return QColor(0x46, 0xa7, 0x58);
    case Icons::Status::Skip: return QColor(0x6b, 0x72, 0x80);
    case Icons::Status::Idle: break;
    }
    return QColor(0x4a, 0x52, 0x5e);
}

} // namespace

QPixmap Icons::app(Kind kind, int size, const QColor &color)
{
    const int s = size * 2;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    if (kind == Kind::Steam) {
        QPen pen(color, s * 0.065);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(s * 0.09, s * 0.09, s * 0.82, s * 0.82));

        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(s * 0.63, s * 0.40), s * 0.145, s * 0.145);
        p.drawEllipse(QPointF(s * 0.32, s * 0.66), s * 0.095, s * 0.095);

        QPen bar(color, s * 0.095, Qt::SolidLine, Qt::RoundCap);
        p.setPen(bar);
        p.drawLine(QPointF(s * 0.33, s * 0.65), QPointF(s * 0.62, s * 0.42));
        p.drawLine(QPointF(s * 0.32, s * 0.66), QPointF(s * 0.155, s * 0.80));
    } else {
        QLinearGradient grad(0, 0, 0, s);
        grad.setColorAt(0.0, color.lighter(130));
        grad.setColorAt(1.0, color.darker(120));

        QPainterPath tile;
        tile.addRoundedRect(QRectF(s * 0.05, s * 0.05, s * 0.90, s * 0.90), s * 0.24, s * 0.24);
        p.fillPath(tile, grad);

        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(int(s * 0.36));
        font.setWeight(QFont::Black);
        p.setFont(font);
        p.setPen(Qt::white);
        p.drawText(QRectF(s * 0.05, s * 0.05, s * 0.90, s * 0.90), Qt::AlignCenter, QStringLiteral("CS2"));
    }

    return pm;
}

QPixmap Icons::badge(Status status, int size)
{
    const int s = size * 2;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor base = statusColor(status);
    if (status == Status::Idle) {
        QPen pen(QColor(0x3a, 0x41, 0x4c), s * 0.09);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(s * 0.14, s * 0.14, s * 0.72, s * 0.72));
        return pm;
    }

    p.setPen(Qt::NoPen);
    p.setBrush(base);
    p.drawEllipse(QRectF(s * 0.08, s * 0.08, s * 0.84, s * 0.84));

    QPen glyph(Qt::white, s * 0.10, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(glyph);
    p.setBrush(Qt::NoBrush);

    switch (status) {
    case Status::Question: {
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(int(s * 0.56));
        p.setFont(font);
        p.setPen(Qt::white);
        p.drawText(QRectF(s * 0.08, s * 0.06, s * 0.84, s * 0.84), Qt::AlignCenter, QStringLiteral("?"));
        break;
    }
    case Status::Cross:
        p.drawLine(QPointF(s * 0.33, s * 0.33), QPointF(s * 0.67, s * 0.67));
        p.drawLine(QPointF(s * 0.67, s * 0.33), QPointF(s * 0.33, s * 0.67));
        break;
    case Status::Check: {
        QPainterPath path;
        path.moveTo(s * 0.28, s * 0.52);
        path.lineTo(s * 0.44, s * 0.68);
        path.lineTo(s * 0.72, s * 0.34);
        p.drawPath(path);
        break;
    }
    case Status::Skip:
        p.drawLine(QPointF(s * 0.30, s * 0.50), QPointF(s * 0.70, s * 0.50));
        break;
    case Status::Idle:
        break;
    }

    return pm;
}
