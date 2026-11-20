#include "boonton4200.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QThread>

#include <cmath>
#include <stdexcept>

QString Reading::unit() const
{
    switch (mode) {
    case Mode::Power: return QStringLiteral("mW");
    case Mode::DecibelMilliwatts: return QStringLiteral("dBm");
    case Mode::RelativeDecibel: return QStringLiteral("dBr");
    }
    return {};
}

QString Reading::statusText() const
{
    switch (status) {
    case 0: return QCoreApplication::translate("Reading", "Messwert gültig");
    case 1: return QCoreApplication::translate("Reading", "Eingabewert zu klein");
    case 2: return QCoreApplication::translate("Reading", "Eingabewert zu groß");
    case 3: return QCoreApplication::translate("Reading", "Messbereich unterschritten");
    case 4: return QCoreApplication::translate("Reading", "Messbereich überschritten");
    case 5:
    case 6: return QCoreApplication::translate("Reading", "Nullabgleich außerhalb des zulässigen Bereichs");
    case 7: return QCoreApplication::translate("Reading", "Differenzkanal außerhalb des Messbereichs");
    default: return QCoreApplication::translate("Reading", "Gerätestatus %1").arg(status);
    }
}

Reading parseReading(const QByteArray &reply)
{
    // The four-digit mantissa and signed exponent form an ordinary decimal number.
    static const QRegularExpression format(QStringLiteral(
        R"(\A(PW|DM|DR)([ABC])\s*([+-]?[0-9]{4}[Ee][+-][0-9])\s*,\s*([0-9])\s*,\s*([0-9])\z)"));
    const auto line = QString::fromLatin1(reply).trimmed();
    const auto match = format.match(line);
    if (!match.hasMatch()) {
        throw std::runtime_error(
            Boonton4200::tr("Unerwarteter Boonton-Datensatz: %1").arg(line.left(64)).toUtf8().constData());
    }

    const auto mode = match.captured(1);
    bool validValue = false;
    const auto scientific = match.captured(3);
    const auto value = scientific.toDouble(&validValue);
    if (!validValue || !std::isfinite(value)) {
        throw std::runtime_error(Boonton4200::tr("Der Messwert ist ungültig.").toUtf8().constData());
    }
    const int decimals = qMax(0, -scientific.right(2).toInt());
    return {QDateTime::currentDateTime(),
            mode == QStringLiteral("PW") ? Mode::Power
                : mode == QStringLiteral("DM") ? Mode::DecibelMilliwatts : Mode::RelativeDecibel,
            match.captured(2).front().unicode() - 'A' + 1, value, decimals,
            match.captured(4).toInt(), match.captured(5).toInt()};
}

namespace {

QByteArray commandFor(Operation operation, int channel)
{
    switch (operation) {
    case Operation::Trigger: return "T";
    case Operation::Power: return "P";
    case Operation::Decibel: return "B";
    case Operation::AutoRange: return "A";
    case Operation::HoldRange: return "O";
    case Operation::Channel: return QByteArray::number(channel) + 'N';
    case Operation::Zero: return "Z";
    case Operation::Calibrate: return "K";
    default: return {};
    }
}

} // namespace

void Boonton4200::execute(Operation operation, GpibTarget target, int channel)
{
    InstrumentResult result;
    try {
        switch (operation) {
        case Operation::Connect: {
            auto candidate = openGpib(target);
            result.reading = parseReading(candidate->read());
            device = std::move(candidate);
            break;
        }
        case Operation::Disconnect: {
            auto current = std::move(device);
            if (current) {
                current->local();
            }
            break;
        }
        default:
            if (!device) {
                throw std::runtime_error(tr("Das Boonton 4200 ist nicht verbunden.").toUtf8().constData());
            }
            if (operation == Operation::Local) {
                device->local(true);
                break;
            }
            const auto command = commandFor(operation, channel);
            if (!command.isEmpty()) {
                device->send(command);
                // Allow the new mode or channel to reach the display before the first read.
                QThread::msleep(110);
            }
            result.reading = parseReading(device->read(operation == Operation::Zero
                                                       || operation == Operation::Calibrate));
            break;
        }
    } catch (const std::exception &error) {
        result.error = QString::fromUtf8(error.what());
    }
    result.connected = bool(device);
    emit finished(result);
}
