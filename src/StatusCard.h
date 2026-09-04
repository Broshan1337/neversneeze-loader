#pragma once

#include "Icons.h"

#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <QVariantAnimation>

class StatusCard : public QFrame
{
    Q_OBJECT
public:
    StatusCard(Icons::Kind kind, const QString &title, const QColor &accent, QWidget *content, QWidget *parent = nullptr);

    void setStatus(Icons::Status status);
    void setBusy(bool busy);
    void setSubtitle(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void startPop();
    void updateBorderTarget();

    QLabel *m_icon;
    QLabel *m_badge;
    QLabel *m_subtitle;
    QColor m_accent;
    Icons::Status m_status = Icons::Status::Idle;
    bool m_busy = false;
    bool m_hovered = false;

    QTimer m_pulse;
    bool m_pulseOn = false;
    QVariantAnimation m_pop;
    QVariantAnimation m_glow;
    qreal m_glowLevel = 0.0; // 0 = base, 1 = full status accent
};
