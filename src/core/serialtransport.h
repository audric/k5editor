#pragma once
#include "sync.h"

namespace k5 {

// Serial port at 38400 baud 8N1, using the operating system directly:
// serialtransport.cpp on Linux, serialtransport_win.cpp on Windows.
// The device is a path like /dev/ttyUSB0, or a name like COM3 on Windows.
class SerialTransport : public Transport {
public:
    explicit SerialTransport(const QString &device);
    ~SerialTransport() override;
    void write(const QByteArray &bytes) override;
    QByteArray read(int timeoutMs) override;
    void discardInput() override;

private:
#ifdef Q_OS_WIN
    void *m_handle = nullptr;   // HANDLE
    int m_timeoutMs = -1;       // read timeout the port is set to
#else
    int m_fd = -1;
#endif
};

QStringList availableSerialPorts();

} // namespace k5
