#pragma once

#include <QColor>
#include <QPixmap>

class Icons
{
public:
    enum class Kind { Steam, Cs2 };
    enum class Status { Idle, Question, Cross, Check, Skip };

    static QPixmap app(Kind kind, int size, const QColor &color);
    static QPixmap badge(Status status, int size);
};
