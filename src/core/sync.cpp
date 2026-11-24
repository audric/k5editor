#include "sync.h"
#include "protocol.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>

namespace k5 {

QByteArray Radio::exchange(const QByteArray &command) {
    m_transport.write(buildPacket(command));
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        if (auto reply = takePacket(m_buffer)) return *reply;
        const qint64 left = m_timeoutMs - timer.elapsed();
        if (left <= 0) throw RadioError(QStringLiteral("The radio did not answer. Check that it is switched on and the cable is pushed fully in."));
        m_buffer += m_transport.read(int(left));
    }
}

QString Radio::hello() {
    for (int attempt = 0;; ++attempt) {
        m_transport.discardInput();
        m_buffer.clear();
        try {
            const QByteArray r = exchange(helloCommand());
            if (r.size() >= 20 && quint8(r[0]) == 0x15) {
                QByteArray name = r.mid(4, 16);
                const int end = name.indexOf('\0');
                if (end >= 0) name.truncate(end);
                return QString::fromLatin1(name);
            }
            if (attempt >= 2) throw RadioError(QStringLiteral("Unexpected answer from the radio."));
        } catch (const RadioError &) {
            if (attempt >= 2) throw;
        }
    }
}

QByteArray Radio::readBlock(quint16 offset, int length) {
    const QByteArray r = exchange(readCommand(offset, quint8(length)));
    if (r.size() != 8 + length || quint8(r[0]) != 0x1c || quint8(r[4]) != (offset & 0xff) || quint8(r[5]) != (offset >> 8))
        throw RadioError(QStringLiteral("Bad answer while reading at 0x%1.").arg(offset, 4, 16, QLatin1Char('0')));
    return r.mid(8);
}

QByteArray Radio::readAll(const Progress &progress) {
    QByteArray image;
    image.reserve(MemSize);
    for (int offset = 0; offset < MemSize; offset += 0x80) {
        image += readBlock(quint16(offset), 0x80);
        if (progress) progress(offset + 0x80, MemSize);
    }
    return image;
}

void Radio::writeBlock(quint16 offset, const QByteArray &data) {
    if (data.isEmpty() || data.size() % 8 != 0 || data.size() > 0x80)
        throw RadioError(QStringLiteral("Internal error: bad write block size."));
    for (int i = 0; i < data.size(); ++i)
        if (!isWritable(offset + i)) throw RadioError(QStringLiteral("Internal error: refusing to write a protected area."));
    const QByteArray r = exchange(writeCommand(offset, data));
    if (r.size() < 6 || quint8(r[0]) != 0x1e || quint8(r[4]) != (offset & 0xff) || quint8(r[5]) != (offset >> 8))
        throw RadioError(QStringLiteral("Bad answer while writing at 0x%1.").arg(offset, 4, 16, QLatin1Char('0')));
}

void Radio::reset() { m_transport.write(buildPacket(resetCommand())); }

bool isWritable(int offset) {
    return (offset >= 0 && offset < ProgEnd) || (offset >= ExtraStart && offset < ExtraStart + ExtraLength);
}

QList<WriteBlock> planWrites(const QByteArray &base, const QByteArray &work) {
    QList<WriteBlock> plan;
    if (base.size() != MemSize || work.size() != MemSize) return plan;
    for (int offset = 0; offset < ProgEnd; offset += 8) {
        if (base.mid(offset, 8) == work.mid(offset, 8)) continue;
        if (!plan.isEmpty() && plan.last().offset + plan.last().data.size() == offset && plan.last().data.size() < 0x80)
            plan.last().data += work.mid(offset, 8);
        else
            plan.append({offset, work.mid(offset, 8)});
    }
    if (base.mid(ExtraStart, ExtraLength) != work.mid(ExtraStart, ExtraLength))
        plan.append({ExtraStart, work.mid(ExtraStart, ExtraLength)});
    return plan;
}

static QString saveBackup(const QString &dir, const QString &firmware, const QByteArray &image) {
    if (!QDir().mkpath(dir)) throw RadioError(QStringLiteral("Cannot create the backup folder %1. Nothing was written to the radio.").arg(dir));
    QString fw = firmware;
    fw.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9.]+")), QStringLiteral("-"));
    const QString stem = QStringLiteral("%1/radio_%2_%3").arg(dir, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss")), fw);
    QString path = stem + QStringLiteral(".bin");
    for (int n = 2; QFile::exists(path); ++n) path = QStringLiteral("%1_%2.bin").arg(stem).arg(n);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(image) != image.size() || !f.flush())
        throw RadioError(QStringLiteral("Cannot save the backup %1. Nothing was written to the radio.").arg(path));
    return path;
}

WriteResult writeToRadio(Radio &radio, const QByteArray &base, const QByteArray &work, const WriteOptions &options) {
    WriteResult result;
    const QList<WriteBlock> plan = planWrites(base, work);
    if (plan.isEmpty()) {
        result.status = WriteResult::NothingToWrite;
        return result;
    }
    auto report = [&](const QString &stage, int done, int total) {
        if (options.progress) options.progress(stage, done, total);
    };

    result.firmware = radio.hello();
    if (!firmwareSupported(result.firmware)) {
        result.status = WriteResult::UnsupportedFirmware;
        return result;
    }

    const QByteArray current = radio.readAll([&](int d, int t) { report(QStringLiteral("Reading the radio"), d, t); });
    result.radioImage = current;
    result.backupPath = saveBackup(options.backupDir, result.firmware, current);

    for (const WriteBlock &block : plan)
        for (int o = block.offset; o < block.offset + block.data.size(); o += 8)
            if (current.mid(o, 8) != base.mid(o, 8)) result.conflictOffsets.append(o);
    if (!result.conflictOffsets.isEmpty() && !options.overwriteConflicts) {
        result.status = WriteResult::Conflict;
        return result;
    }

    for (const WriteBlock &block : plan) {
        radio.writeBlock(quint16(block.offset), block.data);
        report(QStringLiteral("Writing"), ++result.blocksWritten, int(plan.size()));
    }

    const QByteArray after = radio.readAll([&](int d, int t) { report(QStringLiteral("Verifying"), d, t); });
    result.radioImage = after;
    for (const WriteBlock &block : plan)
        if (after.mid(block.offset, block.data.size()) != block.data) {
            result.status = WriteResult::VerifyFailed;
            return result;
        }

    radio.reset();
    result.status = WriteResult::Ok;
    return result;
}

} // namespace k5
