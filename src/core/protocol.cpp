#include "protocol.h"

namespace k5 {

namespace {
const quint8 kXor[16] = {22, 108, 20, 230, 46, 145, 13, 64, 33, 53, 213, 64, 19, 3, 233, 128};
const QByteArray kTimestamp("\x6a\x39\x57\x64", 4);   // any value; must be the same in every command

QByteArray obfuscate(const QByteArray &data) {
    QByteArray out = data;
    for (int i = 0; i < out.size(); ++i) out[i] = char(quint8(out[i]) ^ kXor[i % 16]);
    return out;
}

quint16 crc16(const QByteArray &data) {
    quint32 crc = 0;
    for (char ch : data) {
        crc ^= quint32(quint8(ch)) << 8;
        for (int i = 0; i < 8; ++i) {
            crc <<= 1;
            if (crc & 0x10000) crc = (crc ^ 0x1021) & 0xFFFF;
        }
    }
    return quint16(crc);
}

void appendLE16(QByteArray &b, quint16 v) {
    b += char(v & 0xff);
    b += char(v >> 8);
}
} // namespace

QByteArray buildPacket(const QByteArray &payload) {
    QByteArray body = payload;
    appendLE16(body, crc16(payload));
    QByteArray packet("\xab\xcd", 2);
    packet += char(payload.size());
    packet += '\0';
    packet += obfuscate(body);
    packet += "\xdc\xba";
    return packet;
}

std::optional<QByteArray> takePacket(QByteArray &buffer) {
    for (;;) {
        const int start = buffer.indexOf(QByteArray("\xab\xcd", 2));
        if (start < 0) {
            // keep a trailing AB in case its CD has not arrived yet
            if (buffer.endsWith('\xab')) buffer = buffer.right(1);
            else buffer.clear();
            return std::nullopt;
        }
        buffer.remove(0, start);
        if (buffer.size() < 4) return std::nullopt;
        const int length = quint8(buffer[2]);
        const int total = 4 + length + 4;        // header, payload, CRC, DC BA
        if (buffer[3] != '\0') { buffer.remove(0, 2); continue; }
        if (buffer.size() < total) return std::nullopt;
        if (buffer.mid(total - 2, 2) != QByteArray("\xdc\xba", 2)) { buffer.remove(0, 2); continue; }
        const QByteArray payload = obfuscate(buffer.mid(4, length));
        buffer.remove(0, total);
        return payload;
    }
}

QByteArray helloCommand() { return QByteArray("\x14\x05\x04\x00", 4) + kTimestamp; }

QByteArray readCommand(quint16 offset, quint8 length) {
    QByteArray c("\x1b\x05\x08\x00", 4);
    appendLE16(c, offset);
    c += char(length);
    c += '\0';
    return c + kTimestamp;
}

QByteArray writeCommand(quint16 offset, const QByteArray &data) {
    QByteArray c("\x1d\x05", 2);
    c += char(data.size() + 8);
    c += '\0';
    appendLE16(c, offset);
    c += char(data.size());
    c += '\x01';
    return c + kTimestamp + data;
}

QByteArray resetCommand() { return QByteArray("\xdd\x05\x00\x00", 4); }

} // namespace k5
