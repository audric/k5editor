#pragma once
#include "sync.h"

namespace k5 {

// Serial port at 38400 baud 8N1, using the operating system directly (Linux).
class SerialTransport : public Transport {
public:
    explicit SerialTransport(const QString &device);
    ~SerialTransport() override;
    void write(const QByteArray &bytes) override;
    QByteArray read(int timeoutMs) override;
    void discardInput() override;

private:
    int m_fd = -1;
};

QStringList availableSerialPorts();

} // namespace k5
