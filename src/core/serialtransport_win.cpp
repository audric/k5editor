#include "serialtransport.h"
#include <algorithm>
#include <iterator>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace k5 {

static RadioError osError(const QString &what) {
    const DWORD code = GetLastError();
    return RadioError(QStringLiteral("%1: %2").arg(what, qt_error_string(int(code)).trimmed()));
}

// Reads wait up to readMs for the first byte, then return what has arrived.
static bool setTimeouts(HANDLE h, int readMs) {
    COMMTIMEOUTS t{};
    t.ReadIntervalTimeout = MAXDWORD;
    if (readMs > 0) {
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = DWORD(readMs);
    }
    t.WriteTotalTimeoutConstant = 2000;
    return SetCommTimeouts(h, &t);
}

SerialTransport::SerialTransport(const QString &device) {
    // COM10 and above open only under their full name; the short ones accept it too.
    const QString path = device.startsWith(QLatin1String("\\\\")) ? device : QStringLiteral("\\\\.\\") + device;
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw osError(QStringLiteral("Cannot open %1").arg(device));
    DCB dcb{};
    dcb.DCBlength = sizeof dcb;
    if (!GetCommState(h, &dcb)) {
        const RadioError e = osError(QStringLiteral("%1 is not a serial port").arg(device));
        CloseHandle(h);
        throw e;
    }
    dcb.BaudRate = CBR_38400;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;   // both lines raised, as an open port has them on Linux
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb) || !setTimeouts(h, 0)) {
        const RadioError e = osError(QStringLiteral("Cannot configure %1").arg(device));
        CloseHandle(h);
        throw e;
    }
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    m_handle = h;
    m_timeoutMs = 0;
}

SerialTransport::~SerialTransport() {
    if (m_handle) CloseHandle(HANDLE(m_handle));
}

void SerialTransport::write(const QByteArray &bytes) {
    int sent = 0;
    while (sent < bytes.size()) {
        DWORD n = 0;
        if (!WriteFile(HANDLE(m_handle), bytes.constData() + sent, DWORD(bytes.size() - sent), &n, nullptr))
            throw osError(QStringLiteral("Serial port write failed"));
        if (n == 0) throw RadioError(QStringLiteral("Timed out sending to the serial port."));
        sent += int(n);
    }
}

QByteArray SerialTransport::read(int timeoutMs) {
    timeoutMs = std::max(timeoutMs, 0);
    if (timeoutMs != m_timeoutMs) {
        if (!setTimeouts(HANDLE(m_handle), timeoutMs)) throw osError(QStringLiteral("Serial port failed"));
        m_timeoutMs = timeoutMs;
    }
    char buf[512];
    DWORD n = 0;
    if (!ReadFile(HANDLE(m_handle), buf, sizeof buf, &n, nullptr)) {
        const DWORD code = GetLastError();
        if (code == ERROR_ACCESS_DENIED || code == ERROR_BAD_COMMAND || code == ERROR_DEVICE_REMOVED
            || code == ERROR_GEN_FAILURE || code == ERROR_OPERATION_ABORTED)
            throw RadioError(QStringLiteral("The serial port was disconnected."));
        SetLastError(code);
        throw osError(QStringLiteral("Serial port read failed"));
    }
    return QByteArray(buf, int(n));
}

void SerialTransport::discardInput() { PurgeComm(HANDLE(m_handle), PURGE_RXCLEAR); }

// Windows lists the serial ports that exist right now in the registry.
QStringList availableSerialPorts() {
    QStringList ports;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) != ERROR_SUCCESS) return ports;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        wchar_t data[256];
        DWORD nameLen = DWORD(std::size(name));
        DWORD dataBytes = sizeof data - sizeof(wchar_t);
        DWORD type = 0;
        const LONG r = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE *>(data), &dataBytes);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS || type != REG_SZ) continue;
        data[dataBytes / sizeof(wchar_t)] = 0;
        const QString port = QString::fromWCharArray(data);
        if (!port.isEmpty()) ports << port;
    }
    RegCloseKey(key);
    std::sort(ports.begin(), ports.end(), [](const QString &a, const QString &b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;   // COM9 before COM10
    });
    return ports;
}

} // namespace k5
