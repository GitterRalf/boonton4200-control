#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

enum class Terminator { Eoi, Cr, Lf, CrLf };

struct GpibTarget
{
    int board = 0;
    int address = 8;
    Terminator terminator = Terminator::CrLf;
};

class GpibSession
{
public:
    virtual ~GpibSession() = default;
    virtual QByteArray read(bool extendedTimeout = false) = 0;
    virtual void send(const QByteArray &command) = 0;
    virtual void local(bool force = false) = 0;
};

std::unique_ptr<GpibSession> openGpib(GpibTarget target);
