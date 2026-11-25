// Memory layout of the F4HWN v4.3 firmware (UV-K5 with the DP32G030 chip),
// transcribed from the firmware author's CHIRP driver (uvk5_egzumer_f4hwn_ver_4_3_0.py).
#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace k5 {

constexpr int MemSize = 0x2000;
constexpr int ChannelCount = 200;

bool firmwareSupported(const QString &firmware);
// An image with no channels and the settings of defaults.inc. Everything else is
// erased (FF), and the firmware uses its own default for what is not set.
QByteArray blankImage();

struct Channel {
    bool empty = true;
    quint32 freq = 0;       // in 10 Hz units
    quint32 offset = 0;     // in 10 Hz units
    int shift = 0;          // 0 none, 1 plus, 2 minus
    int rxToneType = 0;     // 0 none, 1 CTCSS, 2 DCS, 3 DCS inverted
    int rxToneCode = 0;     // index into ctcssTones() or dcsCodes()
    int txToneType = 0;
    int txToneCode = 0;
    int modulation = 0;     // index into modulationNames()
    bool narrow = false;
    int power = 0;          // index into powerNames()
    int step = 0;           // index into stepsKHz()
    int scrambler = 0;      // index into scramblerNames()
    bool txLock = false;
    bool busyLock = false;
    bool reverse = false;
    int pttId = 0;          // index into pttIdNames()
    bool dtmfDecode = false;
    int scanLists = 0;      // bit 0 = list 1, bit 1 = list 2, bit 2 = list 3
    int compander = 0;      // index into companderNames()
    QString name;

    bool operator==(const Channel &) const = default;
};

Channel decodeChannel(const QByteArray &image, int index);
// Writes the channel, keeping bits this program does not understand, and sets
// the band marker from the frequency. An empty channel is erased.
void encodeChannel(QByteArray &image, int index, const Channel &channel);
// Human-readable reasons why the stored channel looks damaged; empty if fine.
QStringList channelProblems(const QByteArray &image, int index);
// Band number the firmware expects for this frequency, or -1 if out of range.
int bandForFrequency(quint32 freq10Hz);

const QList<double> &ctcssTones();
const QList<int> &dcsCodes();
const QList<double> &stepsKHz();
const QStringList &modulationNames();
const QStringList &powerNames();
const QStringList &scramblerNames();
const QStringList &pttIdNames();
const QStringList &companderNames();
QString toneText(int type, int code);            // "", "88.5", "D023N", "D023I"
bool parseTone(const QString &text, int &type, int &code);
QStringList allToneTexts();

struct Setting {
    enum Kind { List, Bool, Int, Text };
    QString id;
    QString group;
    QString label;
    Kind kind = List;
    QStringList options;    // List: position = stored value
    int min = 0, max = 0, step = 1;   // Int
    int maxLength = 0;      // Text
    QString allowedChars;   // Text: empty = printable ASCII
};

const QList<Setting> &settings();
const Setting *findSetting(const QString &id);
// List -> int position (-1 if the stored value is not in the list), Bool -> bool,
// Int -> int, Text -> QString.
QVariant settingValue(const QByteArray &image, const QString &id);
// Returns false and leaves the image untouched if the value is not acceptable.
bool setSettingValue(QByteArray &image, const QString &id, const QVariant &value);

} // namespace k5
