// A simulated radio for tests: answers the serial protocol from an in-memory image.
#pragma once
#include "protocol.h"
#include "sync.h"
#include <QList>
#include <QPair>
#include <functional>

class FakeRadio : public k5::Transport {
public:
    QByteArray eeprom;
    QString firmware = "F4HWN v4.3";
    QList<QPair<int, int>> writes;          // offset, length
    bool wasReset = false;
    QByteArray noiseBeforeNextReply;        // e.g. boot chatter
    int ignoreCommands = 0;                 // drop this many commands (no reply)
    std::function<void(int offset)> beforeWrite;

    void write(const QByteArray &bytes) override {
        m_in += bytes;
        while (auto cmd = k5::takePacket(m_in)) handle(*cmd);
    }
    QByteArray read(int) override {
        QByteArray out = m_out;
        m_out.clear();
        return out;
    }
    void discardInput() override { m_out.clear(); }

private:
    void reply(const QByteArray &payload) {
        m_out += noiseBeforeNextReply + k5::buildPacket(payload);
        noiseBeforeNextReply.clear();
    }
    void handle(const QByteArray &c) {
        if (ignoreCommands > 0) { --ignoreCommands; return; }
        const quint8 op = quint8(c[0]);
        if (op == 0x14) {
            QByteArray r("\x15\x05\x24\x00", 4);
            r += firmware.toLatin1().leftJustified(16, '\0') + QByteArray(20, '\0');
            reply(r);
        } else if (op == 0x1b) {
            const int off = quint8(c[4]) | quint8(c[5]) << 8, len = quint8(c[6]);
            QByteArray r("\x1c\x05", 2);
            r += char(len + 4); r += '\0'; r += c[4]; r += c[5]; r += char(len); r += '\0';
            reply(r + eeprom.mid(off, len));
        } else if (op == 0x1d) {
            const int off = quint8(c[4]) | quint8(c[5]) << 8, len = quint8(c[6]);
            if (beforeWrite) beforeWrite(off);
            eeprom.replace(off, len, c.mid(12, len));
            writes.append({off, len});
            QByteArray r("\x1e\x05\x02\x00", 4);
            r += c[4]; r += c[5];
            reply(r);
        } else if (op == 0xdd) {
            wasReset = true;
        }
    }
    QByteArray m_in, m_out;
};
