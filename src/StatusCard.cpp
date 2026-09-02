#include "StatusCard.h"

#include <QVBoxLayout>

namespace {
constexpr int kIconSize = 72;
constexpr int kBadgeSize = 24;
constexpr int kIconSlot = 84;
} // namespace

StatusCard::StatusCard(Icons::Kind kind, const QString &title, const QColor &accent, QWidget *content, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("Card"));
    setMinimumWidth(250);

    m_icon = new QLabel(this);
    m_icon->setFixedSize(kIconSlot, kIconSlot);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setPixmap(Icons::app(kind, kIconSize, accent));

    m_badge = new QLabel(m_icon);
    m_badge->setPixmap(Icons::badge(Icons::Status::Idle, kBadgeSize));

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

    layoutBadge();
}

void StatusCard::setStatus(Icons::Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    m_badge->setPixmap(Icons::badge(status, kBadgeSize));
}

void StatusCard::setSubtitle(const QString &text)
{
    m_subtitle->setText(text);
}

void StatusCard::layoutBadge()
{
    m_badge->move(kIconSlot - kBadgeSize - 4, 0);
    m_badge->raise();
}
