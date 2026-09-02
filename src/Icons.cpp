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
    case Icons::Status::Cross: return QColor(0xbe, 0x3c, 0x42);
    case Icons::Status::Check: return QColor(0xab, 0x46, 0xff);
    case Icons::Status::Skip: return QColor(0x6b, 0x72, 0x80);
    case Icons::Status::Idle: break;
    }
    return QColor(0x2a, 0x2a, 0x2e);
}

QPainterPath agentPath()
{
    // Simplified CS2 key-art silhouette on a 100x100 canvas, agent facing right.
    QPainterPath body;
    body.addEllipse(QPointF(60.0, 27.0), 8.5, 8.5);                     // head
    body.moveTo(56.0, 36.0);
    body.lineTo(44.0, 62.0);                                            // torso
    body.moveTo(44.0, 62.0);
    body.lineTo(31.0, 88.0);                                            // back leg
    body.moveTo(44.0, 62.0);
    body.lineTo(60.0, 84.0);
    body.lineTo(66.0, 92.0);                                            // front leg, knee bent
    return body;
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
        // Filled disc, glyph drawn comfortably inside (the old version filled the whole slot).
        QLinearGradient grad(0, 0, 0, s);
        grad.setColorAt(0.0, color.lighter(125));
        grad.setColorAt(1.0, color.darker(160));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawEllipse(QRectF(s * 0.14, s * 0.14, s * 0.72, s * 0.72));

        p.setRenderHint(QPainter::Antialiasing);
        const QColor white(0xf3, 0xf7, 0xfb);

        // piston arm: knob (top-right) -> ball (bottom-left, breaking the disc edge)
        QPen arm(white, s * 0.075, Qt::SolidLine, Qt::RoundCap);
        p.setPen(arm);
        p.drawLine(QPointF(s * 0.60, s * 0.42), QPointF(s * 0.26, s * 0.70));

        p.setPen(Qt::NoPen);
        p.setBrush(white);
        p.drawEllipse(QPointF(s * 0.60, s * 0.40), s * 0.115, s * 0.115);   // knob disc
        p.setBrush(QColor(0x1b, 0x28, 0x38));
        p.drawEllipse(QPointF(s * 0.60, s * 0.40), s * 0.045, s * 0.045);   // knob hole
        p.setBrush(white);
        p.drawEllipse(QPointF(s * 0.26, s * 0.70), s * 0.075, s * 0.075);   // ball
    } else {
        // CS2 tile: dark navy rounded square + orange agent silhouette + white rifle.
        QLinearGradient grad(0, 0, 0, s);
        grad.setColorAt(0.0, QColor(0x14, 0x3a, 0x66));
        grad.setColorAt(1.0, QColor(0x0c, 0x25, 0x45));
        QPainterPath tile;
        tile.addRoundedRect(QRectF(s * 0.14, s * 0.14, s * 0.72, s * 0.72), s * 0.14, s * 0.14);
        p.fillPath(tile, grad);
        p.setPen(QPen(QColor(0x1e, 0x4a, 0x80), s * 0.012));
        p.setBrush(Qt::NoBrush);
        p.drawPath(tile);

        p.setRenderHint(QPainter::Antialiasing);

        QPen body(color, s * 0.085, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(body);
        p.setBrush(color);
        p.drawPath(agentPath());

        // white rifle, aimed right, with a stock and magazine
        QPen rifle(QColor(0xf2, 0xf4, 0xf7), s * 0.045, Qt::SolidLine, Qt::RoundCap);
        p.setPen(rifle);
        p.drawLine(QPointF(s * 0.50, s * 0.50), QPointF(s * 0.84, s * 0.43));
        p.drawLine(QPointF(s * 0.50, s * 0.50), QPointF(s * 0.46, s * 0.55));
        p.drawLine(QPointF(s * 0.64, s * 0.47), QPointF(s * 0.62, s * 0.56));
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
        QPen pen(QColor(0x2a, 0x2a, 0x2e), s * 0.09);
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
