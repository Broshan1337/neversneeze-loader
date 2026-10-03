#include "StatusCard.h"

#include <QPainter>
#include <QVBoxLayout>

namespace {
constexpr int kIconSize = 72;
constexpr int kBadgeSize = 24;
constexpr int kIconSlot = 84;

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    t = qBound(0.0, t, 1.0);
    return QColor(a.red() + int((b.red() - a.red()) * t),
                  a.green() + int((b.green() - a.green()) * t),
                  a.blue() + int((b.blue() - a.blue()) * t),
                  a.alpha() + int((b.alpha() - a.alpha()) * t));
}
} // namespace

StatusCard::StatusCard(Icons::Kind kind, const QString &title, const QColor &accent, QWidget *content, QWidget *parent)
    : QFrame(parent)
    , m_accent(accent)
{
    setObjectName(QStringLiteral("Card"));
    setMinimumWidth(250);
    setAttribute(Qt::WA_Hover);

    m_icon = new QLabel(this);
    m_icon->setFixedSize(kIconSlot, kIconSlot);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setPixmap(Icons::app(kind, kIconSize, accent));

    m_badge = new QLabel(m_icon);
    m_badge->setPixmap(Icons::badge(Icons::Status::Idle, kBadgeSize));
    m_badge->move(kIconSlot - kBadgeSize - 4, 0);
    m_badge->raise();
    m_badge->hide(); // Idle = nothing to say yet

    QLabel *titleLabel = new QLabel(title, this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(QStringLiteral(
        "color:%1; font-size:15px; font-weight:800; letter-spacing:1px; border:none; background:transparent;")
                                  .arg(accent.name()));

    m_subtitle = new QLabel(this);
    m_subtitle->setAlignment(Qt::AlignCenter);
    m_subtitle->setStyleSheet(
        "color:#8a94a2; font-size:12px; border:none; background:transparent;");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 20, 18, 18);
    layout->setSpacing(8);
    layout->addWidget(m_icon, 0, Qt::AlignHCenter);
    layout->addWidget(titleLabel);
    layout->addWidget(m_subtitle);
    layout->addSpacing(4);
    layout->addWidget(content);
    layout->addStretch();

    m_pulse.setInterval(450);
    connect(&m_pulse, &QTimer::timeout, this, [this] {
        m_pulseOn = !m_pulseOn;
        update();
    });

    m_pop.setDuration(240);
    m_pop.setStartValue(0.0);
    m_pop.setEndValue(1.0);
    m_pop.setEasingCurve(QEasingCurve::OutBack);
    connect(&m_pop, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        const qreal t = v.toReal();
        const int size = int(kBadgeSize * (0.35 + 0.65 * t));
        const int x = kIconSlot - kBadgeSize - 4 + (kBadgeSize - size) / 2;
        const int y = (kBadgeSize - size) / 2;
        m_badge->setGeometry(x, y, size, size);
    });

    m_glow.setDuration(180);
    connect(&m_glow, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_glowLevel = v.toReal();
        update();
    });
}

void StatusCard::setStatus(Icons::Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    m_badge->setVisible(status != Icons::Status::Idle); // idle = clean icon, no badge
    m_badge->setPixmap(Icons::badge(status, kBadgeSize));
    m_badge->setGeometry(kIconSlot - kBadgeSize - 4, 0, kBadgeSize, kBadgeSize);
    if (status == Icons::Status::Check)
        m_pop.start();
    update();
}

void StatusCard::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    if (busy) {
        m_pulseOn = true;
        m_pulse.start();
    } else {
        m_pulse.stop();
        m_pulseOn = false;
    }
    update();
}

void StatusCard::setSubtitle(const QString &text)
{
    m_subtitle->setText(text);
}

void StatusCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor border = [&] {
        if (m_busy)
            return mix(QColor(0x1a, 0x1a, 0x1e), QColor(0xab, 0x46, 0xff), m_pulseOn ? 1.0 : 0.35);
        switch (m_status) {
        case Icons::Status::Check: return QColor(0xab, 0x46, 0xff);
        case Icons::Status::Cross: return QColor(0xbe, 0x3c, 0x42);
        case Icons::Status::Question: return QColor(0x3a, 0x3a, 0x40);
        default: return QColor(0x1a, 0x1a, 0x1e);
        }
    }();

    // soft violet glow ring when the card is live (injected / busy / hovered)
    const qreal glowStrength = (m_status == Icons::Status::Check || m_busy) ? 1.0 : m_glowLevel;
    const QColor glowColor = m_status == Icons::Status::Cross && !m_busy ? QColor(0xbe, 0x3c, 0x42)
                                                                         : QColor(0xab, 0x46, 0xff);
    const int extent = 6;
    for (int i = extent; i >= 1; --i) {
        QColor c = glowColor;
        c.setAlpha(int(30 * glowStrength * (1.0 - qreal(i) / (extent + 1))));
        if (c.alpha() <= 0)
            continue;
        p.setPen(QPen(c, 1.4));
        p.setBrush(Qt::NoBrush);
        const qreal grow = i * 0.9;
        p.drawRoundedRect(QRectF(rect()).adjusted(grow, grow, -grow, -grow), 14 + i, 14 + i);
    }

    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
}

void StatusCard::enterEvent(QEnterEvent *event)
{
    QFrame::enterEvent(event);
    m_glow.stop();
    m_glow.setStartValue(m_glowLevel);
    m_glow.setEndValue(1.0);
    m_glow.start();
}

void StatusCard::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);
    m_glow.stop();
    m_glow.setStartValue(m_glowLevel);
    m_glow.setEndValue(0.0);
    m_glow.start();
}
