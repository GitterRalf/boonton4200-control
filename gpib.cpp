// NI-488.2 transport adapted from the OsciViewer successors.
#include "gpib.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>

#include <array>
#include <stdexcept>
#include <windows.h>

namespace {

constexpr int gpibError = 0x8000;
constexpr int gpibEnd = 0x2000;
constexpr int timeoutConfig = 0x03; // IbcTMO
constexpr int readdressConfig = 0x06; // IbcREADDR
constexpr int unaddressConfig = 0x1b; // IbcUnAddr
constexpr int timeout3Seconds = 12;
constexpr int timeout30Seconds = 14;
constexpr int receiveEos = 0x1400; // REOS | BIN

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

QByteArray suffix(Terminator terminator)
{
    switch (terminator) {
    case Terminator::Eoi: return {};
    case Terminator::Cr: return "\r";
    case Terminator::Lf: return "\n";
    case Terminator::CrLf: return "\r\n";
    }
    return {};
}

class NiGpib final : public GpibSession
{
    Q_DECLARE_TR_FUNCTIONS(Gpib)

public:
    explicit NiGpib(GpibTarget target) : target(target), writeSuffix(suffix(target.terminator))
    {
        wchar_t systemDirectory[MAX_PATH]{};
        const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            fail(tr("Das Windows-Systemverzeichnis konnte nicht ermittelt werden."));
        }
        library.setFileName(QDir(QString::fromWCharArray(systemDirectory)).filePath(
            QStringLiteral("ni4882.dll")));
        if (!library.load()) {
            fail(tr("NI-488.2 konnte nicht geladen werden. %1").arg(library.errorString()));
        }

        const auto open = resolve<int (WINAPI *)(int, int, int, int, int, int)>("ibdev");
        const auto listen = resolve<int (WINAPI *)(int, int, int, short *)>("ibln");
        offline = resolve<int (WINAPI *)(int, int)>("ibonl");
        setLocal = resolve<int (WINAPI *)(int)>("ibloc");
        enableRemote = resolve<void (WINAPI *)(int, const unsigned short *)>("EnableRemote");
        configure = resolve<int (WINAPI *)(int, int, int)>("ibconfig");
        writeBytes = resolve<int (WINAPI *)(int, const void *, size_t)>("ibwrt");
        readBytes = resolve<int (WINAPI *)(int, void *, size_t)>("ibrd");
        status = resolve<int (WINAPI *)()>("ThreadIbsta");
        error = resolve<int (WINAPI *)()>("ThreadIberr");
        count = resolve<int (WINAPI *)()>("ThreadIbcnt");

        const int eos = writeSuffix.isEmpty() ? 0 : receiveEos | static_cast<unsigned char>(writeSuffix.back());
        descriptor = open(target.board, target.address, 0, timeout3Seconds, 1, eos);
        if (descriptor < 0) {
            fail(tr("GPIB %1, Adresse %2 konnte nicht geöffnet werden (NI-Fehler %3).")
                 .arg(target.board).arg(target.address).arg(error()));
        }
        try {
            short present = 0;
            check(listen(descriptor, target.address, 0, &present), tr("GPIB-Adressprüfung"));
            if (!present) {
                fail(tr("Auf GPIB %1, Adresse %2 antwortet kein Teilnehmer.")
                     .arg(target.board).arg(target.address));
            }
            // The 4200 emits one frame per talk addressing, including for repeated reads.
            check(configure(descriptor, readdressConfig, 1), tr("GPIB-Neuadressierung einstellen"));
            check(configure(descriptor, unaddressConfig, 1), tr("GPIB-Abadressierung einstellen"));
        } catch (...) {
            offline(descriptor, 0);
            throw;
        }
    }

    ~NiGpib() override
    {
        if (usingRemote) {
            setLocal(descriptor);
        }
        offline(descriptor, 0);
    }

    void local(bool force) override
    {
        if (usingRemote || force) {
            check(setLocal(descriptor), tr("Rückgabe an die Gerätebedienung"));
            usingRemote = false;
        }
    }

    void send(const QByteArray &command) override
    {
        check(configure(descriptor, timeoutConfig, timeout3Seconds), tr("GPIB-Zeitlimit setzen"));
        if (!usingRemote) {
            const std::array<unsigned short, 2> addresses{static_cast<unsigned short>(target.address), 0xffff};
            usingRemote = true;
            enableRemote(target.board, addresses.data());
            check(status(), tr("Umschalten auf Fernsteuerung"));
        }
        const auto payload = command + writeSuffix;
        check(writeBytes(descriptor, payload.constData(), static_cast<size_t>(payload.size())),
              tr("Senden von %1").arg(QString::fromLatin1(command)));
        if (count() != payload.size()) {
            fail(tr("Der GPIB-Befehl wurde nur teilweise übertragen."));
        }
    }

    QByteArray read(bool extendedTimeout) override
    {
        check(configure(descriptor, timeoutConfig, extendedTimeout ? timeout30Seconds : timeout3Seconds),
              tr("GPIB-Zeitlimit setzen"));
        QByteArray buffer(128, '\0');
        const auto state = readBytes(descriptor, buffer.data(), static_cast<size_t>(buffer.size()));
        const auto received = count();
        check(state, tr("Messwert lesen"));
        if (!(state & gpibEnd) || received <= 0 || received > buffer.size()) {
            fail(tr("Der GPIB-Messdatensatz ist unvollständig."));
        }
        buffer.truncate(received);
        return buffer;
    }

private:
    template<typename Function>
    Function resolve(const char *name)
    {
        const auto symbol = library.resolve(name);
        if (!symbol) {
            fail(tr("NI-488.2 stellt %1 nicht bereit.").arg(QString::fromLatin1(name)));
        }
        return reinterpret_cast<Function>(symbol);
    }

    void check(int state, const QString &operation) const
    {
        if (state & gpibError) {
            fail(tr("%1 fehlgeschlagen (NI-Fehler %2, Status 0x%3).")
                 .arg(operation).arg(error()).arg(state & 0xffff, 0, 16));
        }
    }

    QLibrary library;
    const GpibTarget target;
    const QByteArray writeSuffix;
    int descriptor = -1;
    bool usingRemote = false;
    int (WINAPI *offline)(int, int) = nullptr;
    int (WINAPI *setLocal)(int) = nullptr;
    void (WINAPI *enableRemote)(int, const unsigned short *) = nullptr;
    int (WINAPI *configure)(int, int, int) = nullptr;
    int (WINAPI *writeBytes)(int, const void *, size_t) = nullptr;
    int (WINAPI *readBytes)(int, void *, size_t) = nullptr;
    int (WINAPI *status)() = nullptr;
    int (WINAPI *error)() = nullptr;
    int (WINAPI *count)() = nullptr;
};

} // namespace

std::unique_ptr<GpibSession> openGpib(GpibTarget target)
{
    return std::make_unique<NiGpib>(target);
}
