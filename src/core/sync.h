// Talking to the radio and writing changes back safely.
#pragma once
#include "f4hwn.h"
#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>
#include <stdexcept>

namespace k5 {

struct RadioError : std::runtime_error {
    explicit RadioError(const QString &what) : std::runtime_error(what.toStdString()) {}
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual void write(const QByteArray &bytes) = 0;
    // Returns whatever arrived, waiting up to timeoutMs; empty on timeout.
    virtual QByteArray read(int timeoutMs) = 0;
    virtual void discardInput() = 0;
};

class Radio {
public:
    using Progress = std::function<void(int done, int total)>;
    explicit Radio(Transport &transport, int timeoutMs = 1500) : m_transport(transport), m_timeoutMs(timeoutMs) {}
    QString hello();                                   // firmware version string
    QByteArray readBlock(quint16 offset, int length);
    QByteArray readAll(const Progress &progress = {});
    void writeBlock(quint16 offset, const QByteArray &data);
    void reset();

private:
    QByteArray exchange(const QByteArray &command);
    Transport &m_transport;
    int m_timeoutMs;
    QByteArray m_buffer;
};

// Areas this program may write: channels and settings (below 0x1D00) and the
// F4HWN extra settings (0x1FF2..0x1FF9). Calibration is never written.
constexpr int ProgEnd = 0x1D00;
constexpr int ExtraStart = 0x1FF2;
constexpr int ExtraLength = 8;
bool isWritable(int offset);

struct WriteBlock {
    int offset = 0;
    QByteArray data;
};
// Blocks that turn `base` into `work`, limited to the writable areas.
QList<WriteBlock> planWrites(const QByteArray &base, const QByteArray &work);

struct WriteOptions {
    QString backupDir;
    bool overwriteConflicts = false;
    std::function<void(const QString &stage, int done, int total)> progress;
};
struct WriteResult {
    enum Status { Ok, NothingToWrite, UnsupportedFirmware, Conflict, VerifyFailed };
    Status status = Ok;
    QString firmware;
    QString backupPath;
    QList<int> conflictOffsets;   // blocks the radio changed since `base` was read
    QByteArray radioImage;        // radio memory after the operation
    int blocksWritten = 0;
};
// base: image as last read from the radio; work: the edited image.
WriteResult writeToRadio(Radio &radio, const QByteArray &base, const QByteArray &work,
                         const WriteOptions &options);

} // namespace k5
