#include "f4hwn.h"
#include <QHash>
#include <QtEndian>

namespace k5 {

namespace {

constexpr int kAttrBase = 0x0D60;
constexpr int kNameBase = 0x0F50;
constexpr int kNameStored = 16;
constexpr int kNameShown = 10;

quint8 byteAt(const QByteArray &image, int offset) { return quint8(image[offset]); }

bool isPrintable(quint8 c) { return c >= 0x20 && c <= 0x7e; }

// Text up to the first 00 or FF, without trailing spaces.
QString readText(const QByteArray &image, int offset, int length) {
    QByteArray raw = image.mid(offset, length);
    for (int i = 0; i < raw.size(); ++i)
        if (raw[i] == '\0' || quint8(raw[i]) == 0xff) {
            raw.truncate(i);
            break;
        }
    while (raw.endsWith(' ')) raw.chop(1);
    return QString::fromLatin1(raw);
}

QString mhz(quint32 freq10Hz) { return QString::number(freq10Hz / 100000.0, 'f', 5); }

} // namespace

bool firmwareSupported(const QString &firmware) { return firmware.startsWith(QLatin1String("F4HWN v4.3")); }

QByteArray blankImage() {
    static const struct { const char *id; QVariant value; } defaults[] = {
#include "defaults.inc"
    };
    QByteArray image(MemSize, '\xff');
    for (const auto &d : defaults)
        if (!setSettingValue(image, QLatin1String(d.id), d.value)) qFatal("defaults.inc: bad value for %s", d.id);
    return image;
}

// ---------------------------------------------------------------- tables

const QList<double> &ctcssTones() {
    static const QList<double> l{
        67.0,  69.3,  71.9,  74.4,  77.0,  79.7,  82.5,  85.4,  88.5,  91.5,  94.8,  97.4,  100.0,
        103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3, 131.8, 136.5, 141.3, 146.2, 151.4, 156.7,
        159.8, 162.2, 165.5, 167.9, 171.3, 173.8, 177.3, 179.9, 183.5, 186.2, 189.9, 192.8, 196.6,
        199.5, 203.5, 206.5, 210.7, 218.1, 225.7, 229.1, 233.6, 241.8, 250.3, 254.1};
    return l;
}

const QList<int> &dcsCodes() {
    static const QList<int> l{
        23,  25,  26,  31,  32,  36,  43,  47,  51,  53,  54,  65,  71,  72,  73,  74,  114, 115,
        116, 122, 125, 131, 132, 134, 143, 145, 152, 155, 156, 162, 165, 172, 174, 205, 212, 223,
        225, 226, 243, 244, 245, 246, 251, 252, 255, 261, 263, 265, 266, 271, 274, 306, 311, 315,
        325, 331, 332, 343, 346, 351, 356, 364, 365, 371, 411, 412, 413, 423, 431, 432, 445, 446,
        452, 454, 455, 462, 464, 465, 466, 503, 506, 516, 523, 526, 532, 546, 565, 606, 612, 624,
        627, 631, 632, 654, 662, 664, 703, 712, 723, 731, 732, 734, 743, 754};
    return l;
}

const QList<double> &stepsKHz() {
    static const QList<double> l{2.5, 5,  6.25, 10, 12.5, 25, 8.33, 0.01, 0.05, 0.1, 0.25, 0.5,
                                 1,   1.25, 9,  15, 20,   30, 50,   100,  125,  200, 250,  500};
    return l;
}

const QStringList &modulationNames() {
    static const QStringList l{"FM", "AM", "USB"};
    return l;
}
const QStringList &powerNames() {
    static const QStringList l{"USER", "LOW1", "LOW2", "LOW3", "LOW4", "LOW5", "MID", "HIGH"};
    return l;
}
const QStringList &scramblerNames() {
    static const QStringList l{"OFF",    "2600Hz", "2700Hz", "2800Hz", "2900Hz", "3000Hz",
                               "3100Hz", "3200Hz", "3300Hz", "3400Hz", "3500Hz"};
    return l;
}
const QStringList &pttIdNames() {
    static const QStringList l{"OFF", "UP CODE", "DOWN CODE", "UP+DOWN CODE", "APOLLO QUINDAR"};
    return l;
}
const QStringList &companderNames() {
    static const QStringList l{"OFF", "TX", "RX", "TX/RX"};
    return l;
}

QString toneText(int type, int code) {
    if (type == 1 && code >= 0 && code < ctcssTones().size()) return QString::number(ctcssTones().at(code), 'f', 1);
    if ((type == 2 || type == 3) && code >= 0 && code < dcsCodes().size())
        return QStringLiteral("D%1%2").arg(dcsCodes().at(code), 3, 10, QLatin1Char('0')).arg(type == 2 ? 'N' : 'I');
    return {};
}

QStringList allToneTexts() {
    static const QStringList all = [] {
        QStringList l{QString()};
        for (int i = 0; i < ctcssTones().size(); ++i) l << toneText(1, i);
        for (int i = 0; i < dcsCodes().size(); ++i) l << toneText(2, i);
        for (int i = 0; i < dcsCodes().size(); ++i) l << toneText(3, i);
        return l;
    }();
    return all;
}

bool parseTone(const QString &text, int &type, int &code) {
    const QString t = text.trimmed().toUpper();
    if (t.isEmpty()) {
        type = 0;
        code = 0;
        return true;
    }
    for (int ty = 1; ty <= 3; ++ty) {
        const int n = ty == 1 ? ctcssTones().size() : dcsCodes().size();
        for (int i = 0; i < n; ++i)
            if (toneText(ty, i) == t) {
                type = ty;
                code = i;
                return true;
            }
    }
    return false;
}

// ---------------------------------------------------------------- channels

int bandForFrequency(quint32 f) {
    if (f < 1800000) return -1;        // below 18 MHz
    if (f <= 10800000) return 0;
    if (f < 13700000) return 1;
    if (f < 17400000) return 2;
    if (f < 35000000) return 3;
    if (f < 40000000) return 4;
    if (f < 47000000) return 5;
    if (f <= 130000000) return 6;      // up to 1300 MHz
    return -1;
}

Channel decodeChannel(const QByteArray &image, int index) {
    Channel c;
    const int rec = index * 16;
    c.freq = qFromLittleEndian<quint32>(image.constData() + rec);
    if (c.freq == 0 || c.freq == 0xffffffff) {
        c.freq = 0;
        return c;
    }
    c.empty = false;
    c.offset = qFromLittleEndian<quint32>(image.constData() + rec + 4);
    c.rxToneCode = byteAt(image, rec + 8);
    c.txToneCode = byteAt(image, rec + 9);
    c.txToneType = byteAt(image, rec + 10) >> 4;
    c.rxToneType = byteAt(image, rec + 10) & 0x0f;
    c.modulation = byteAt(image, rec + 11) >> 4;
    c.shift = byteAt(image, rec + 11) & 0x0f;
    const quint8 flags = byteAt(image, rec + 12);
    c.txLock = flags & 0x40;
    c.busyLock = flags & 0x20;
    c.power = (flags >> 2) & 7;
    c.narrow = flags & 0x02;
    c.reverse = flags & 0x01;
    c.pttId = (byteAt(image, rec + 13) >> 1) & 7;
    c.dtmfDecode = byteAt(image, rec + 13) & 1;
    c.step = byteAt(image, rec + 14);
    c.scrambler = byteAt(image, rec + 15);
    const quint8 attr = byteAt(image, kAttrBase + index);
    c.scanLists = attr >> 5;
    c.compander = (attr >> 3) & 3;
    c.name = readText(image, kNameBase + index * kNameStored, kNameStored).left(kNameShown);
    return c;
}

void encodeChannel(QByteArray &image, int index, const Channel &c) {
    const int rec = index * 16;
    const int nameAt = kNameBase + index * kNameStored;
    if (c.empty) {
        image.replace(rec, 16, QByteArray(16, '\xff'));
        image.replace(nameAt, kNameStored, QByteArray(kNameStored, '\xff'));
        image[kAttrBase + index] = char(0xe7);     // how F4HWN's driver marks a free slot
        return;
    }
    const quint32 oldFreq = qFromLittleEndian<quint32>(image.constData() + rec);
    const bool wasEmpty = oldFreq == 0 || oldFreq == 0xffffffff;
    const quint8 oldFlags = wasEmpty ? 0 : byteAt(image, rec + 12);
    const quint8 oldDtmf = wasEmpty ? 0 : byteAt(image, rec + 13);
    const quint8 oldAttr = byteAt(image, kAttrBase + index);

    QByteArray r(16, '\0');
    qToLittleEndian<quint32>(c.freq, r.data());
    qToLittleEndian<quint32>(c.offset, r.data() + 4);
    r[8] = char(c.rxToneCode);
    r[9] = char(c.txToneCode);
    r[10] = char((c.txToneType & 0x0f) << 4 | (c.rxToneType & 0x0f));
    r[11] = char((c.modulation & 0x0f) << 4 | (c.shift & 0x0f));
    r[12] = char((oldFlags & 0x80) | (c.txLock ? 0x40 : 0) | (c.busyLock ? 0x20 : 0) | (c.power & 7) << 2 |
                 (c.narrow ? 0x02 : 0) | (c.reverse ? 0x01 : 0));
    r[13] = char((oldDtmf & 0xf0) | (c.pttId & 7) << 1 | (c.dtmfDecode ? 1 : 0));
    r[14] = char(c.step);
    r[15] = char(c.scrambler);
    image.replace(rec, 16, r);

    int band = bandForFrequency(c.freq);
    if (band < 0) band = oldAttr & 7;
    image[kAttrBase + index] = char((c.scanLists & 7) << 5 | (c.compander & 3) << 3 | band);

    QByteArray name = c.name.left(kNameShown).toLatin1().leftJustified(kNameShown, ' ');
    name += QByteArray(kNameStored - kNameShown, '\0');
    image.replace(nameAt, kNameStored, name);
}

QStringList channelProblems(const QByteArray &image, int index) {
    QStringList problems;
    const Channel c = decodeChannel(image, index);
    if (c.empty) return problems;
    const int band = bandForFrequency(c.freq);
    const int storedBand = byteAt(image, kAttrBase + index) & 7;
    if (band < 0)
        problems << QStringLiteral("Frequency %1 MHz is outside what the radio can tune (18-1300 MHz)").arg(mhz(c.freq));
    else if (storedBand != band)
        problems << QStringLiteral("Band marker is %1 but this frequency needs %2 (left over from another firmware)").arg(storedBand).arg(band);
    if (c.modulation >= modulationNames().size()) problems << QStringLiteral("Unknown mode value %1").arg(c.modulation);
    if (c.shift > 2) problems << QStringLiteral("Unknown shift direction value %1").arg(c.shift);
    auto checkTone = [&](const char *which, int type, int code) {
        if (type > 3)
            problems << QStringLiteral("Unknown %1 tone type %2").arg(QLatin1String(which)).arg(type);
        else if ((type == 1 && code >= ctcssTones().size()) || (type >= 2 && code >= dcsCodes().size()))
            problems << QStringLiteral("%1 tone number %2 does not exist").arg(QLatin1String(which)).arg(code);
    };
    checkTone("RX", c.rxToneType, c.rxToneCode);
    checkTone("TX", c.txToneType, c.txToneCode);
    if (c.step >= stepsKHz().size()) problems << QStringLiteral("Unknown step value %1").arg(c.step);
    if (c.scrambler >= scramblerNames().size()) problems << QStringLiteral("Unknown scrambler value %1").arg(c.scrambler);
    if (c.pttId >= pttIdNames().size()) problems << QStringLiteral("Unknown PTT ID value %1").arg(c.pttId);
    const QByteArray rawName = image.mid(kNameBase + index * kNameStored, kNameStored);
    for (char ch : rawName) {
        if (ch == '\0' || quint8(ch) == 0xff) break;
        if (!isPrintable(quint8(ch))) {
            problems << QStringLiteral("Name contains unreadable characters");
            break;
        }
    }
    return problems;
}

// ---------------------------------------------------------------- settings

namespace {

enum Encoding { Plain, Vox, RxMode, PriorityChannel, Scaled10, Negated, FLock };

struct Field {
    Setting meta;
    int offset = 0;
    int shift = 0;
    int width = 8;       // bits; Text: number of bytes
    Encoding encoding = Plain;
};

const QString kDtmfDigits = QStringLiteral("0123456789ABCD");
const QString kDtmfAll = QStringLiteral("0123456789ABCD*#");
const QString kDtmfSeparators = QStringLiteral("ABCD*#");

QString minutesSeconds(int seconds) {
    if (seconds < 60) return QStringLiteral("%1 sec").arg(seconds);
    if (seconds % 60 == 0) return QStringLiteral("%1 min").arg(seconds / 60);
    return QStringLiteral("%1 min : %2 sec").arg(seconds / 60).arg(seconds % 60);
}

QStringList talkTimeList() {
    QStringList l;
    for (int i = 0; i < 5; ++i) l << QStringLiteral("N/U");
    for (int i = 5; i <= 179; ++i) l << minutesSeconds(i * 5 + 5);
    return l;
}
QStringList backlightList() {
    QStringList l{"OFF"};
    for (int i = 1; i <= 60; ++i) l << minutesSeconds(i * 5);
    l << "Always on";
    return l;
}
QStringList scanResumeList() {
    QStringList l{"STOP: stop scanning when a signal is received"};
    for (int i = 1; i <= 80; ++i)
        l << QStringLiteral("CARRIER %1.%2 s: resume this long after the signal disappears")
                 .arg(i / 4)
                 .arg((i % 4) * 25, 2, 10, QLatin1Char('0'));
    for (int s = 5; s <= 120; s += 5) l << QStringLiteral("TIMEOUT %1: listen this long, then resume").arg(minutesSeconds(s));
    return l;
}
QStringList autoLockList() {
    QStringList l{"OFF"};
    for (int i = 1; i <= 40; ++i) l << minutesSeconds(i * 15);
    return l;
}
QStringList offTimerList() {
    QStringList l{"OFF"};
    for (int m = 1; m <= 120; ++m) l << QStringLiteral("%1h:%2m").arg(m / 60).arg(m % 60, 2, 10, QLatin1Char('0'));
    return l;
}
QStringList channelList(bool withNone) {
    QStringList l;
    if (withNone) l << "None";
    for (int i = 1; i <= ChannelCount; ++i) l << QStringLiteral("Channel M%1").arg(i);
    return l;
}
QStringList numbers(int from, int to) {
    QStringList l;
    for (int i = from; i <= to; ++i) l << QString::number(i);
    return l;
}

const QStringList kOffOn{"OFF", "ON"};
const QStringList kKeyActions{"NONE", "FLASHLIGHT", "POWER", "MONITOR", "SCAN", "VOX", "ALARM", "FM RADIO", "1750Hz TONE",
                              "LOCK KEYPAD", "Main VFO (A / B)", "VFO / MEM", "MODE", "Backlight off temporarily",
                              "RX MODE", "MAIN ONLY", "PTT classic / one push", "WIDE / NARROW", "BACKLIGHT", "MUTE",
                              "POWER HIGH", "REMOVE OFFSET"};

struct Builder {
    QList<Field> fields;
    QString group;

    Field &add(const char *id, const char *label, Setting::Kind kind, int offset, int shift, int width) {
        Field f;
        f.meta.id = QLatin1String(id);
        f.meta.group = group;
        f.meta.label = QString::fromUtf8(label);
        f.meta.kind = kind;
        f.offset = offset;
        f.shift = shift;
        f.width = width;
        fields.append(f);
        return fields.last();
    }
    void list(const char *id, const char *label, int offset, const QStringList &options, int shift = 0, int width = 8,
              Encoding enc = Plain) {
        Field &f = add(id, label, Setting::List, offset, shift, width);
        f.meta.options = options;
        f.encoding = enc;
    }
    void flag(const char *id, const char *label, int offset, int shift = 0, int width = 8) {
        add(id, label, Setting::Bool, offset, shift, width);
    }
    void number(const char *id, const char *label, int offset, int min, int max, int step = 1, Encoding enc = Plain,
                int shift = 0, int width = 8) {
        Field &f = add(id, label, Setting::Int, offset, shift, width);
        f.meta.min = min;
        f.meta.max = max;
        f.meta.step = step;
        f.encoding = enc;
    }
    void text(const char *id, const char *label, int offset, int stored, int maxLength, const QString &allowed = {}) {
        Field &f = add(id, label, Setting::Text, offset, 0, stored);
        f.meta.maxLength = maxLength;
        f.meta.allowedChars = allowed;
    }
};

const QList<Field> &fields() {
    static const QList<Field> all = [] {
        Builder b;

        b.group = "Radio";
        b.number("squelch", "Squelch (Sql)", 0xe71, 0, 9);
        b.list("rx_mode", "RX mode", 0xe7a, {"MAIN ONLY", "DUAL RX RESPOND", "CROSS BAND", "MAIN TX DUAL RX"}, 0, 8, RxMode);
        b.list("TX_VFO", "Main VFO", 0xeab, {"A", "B"});
        b.list("tot", "Max TX time (TxTOut)", 0xe72, talkTimeList());
        b.list("set_pwr", "Power used by channels set to USER (SetPwr)", 0x1ff7, {"< 20mW", "125mW", "250mW", "500mW", "1W", "2W", "5W"}, 4, 4);
        b.list("mic_gain", "Mic gain (Mic)", 0xe77, {"+1.1dB", "+4.0dB", "+8.0dB", "+12.0dB", "+15.1dB"});
        b.list("vox", "Voice-operated transmit (VOX)", 0xe75, QStringList{"OFF"} + numbers(1, 10), 0, 8, Vox);
        b.list("roger_beep", "End of transmission beep (Roger)", 0xea9, {"OFF", "Roger beep", "MDC data burst"});
        b.flag("ste", "Squelch tail elimination (STE)", 0xe7e, 0, 1);
        b.list("rp_ste", "Repeater squelch tail elimination (RP STE)", 0xeaa,
               {"OFF", "100ms", "200ms", "300ms", "400ms", "500ms", "600ms", "700ms", "800ms", "900ms", "1000ms"});
        b.list("set_nfm", "Narrow FM width (SetNFM)", 0xe7e, {"NARROW", "NARROWER"}, 1, 2);
        b.list("set_ptt", "PTT mode (SetPtt)", 0x1ff7, {"CLASSIC", "ONEPUSH"}, 0, 4);
        b.list("set_tot", "TX timeout indicator (SetTot)", 0x1ff6, {"OFF", "SOUND", "VISUAL", "ALL"}, 4, 4);
        b.list("set_eot", "End of transmission indicator (SetEot)", 0x1ff6, {"OFF", "SOUND", "VISUAL", "ALL"}, 0, 4);
        b.list("call_channel", "One-key call channel (1 Call)", 0xe70, channelList(false));
        b.flag("freq_mode_allowed", "Frequency (VFO) mode allowed", 0xe7f);
        b.list("battery_save", "Battery saver (BatSav)", 0xe7b, {"OFF", "1:1", "1:2", "1:3", "1:4", "1:5"});
        b.list("Battery_type", "Battery type (BatTyp)", 0xeac, {"1600 mAh K5", "2200 mAh K5", "3500 mAh K5", "1500 mAh K1", "2500 mAh K1"});
        b.list("alarm_mode", "Alarm mode", 0xea8, {"SITE", "TONE"});

        b.group = "Display and sound";
        b.list("channel_display_mode", "Channel display (ChDisp)", 0xe79, {"Frequency", "Channel number", "Name", "Name + frequency"});
        b.list("welcome_mode", "Power-on screen (POnMsg)", 0xe97,
               {"Message line 1, voltage, sound (ALL)", "Two short sounds (SOUND)", "Message lines 1 and 2 (MESSAGE)", "Battery voltage (VOLTAGE)", "NONE"});
        b.text("logo1", "Power-on message line 1", 0xeb0, 16, 12);
        b.text("logo2", "Power-on message line 2", 0xec0, 16, 12);
        b.list("battery_text", "Battery level display (BatTXT)", 0xf47, {"NONE", "VOLTAGE", "PERCENT"}, 2, 2);
        b.list("backlight_time", "Backlight time (BLTime)", 0xe7d, backlightList());
        b.list("backlight_min", "Minimum backlight level (BLMin)", 0xe78, numbers(0, 10), 4, 4);
        b.list("backlight_max", "Maximum backlight level (BLMax)", 0xe78, numbers(0, 10), 0, 4);
        b.list("backlight_on_TX_RX", "Backlight on TX/RX (BLTxRx)", 0xf47, {"OFF", "TX", "RX", "TX/RX"}, 6, 2);
        b.number("set_contrast", "Display contrast (SetCtr)", 0x1ff5, 0, 15, 1, Plain, 0, 4);
        b.list("set_inv", "Invert display (SetInv)", 0x1ff5, kOffOn, 4, 1);
        b.list("set_met", "S-meter style (SetMet)", 0x1ff5, {"TINY", "CLASSIC"}, 6, 1);
        b.list("set_gui", "Text style (SetGui)", 0x1ff5, {"TINY", "CLASSIC"}, 7, 1);
        b.flag("mic_bar", "Microphone level bar (MicBar)", 0xf47, 4, 1);
        b.flag("button_beep", "Keypad beep (Beep)", 0xe90, 0, 1);
        b.list("voice", "Voice prompts", 0xea0, {"OFF", "Chinese", "English"});
        b.number("s0_level", "S-meter S0 level (dBm)", 0xea1, -200, -90, 1, Negated);
        b.number("s9_level", "S-meter S9 level (dBm)", 0xea2, -160, -50, 1, Negated);

        b.group = "Keys and lock";
        b.list("key1_shortpress_action", "Side key 1, short press (F1Shrt)", 0xe91, kKeyActions);
        b.list("key1_longpress_action", "Side key 1, long press (F1Long)", 0xe92, kKeyActions);
        b.list("key2_shortpress_action", "Side key 2, short press (F2Shrt)", 0xe93, kKeyActions);
        b.list("key2_longpress_action", "Side key 2, long press (F2Long)", 0xe94, kKeyActions);
        b.list("keyM_longpress_action", "Menu key, long press (M Long)", 0xe90, kKeyActions, 1, 7);
        b.flag("key_lock", "Keypad locked", 0xe74, 0, 1);
        b.list("auto_keypad_lock", "Lock keypad automatically after (KeyLck)", 0xe96, autoLockList());
        b.list("set_lck", "What the keypad lock blocks (SetLck)", 0x1ff5, {"KEYS", "KEYS+PTT"}, 5, 1);
        b.list("set_key", "Key used by RescueOps (SetKEY)", 0xe74, {"MENU", "KEY_UP", "KEY_DOWN", "KEY_EXIT", "KEY_STAR"}, 2, 4);
        b.list("set_menu_lock", "RescueOps menu lock", 0xe74, kOffOn, 1, 1);
        b.list("set_tmr", "Timer (SetTmr)", 0x1ff4, kOffOn, 0, 1);
        b.list("set_off_tmr", "Switch off after (SetOff)", 0x1ff4, offTimerList(), 1, 7);

        b.group = "Scanning";
        b.list("scan_resume_mode", "Scan resume (ScnRev)", 0xe95, scanResumeList());
        b.list("slDef", "Default scan list (SList)", 0xf18, {"No list", "List 1", "List 2", "List 3", "Lists 1, 2, 3", "All channels"});
        b.flag("sl1PriorEnab", "List 1: priority channel scan", 0xf19, 0, 1);
        b.list("sl1PriorCh1", "List 1: priority channel 1", 0xf1a, channelList(true), 0, 8, PriorityChannel);
        b.list("sl1PriorCh2", "List 1: priority channel 2", 0xf1b, channelList(true), 0, 8, PriorityChannel);
        b.flag("sl2PriorEnab", "List 2: priority channel scan", 0xf19, 1, 1);
        b.list("sl2PriorCh1", "List 2: priority channel 1", 0xf1c, channelList(true), 0, 8, PriorityChannel);
        b.list("sl2PriorCh2", "List 2: priority channel 2", 0xf1d, channelList(true), 0, 8, PriorityChannel);
        b.flag("sl3PriorEnab", "List 3: priority channel scan", 0xf19, 2, 1);
        b.list("sl3PriorCh1", "List 3: priority channel 1", 0xf1e, channelList(true), 0, 8, PriorityChannel);
        b.list("sl3PriorCh2", "List 3: priority channel 2", 0xf1f, channelList(true), 0, 8, PriorityChannel);

        b.group = "DTMF";
        b.text("dtmf_dtmf_local_code", "Own ID (3 characters, 0-9 A-D)", 0xee0, 3, 3, kDtmfDigits);
        b.text("dtmf_dtmf_up_code", "Up code, sent at start of TX (UPCode)", 0xef8, 16, 16, kDtmfAll);
        b.text("dtmf_dtmf_down_code", "Down code, sent at end of TX (DWCode)", 0xf08, 16, 16, kDtmfAll);
        b.flag("dtmf_side_tone", "Play DTMF on the speaker when sending (D ST)", 0xed0);
        b.flag("live_DTMF_decoder", "Show received DTMF codes", 0xf47, 1, 1);
        b.list("dtmf_decode_response", "Decode response (D Resp)", 0xed3, {"Do nothing", "Ring", "Reply", "Ring and reply"});
        b.number("dtmf_auto_reset_time", "Auto reset time, s (D Hold)", 0xed4, 5, 60);
        b.number("dtmf_preload_time", "Pre-load time, ms (D Prel)", 0xed5, 30, 990, 10, Scaled10);
        b.number("dtmf_first_code_persist_time", "First code persist time, ms", 0xed6, 30, 1000, 10, Scaled10);
        b.number("dtmf_hash_persist_time", "* and # persist time, ms", 0xed7, 30, 1000, 10, Scaled10);
        b.number("dtmf_code_persist_time", "Code persist time, ms", 0xed8, 30, 1000, 10, Scaled10);
        b.number("dtmf_code_interval_time", "Code interval time, ms", 0xed9, 30, 1000, 10, Scaled10);
        b.text("dtmf_separate_code", "Separate code (one of A-D * #)", 0xed1, 1, 1, kDtmfSeparators);
        b.text("dtmf_group_call_code", "Group call code (one of A-D * #)", 0xed2, 1, 1, kDtmfSeparators);
        b.flag("dtmf_permit_remote_kill", "Permit remote kill", 0xeda);
        b.text("dtmf_kill_code", "Kill code (5 characters, 0-9 A-D)", 0xee8, 5, 5, kDtmfDigits);
        b.text("dtmf_revive_code", "Revive code (5 characters, 0-9 A-D)", 0xef0, 5, 5, kDtmfDigits);

        b.group = "Unlock";
        b.list("int_flock", "TX frequency lock (F Lock)", 0xf40,
               {"DEFAULT+ (137-174, 400-470)", "FCC HAM (144-148, 420-450)", "CA HAM (144-148, 430-450)",
                "CE HAM (144-146, 430-440)", "GB HAM (144-148, 430-440)", "137-174, 400-430", "137-174, 400-438",
                "PMR 446", "GMRS FRS MURS", "DISABLE ALL", "UNLOCK ALL"},
               0, 8, FLock);
        b.flag("int_350en", "Receive 350-400 MHz (350 En)", 0xf45);
        b.flag("int_scren", "Scrambler enabled (ScraEn)", 0xf46);
        return b.fields;
    }();
    return all;
}

const Field *findField(const QString &id) {
    static const QHash<QString, const Field *> index = [] {
        QHash<QString, const Field *> h;
        for (const Field &f : fields()) h.insert(f.meta.id, &f);
        return h;
    }();
    return index.value(id, nullptr);
}

// A byte holding several small settings that still reads FF was never saved by the radio.
bool isUnsetSharedByte(const QByteArray &image, const Field &f) {
    return f.meta.kind != Setting::Text && f.width < 8 && byteAt(image, f.offset) == 0xff;
}

// What the firmware assumes for such a byte: everything off, contrast 11.
quint8 defaultSharedByte(int offset) { return offset == 0x1ff5 ? 0x0b : 0x00; }

int rawValue(const QByteArray &image, const Field &f) {
    return (byteAt(image, f.offset) >> f.shift) & ((1 << f.width) - 1);
}

void setRawValue(QByteArray &image, const Field &f, int value) {
    const quint8 current = isUnsetSharedByte(image, f) ? defaultSharedByte(f.offset) : byteAt(image, f.offset);
    const int mask = ((1 << f.width) - 1) << f.shift;
    image[f.offset] = char((current & ~mask) | ((value << f.shift) & mask));
}

constexpr int kVoxSwitch = 0xe75, kVoxLevel = 0xe76, kCrossBand = 0xe7a, kDualWatch = 0xe7c, kTxVfo = 0xeab;

} // namespace

const QList<Setting> &settings() {
    static const QList<Setting> all = [] {
        QList<Setting> l;
        for (const Field &f : fields()) l << f.meta;
        return l;
    }();
    return all;
}

const Setting *findSetting(const QString &id) {
    const Field *f = findField(id);
    return f ? &f->meta : nullptr;
}

QVariant settingValue(const QByteArray &image, const QString &id) {
    const Field *f = findField(id);
    if (!f || image.size() != MemSize) return {};
    const Setting &s = f->meta;
    switch (s.kind) {
    case Setting::Text:
        return readText(image, f->offset, s.maxLength);
    case Setting::Bool: {
        if (isUnsetSharedByte(image, *f)) return false;
        const int raw = rawValue(image, *f);
        return raw == 1;      // the firmware treats anything else in a whole byte as "not set"
    }
    case Setting::Int: {
        if (isUnsetSharedByte(image, *f)) return -1000;      // below every range: shown as not set
        const int raw = rawValue(image, *f);
        if (f->encoding == Scaled10) return raw * 10;
        if (f->encoding == Negated) return -raw;
        return raw;
    }
    case Setting::List: {
        int value;
        switch (f->encoding) {
        case Vox:
            value = byteAt(image, kVoxSwitch) == 1 ? byteAt(image, kVoxLevel) + 1 : (byteAt(image, kVoxSwitch) == 0 ? 0 : -1);
            break;
        case RxMode:
            value = (byteAt(image, kCrossBand) ? 2 : 0) + (byteAt(image, kDualWatch) ? 1 : 0);
            break;
        case PriorityChannel: {
            const int raw = rawValue(image, *f);
            value = raw < ChannelCount ? raw + 1 : 0;
            break;
        }
        default:
            value = isUnsetSharedByte(image, *f) ? -1 : rawValue(image, *f);
        }
        return value >= 0 && value < s.options.size() ? value : -1;
    }
    }
    return {};
}

bool setSettingValue(QByteArray &image, const QString &id, const QVariant &value) {
    const Field *f = findField(id);
    if (!f || image.size() != MemSize) return false;
    const Setting &s = f->meta;
    switch (s.kind) {
    case Setting::Text: {
        QString text = value.toString();
        if (!s.allowedChars.isEmpty()) text = text.toUpper();
        if (text.size() > s.maxLength) return false;
        for (const QChar ch : text) {
            if (s.allowedChars.isEmpty() ? !(ch.unicode() >= 0x20 && ch.unicode() <= 0x7e) : !s.allowedChars.contains(ch))
                return false;
        }
        QByteArray stored = text.toLatin1().leftJustified(s.maxLength, '\0');
        if (f->width == 16 && s.maxLength == 12) stored += QByteArray("\x00\xff\xff\xff", 4);   // power-on message layout
        image.replace(f->offset, stored.size(), stored);
        return true;
    }
    case Setting::Bool:
        setRawValue(image, *f, value.toBool() ? 1 : 0);
        return true;
    case Setting::Int: {
        bool ok = false;
        const int v = value.toInt(&ok);
        if (!ok || v < s.min || v > s.max) return false;
        if (f->encoding == Scaled10) setRawValue(image, *f, v / 10);
        else if (f->encoding == Negated) setRawValue(image, *f, -v);
        else setRawValue(image, *f, v);
        return true;
    }
    case Setting::List: {
        bool ok = false;
        const int v = value.toInt(&ok);
        if (!ok || v < 0 || v >= s.options.size()) return false;
        if (s.options.at(v) == QLatin1String("N/U")) return false;      // values the firmware does not use
        switch (f->encoding) {
        case Vox:
            image[kVoxSwitch] = char(v > 0 ? 1 : 0);
            image[kVoxLevel] = char(v > 0 ? v - 1 : 0);
            return true;
        case RxMode: {
            const int mainVfo = byteAt(image, kTxVfo) + 1;
            image[kCrossBand] = char(v & 2 ? mainVfo : 0);
            image[kDualWatch] = char(v & 1 ? mainVfo : 0);
            return true;
        }
        case PriorityChannel:
            setRawValue(image, *f, v == 0 ? 0xff : v - 1);
            return true;
        case FLock:
            // Like the firmware author's driver: "UNLOCK ALL" can only be switched on in the radio's own menu.
            if (v == 10 && rawValue(image, *f) != 10) return false;
            setRawValue(image, *f, v);
            return true;
        default:
            setRawValue(image, *f, v);
            return true;
        }
    }
    }
    return false;
}

} // namespace k5
