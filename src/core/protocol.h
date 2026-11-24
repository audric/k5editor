// Serial packet format of the Quansheng UV-K5 family (38400 baud, 8N1).
#pragma once
#include <QByteArray>
#include <optional>

namespace k5 {

// Wraps a command payload: AB CD, length, 00, obfuscated(payload + CRC16), DC BA.
QByteArray buildPacket(const QByteArray &payload);

// Removes the first complete packet from `buffer` and returns its payload.
// Bytes before the packet (boot noise, other frame types) are discarded.
// Returns nullopt when no complete packet is buffered yet.
std::optional<QByteArray> takePacket(QByteArray &buffer);

QByteArray helloCommand();
QByteArray readCommand(quint16 offset, quint8 length);
QByteArray writeCommand(quint16 offset, const QByteArray &data);
QByteArray resetCommand();

} // namespace k5
