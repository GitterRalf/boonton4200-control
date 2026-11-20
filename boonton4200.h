#pragma once

#include "gpib.h"

#include <QDateTime>
#include <QObject>

#include <optional>

enum class Mode { Power, DecibelMilliwatts, RelativeDecibel };

struct Reading
{
    QDateTime time;
    Mode mode;
    int channel;
    double value;
    int decimals;
    int status;
    int range;

    QString unit() const;
    QString statusText() const;
};

Reading parseReading(const QByteArray &reply);

enum class Operation { Connect, Disconnect, Read, Trigger, Power, Decibel, AutoRange,
                       HoldRange, Channel, Zero, Calibrate, Local };

struct InstrumentResult
{
    bool connected = false;
    std::optional<Reading> reading;
    QString error;
};
Q_DECLARE_METATYPE(InstrumentResult)

class Boonton4200 final : public QObject
{
    Q_OBJECT

public:
    void execute(Operation operation, GpibTarget target = {}, int channel = 1);

signals:
    void finished(const InstrumentResult &result);

private:
    std::unique_ptr<GpibSession> device;
};
