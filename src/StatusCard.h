#pragma once

#include "Icons.h"

#include <QFrame>
#include <QLabel>

class StatusCard : public QFrame
{
    Q_OBJECT
public:
    StatusCard(Icons::Kind kind, const QString &title, const QColor &accent, QWidget *content, QWidget *parent = nullptr);

    void setStatus(Icons::Status status);
    void setSubtitle(const QString &text);

private:
    void layoutBadge();

    QLabel *m_icon;
    QLabel *m_badge;
    QLabel *m_subtitle;
    Icons::Status m_status = Icons::Status::Idle;
};
