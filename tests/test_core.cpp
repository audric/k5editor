#include "f4hwn.h"
#include "fakeradio.h"
#include "protocol.h"
#include "sync.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace k5;

static QByteArray fixture(const char *name) {
    QFile f(QStringLiteral(FIXTURE_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly)) qFatal("missing fixture %s", name);
    return f.readAll();
}
static QByteArray hex(const char *h) { return QByteArray::fromHex(h); }
static int slotByName(const QByteArray &img, const QString &name) {
    for (int i = 0; i < ChannelCount; ++i)
        if (decodeChannel(img, i).name == name) return i;
    return -1;
}

class TestCore : public QObject {
    Q_OBJECT
private slots:
    // ---- packets (expected bytes produced by an independent script that was proven against a real radio)
    void helloPacketMatchesKnownBytes() {
        QCOMPARE(buildPacket(helloCommand()), hex("abcd0800026910e644a85a24b9a9dcba"));
    }
    void readPacketMatchesKnownBytes() {
        QCOMPARE(buildPacket(readCommand(0x0e70, 0x80)), hex("abcd0c000d691ce65e9f8d404b0c8224d26edcba"));
    }
    void writePacketMatchesKnownBytes() {
        QCOMPARE(buildPacket(writeCommand(0x0140, hex("0001020304050607"))),
                 hex("abcd14000b6904e66e9005414b0c82241302eb83126912e1b5dfdcba"));
    }
    void resetPacketMatchesKnownBytes() {
        QCOMPARE(buildPacket(resetCommand()), hex("abcd0400cb6914e65bebdcba"));
    }
    void decodesHelloReply() {
        QByteArray buf = hex("abcd2800036930e668a545176f15a3743d30e980166c14e62e910d402135d5401303e980166c14e62e910d4092f9dcba");
        auto p = takePacket(buf);
        QVERIFY(p.has_value());
        QCOMPARE(p->left(14), hex("15052400463448574e2076342e33"));   // 15 05 24 00 "F4HWN v4.3"
        QVERIFY(buf.isEmpty());
    }
    void skipsNoiseBeforePacket() {
        QByteArray buf = hex("aa550200ff00") + buildPacket(helloCommand());
        auto p = takePacket(buf);
        QVERIFY(p.has_value());
        QCOMPARE(*p, helloCommand());
    }
    void incompletePacketIsLeftInBuffer() {
        const QByteArray whole = buildPacket(helloCommand());
        QByteArray buf = whole.left(9);
        QVERIFY(!takePacket(buf).has_value());
        buf += whole.mid(9);
        QVERIFY(takePacket(buf).has_value());
    }

    // ---- channels
    void decodesSimplexChannel() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        const Channel c = decodeChannel(img, 0);
        QVERIFY(!c.empty);
        QCOMPARE(c.name, QString("CH ONE"));
        QCOMPARE(c.freq, 44600625u);
        QCOMPARE(c.shift, 0);
        QCOMPARE(modulationNames().at(c.modulation), QString("FM"));
        QCOMPARE(c.narrow, false);
        QCOMPARE(powerNames().at(c.power), QString("USER"));
        QCOMPARE(stepsKHz().at(c.step), 12.5);
        QCOMPARE(c.scanLists, 1);
    }
    void decodesShiftAndOffset() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        const Channel uhf = decodeChannel(img, slotByName(img, "RPT UHF"));
        QCOMPARE(uhf.shift, 1);
        QCOMPARE(uhf.offset, 160000u);
        const Channel vhf = decodeChannel(img, slotByName(img, "RPT VHF"));
        QCOMPARE(vhf.shift, 2);
        QCOMPARE(vhf.offset, 60000u);
    }
    void decodesModeWidthPowerAndTone() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        const Channel air = decodeChannel(img, slotByName(img, "AIR"));
        QCOMPARE(modulationNames().at(air.modulation), QString("AM"));
        QCOMPARE(air.narrow, true);
        QCOMPARE(powerNames().at(air.power), QString("MID"));
        const Channel tone = decodeChannel(img, slotByName(img, "TONE"));
        QCOMPARE(toneText(tone.txToneType, tone.txToneCode), QString("114.8"));
        QCOMPARE(toneText(tone.rxToneType, tone.rxToneCode), QString(""));
        const Channel dcs = decodeChannel(img, slotByName(img, "DCS"));
        QCOMPARE(toneText(dcs.rxToneType, dcs.rxToneCode), QString("D023N"));
        QCOMPARE(toneText(dcs.txToneType, dcs.txToneCode), QString("D754I"));
        QCOMPARE(modulationNames().at(decodeChannel(img, slotByName(img, "SIDEBAND")).modulation), QString("USB"));
    }
    void decodesErasedSlotAsEmpty() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        QVERIFY(decodeChannel(img, 16).empty);
    }
    void reencodingEveryChannelChangesNothingButBandMarkers() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        QByteArray out = img;
        int used = 0;
        for (int i = 0; i < ChannelCount; ++i) {
            const Channel c = decodeChannel(img, i);
            if (!c.empty) { encodeChannel(out, i, c); ++used; }
        }
        QCOMPARE(used, 10);
        QCOMPARE(out.left(0xd60), img.left(0xd60));            // channel records
        QCOMPARE(out.mid(0xf50), img.mid(0xf50));              // names and the rest
        for (int i = 0; i < ChannelCount; ++i)                 // only the 3 band bits may differ
            QCOMPARE(quint8(out[0xd60 + i]) & 0xf8, quint8(img[0xd60 + i]) & 0xf8);
    }
    void encodeWritesAllFields() {
        QByteArray img = fixture("f4hwn_v43.bin");
        Channel c;
        c.empty = false; c.freq = 14550000; c.offset = 60000; c.shift = 2;
        c.rxToneType = 1; c.rxToneCode = 8; c.txToneType = 2; c.txToneCode = 3;
        c.modulation = 2; c.narrow = true; c.power = 7; c.step = 5; c.scrambler = 2;
        c.txLock = true; c.busyLock = true; c.reverse = true; c.pttId = 3; c.dtmfDecode = true;
        c.scanLists = 5; c.compander = 3; c.name = "TEST";
        encodeChannel(img, 99, c);
        QCOMPARE(decodeChannel(img, 99), c);
        QCOMPARE(img.mid(99 * 16, 16), hex("f003de0060ea0000080321227f070502"));
        QCOMPARE(quint8(img[0xd60 + 99]), quint8(0xba));       // lists 1+3, compander 3, band 2
        QCOMPARE(img.mid(0xf50 + 99 * 16, 16), QByteArray("TEST      ") + QByteArray(6, '\0'));
    }
    void encodeEmptyErasesSlot() {
        QByteArray img = fixture("f4hwn_v43.bin");
        encodeChannel(img, 0, Channel{});
        QCOMPARE(img.mid(0, 16), QByteArray(16, '\xff'));
        QCOMPARE(img.mid(0xf50, 16), QByteArray(16, '\xff'));
        QCOMPARE(quint8(img[0xd60]), quint8(0xe7));
        QVERIFY(decodeChannel(img, 0).empty);
    }
    void encodeKeepsUnknownBits() {
        QByteArray img = fixture("f4hwn_v43.bin");
        img[12] = char(quint8(img[12]) | 0x80);    // unused bit in the power byte
        img[13] = char(quint8(img[13]) | 0xf0);    // unused nibble in the DTMF byte
        Channel c = decodeChannel(img, 0);
        c.power = 6;
        encodeChannel(img, 0, c);
        QCOMPARE(decodeChannel(img, 0).power, 6);
        QCOMPARE(quint8(img[12]) & 0x80, 0x80);
        QCOMPARE(quint8(img[13]) & 0xf0, 0xf0);
    }
    void longNameIsCutToTenCharacters() {
        QByteArray img = fixture("f4hwn_v43.bin");
        Channel c = decodeChannel(img, 0);
        c.name = "ABCDEFGHIJKLMNOP";
        encodeChannel(img, 0, c);
        QCOMPARE(decodeChannel(img, 0).name, QString("ABCDEFGHIJ"));
    }
    void bandFollowsFrequency() {
        QCOMPARE(bandForFrequency(2696500), 0);     // 26.965 MHz
        QCOMPARE(bandForFrequency(11852500), 1);
        QCOMPARE(bandForFrequency(14550000), 2);
        QCOMPARE(bandForFrequency(25535000), 3);
        QCOMPARE(bandForFrequency(36000000), 4);
        QCOMPARE(bandForFrequency(44600625), 5);
        QCOMPARE(bandForFrequency(86800000), 6);
        QCOMPARE(bandForFrequency(66), -1);
        QCOMPARE(bandForFrequency(140000000), -1);
    }
    void healthyChannelHasNoProblems() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        QVERIFY(channelProblems(img, 0).isEmpty());
        QVERIFY(channelProblems(img, 16).isEmpty());          // empty slot
    }
    void damagedFrequencyIsReported() {
        const QByteArray img = fixture("damaged.bin");
        QVERIFY(!channelProblems(img, 3).isEmpty());          // holds 0.0257 MHz
        QVERIFY(!channelProblems(img, 24).isEmpty());         // holds 0.00066 MHz
        QVERIFY(channelProblems(img, 4).isEmpty());           // intact neighbour
    }
    void wrongBandMarkerIsReportedAndFixedByReencoding() {
        QByteArray img = fixture("f4hwn_v43.bin");
        const int cb = slotByName(img, "OLD BAND");            // carries another firmware's band marker
        QVERIFY(!channelProblems(img, cb).isEmpty());
        encodeChannel(img, cb, decodeChannel(img, cb));
        QVERIFY(channelProblems(img, cb).isEmpty());
    }
    void unknownModeIsReported() {
        QByteArray img = fixture("f4hwn_v43.bin");
        img[11] = char(0x30);
        QVERIFY(!channelProblems(img, 0).isEmpty());
    }
    void toneTextRoundTrips() {
        const QStringList all = allToneTexts();
        QCOMPARE(all.size(), 1 + 50 + 104 * 2);
        for (const QString &t : all) {
            int type = -1, code = -1;
            QVERIFY2(parseTone(t, type, code), qPrintable(t));
            QCOMPARE(toneText(type, code), t);
        }
        int type, code;
        QVERIFY(!parseTone("99.9", type, code));
    }

    // ---- settings
    void readsSettingsFromImage() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        QCOMPARE(settingValue(img, "squelch").toInt(), 1);
        QCOMPARE(findSetting("mic_gain")->options.at(settingValue(img, "mic_gain").toInt()), QString("+12.0dB"));
        QCOMPARE(settingValue(img, "backlight_min").toInt(), 0);
        QCOMPARE(settingValue(img, "backlight_max").toInt(), 10);
        QCOMPARE(settingValue(img, "logo1").toString(), QString("K5 EDITOR"));
        QCOMPARE(findSetting("int_flock")->options.at(settingValue(img, "int_flock").toInt()), QString("UNLOCK ALL"));
        QCOMPARE(findSetting("set_pwr")->options.at(settingValue(img, "set_pwr").toInt()), QString("< 20mW"));
        QCOMPARE(findSetting("tot")->options.at(settingValue(img, "tot").toInt()), QString("1 min"));
        QCOMPARE(findSetting("battery_save")->options.at(settingValue(img, "battery_save").toInt()), QString("1:4"));
        QCOMPARE(settingValue(img, "s0_level").toInt(), -130);
        QCOMPARE(settingValue(img, "button_beep").toBool(), false);
        QCOMPARE(settingValue(img, "mic_bar").toBool(), true);
        QCOMPARE(settingValue(img, "sl1PriorCh1").toInt(), 0);     // "None"
    }
    void everySettingReadsWithoutCrashingAndHasGroup() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        QVERIFY(settings().size() > 40);
        for (const Setting &s : settings()) {
            QVERIFY2(!s.group.isEmpty() && !s.label.isEmpty(), qPrintable(s.id));
            QVERIFY2(settingValue(img, s.id).isValid(), qPrintable(s.id));
        }
    }
    void writingASettingBackKeepsItsValue() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        int checked = 0;
        for (const Setting &s : settings()) {
            const QVariant v = settingValue(img, s.id);
            if (s.kind == Setting::List && v.toInt() < 0) continue;                         // stored value not in list
            if (s.kind == Setting::Int && (v.toInt() < s.min || v.toInt() > s.max)) continue; // stored value out of range
            QByteArray out = img;
            QVERIFY2(setSettingValue(out, s.id, v), qPrintable(s.id));
            QVERIFY2(settingValue(out, s.id) == v, qPrintable(s.id));
            ++checked;
        }
        QVERIFY(checked > 40);
    }
    void writesPlainByteSetting() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        QByteArray out = img;
        QVERIFY(setSettingValue(out, "squelch", 5));
        QCOMPARE(quint8(out[0xe71]), quint8(5));
        out[0xe71] = img[0xe71];
        QCOMPARE(out, img);
    }
    void writesBitFieldWithoutTouchingNeighbours() {
        QByteArray out = fixture("f4hwn_v43.bin");
        QCOMPARE(quint8(out[0xe78]), quint8(0x0a));
        QVERIFY(setSettingValue(out, "backlight_min", 3));
        QCOMPARE(quint8(out[0xe78]), quint8(0x3a));
        QVERIFY(setSettingValue(out, "set_pwr", 4));
        QCOMPARE(quint8(out[0x1ff7]), quint8(0x40));
        QVERIFY(setSettingValue(out, "mic_bar", false));
        QCOMPARE(quint8(out[0xf47]), quint8(0x23));
    }
    void writesTextWithFirmwarePadding() {
        QByteArray out = fixture("f4hwn_v43.bin");
        QVERIFY(setSettingValue(out, "logo1", "HELLO"));
        QCOMPARE(out.mid(0xeb0, 16), QByteArray("HELLO") + QByteArray(8, '\0') + hex("ffffff"));
        QVERIFY(!setSettingValue(out, "logo1", "THIS IS TOO LONG"));
        QVERIFY(setSettingValue(out, "dtmf_dtmf_up_code", "12AB"));
        QCOMPARE(out.mid(0xef8, 16), QByteArray("12AB") + QByteArray(12, '\0'));
        QVERIFY(!setSettingValue(out, "dtmf_dtmf_up_code", "12xz"));
    }
    void rejectsOutOfRangeValues() {
        QByteArray out = fixture("f4hwn_v43.bin");
        QVERIFY(setSettingValue(out, "squelch", 9));
        const QByteArray before = out;
        QVERIFY(!setSettingValue(out, "squelch", 10));
        QVERIFY(!setSettingValue(out, "mic_gain", 99));
        QVERIFY(!setSettingValue(out, "no_such_setting", 1));
        QCOMPARE(out, before);
    }
    void voxCombinesSwitchAndLevel() {
        QByteArray out = fixture("f4hwn_v43.bin");
        QVERIFY(setSettingValue(out, "vox", 3));
        QCOMPARE(quint8(out[0xe75]), quint8(1));
        QCOMPARE(quint8(out[0xe76]), quint8(2));
        QCOMPARE(settingValue(out, "vox").toInt(), 3);
        QVERIFY(setSettingValue(out, "vox", 0));
        QCOMPARE(quint8(out[0xe75]), quint8(0));
        QCOMPARE(settingValue(out, "vox").toInt(), 0);
    }
    void rxModeDependsOnMainVfo() {
        QByteArray out = fixture("f4hwn_v43.bin");
        QCOMPARE(quint8(out[0xeab]), quint8(1));                  // main VFO is B in the fixture
        QVERIFY(setSettingValue(out, "rx_mode", 3));              // MAIN TX DUAL RX
        QCOMPARE(quint8(out[0xe7a]), quint8(2));
        QCOMPARE(quint8(out[0xe7c]), quint8(2));
        QCOMPARE(settingValue(out, "rx_mode").toInt(), 3);
        QVERIFY(setSettingValue(out, "rx_mode", 1));              // DUAL RX RESPOND
        QCOMPARE(quint8(out[0xe7a]), quint8(0));
        QCOMPARE(quint8(out[0xe7c]), quint8(2));
    }
    void priorityChannelUsesFFForNone() {
        QByteArray out = fixture("f4hwn_v43.bin");
        QVERIFY(setSettingValue(out, "sl1PriorCh1", 5));          // "Channel M5"
        QCOMPARE(quint8(out[0xf1a]), quint8(4));
        QVERIFY(setSettingValue(out, "sl1PriorCh1", 0));
        QCOMPARE(quint8(out[0xf1a]), quint8(0xff));
    }
    void scaledAndNegatedIntegers() {
        QByteArray out = fixture("f4hwn_v43.bin");
        QVERIFY(setSettingValue(out, "dtmf_preload_time", 500));
        QCOMPARE(quint8(out[0xed5]), quint8(50));
        QVERIFY(setSettingValue(out, "s9_level", -80));
        QCOMPARE(quint8(out[0xea2]), quint8(80));
    }
    void byteTheRadioNeverWroteReadsAsNotSet() {
        const QByteArray img = fixture("f4hwn_v43.bin");
        QCOMPARE(quint8(img[0x1ff5]), quint8(0xff));               // never saved by the radio
        QCOMPARE(settingValue(img, "set_inv").toInt(), -1);
        QCOMPARE(settingValue(img, "set_met").toInt(), -1);
        QVERIFY(settingValue(img, "set_contrast").toInt() < 0);
    }
    void firstWriteIntoUnsetByteStartsFromFirmwareDefaults() {
        QByteArray out = fixture("f4hwn_v43.bin");
        QVERIFY(setSettingValue(out, "set_met", 1));
        QCOMPARE(quint8(out[0x1ff5]), quint8(0x4b));               // S-meter classic, contrast 11, rest off
        QCOMPARE(settingValue(out, "set_inv").toInt(), 0);
        QCOMPARE(settingValue(out, "set_contrast").toInt(), 11);
    }
    void unlockAllCannotBeEnabledFromHere() {
        QByteArray out = fixture("f4hwn_v43.bin");
        QVERIFY(setSettingValue(out, "int_flock", 10));            // already unlocked: fine
        QVERIFY(setSettingValue(out, "int_flock", 0));
        QVERIFY(!setSettingValue(out, "int_flock", 10));           // must be done in the radio menu
        QCOMPARE(quint8(out[0xf40]), quint8(0));
    }

    // ---- write planning
    void planCoversChangedBytesInEightByteBlocks() {
        const QByteArray base = fixture("f4hwn_v43.bin");
        QByteArray work = base;
        work[0x0013] = char(work[0x0013] ^ 1);
        const auto plan = planWrites(base, work);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan[0].offset, 0x0010);
        QCOMPARE(plan[0].data, work.mid(0x0010, 8));
    }
    void planMergesAdjacentBlocksUpToPacketLimit() {
        const QByteArray base = fixture("f4hwn_v43.bin");
        QByteArray work = base;
        for (int i = 0x100; i < 0x100 + 0x90; ++i) work[i] = char(work[i] ^ 1);
        const auto plan = planWrites(base, work);
        QCOMPARE(plan.size(), 2);
        QCOMPARE(plan[0].offset, 0x100);
        QCOMPARE(plan[0].data.size(), 0x80);
        QCOMPARE(plan[1].offset, 0x180);
        QCOMPARE(plan[1].data.size(), 0x10);
    }
    void planNeverTouchesCalibration() {
        const QByteArray base = fixture("f4hwn_v43.bin");
        QByteArray work = base;
        for (int off : {0x1d00, 0x1e00, 0x1f40, 0x1f8e, 0x1ff0, 0x1ffa}) work[off] = char(work[off] ^ 1);
        QVERIFY(planWrites(base, work).isEmpty());
        work[0x1cff] = char(work[0x1cff] ^ 1);                 // last writable byte
        QCOMPARE(planWrites(base, work).size(), 1);
    }
    void planWritesExtraSettingsAsOneFixedBlock() {
        const QByteArray base = fixture("f4hwn_v43.bin");
        QByteArray work = base;
        work[0x1ff7] = 0x40;
        const auto plan = planWrites(base, work);
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan[0].offset, 0x1ff2);
        QCOMPARE(plan[0].data, work.mid(0x1ff2, 8));
    }

    // ---- radio
    void readsFirmwareAndWholeMemory() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        Radio radio(fake);
        QCOMPARE(radio.hello(), QString("F4HWN v4.3"));
        QCOMPARE(radio.readAll(), fake.eeprom);
        QVERIFY(fake.writes.isEmpty());
    }
    void helloSurvivesBootNoiseAndOneLostCommand() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        fake.ignoreCommands = 1;
        fake.noiseBeforeNextReply = hex("aa550200");
        Radio radio(fake, 30);
        QCOMPARE(radio.hello(), QString("F4HWN v4.3"));
    }
    void silentRadioRaisesError() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        fake.ignoreCommands = 100;
        Radio radio(fake, 30);
        QVERIFY_EXCEPTION_THROWN(radio.hello(), RadioError);
    }

    // ---- writing to the radio
    void writeSendsOnlyChangedBlocksAndResets() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        const QByteArray base = fake.eeprom;
        QByteArray work = base;
        Channel c = decodeChannel(work, 0); c.name = "RENAMED"; encodeChannel(work, 0, c);
        QTemporaryDir dir; Radio radio(fake);
        const WriteResult r = writeToRadio(radio, base, work, {dir.path(), false, {}});
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(fake.eeprom, work);
        QCOMPARE(r.radioImage, work);
        QCOMPARE(fake.writes.size(), 1);
        QCOMPARE(fake.writes[0].first, 0xf50);
        QVERIFY(fake.wasReset);
    }
    void writeSavesBackupOfRadioBeforeChangingIt() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        const QByteArray base = fake.eeprom;
        QByteArray work = base; setSettingValue(work, "squelch", 4);
        QTemporaryDir dir; Radio radio(fake);
        const WriteResult r = writeToRadio(radio, base, work, {dir.path(), false, {}});
        QCOMPARE(r.status, WriteResult::Ok);
        QFile f(r.backupPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), base);
        QVERIFY(r.backupPath.startsWith(dir.path()));
    }
    void writeRefusesUnknownFirmware() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin"); fake.firmware = "OTHER 1.0";
        const QByteArray base = fake.eeprom;
        QByteArray work = base; setSettingValue(work, "squelch", 4);
        QTemporaryDir dir; Radio radio(fake);
        const WriteResult r = writeToRadio(radio, base, work, {dir.path(), false, {}});
        QCOMPARE(r.status, WriteResult::UnsupportedFirmware);
        QVERIFY(fake.writes.isEmpty());
        QCOMPARE(fake.eeprom, base);
    }
    void writeWithNoChangesDoesNothing() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        QTemporaryDir dir; Radio radio(fake);
        const WriteResult r = writeToRadio(radio, fake.eeprom, fake.eeprom, {dir.path(), false, {}});
        QCOMPARE(r.status, WriteResult::NothingToWrite);
        QVERIFY(fake.writes.isEmpty());
        QVERIFY(!fake.wasReset);
    }
    void writeStopsWhenRadioChangedTheSameBlock() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        const QByteArray base = fake.eeprom;
        QByteArray work = base; setSettingValue(work, "squelch", 4);       // 0xe71
        fake.eeprom[0xe72] = 0x20;                                          // radio changed its timeout
        QTemporaryDir dir; Radio radio(fake);
        const WriteResult r = writeToRadio(radio, base, work, {dir.path(), false, {}});
        QCOMPARE(r.status, WriteResult::Conflict);
        QCOMPARE(r.conflictOffsets, QList<int>{0xe70});
        QVERIFY(fake.writes.isEmpty());
    }
    void writeKeepsRadioChangesInOtherBlocks() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        const QByteArray base = fake.eeprom;
        QByteArray work = base; setSettingValue(work, "squelch", 4);
        fake.eeprom[0xc90] = 0x55;                                          // radio saved VFO state
        QTemporaryDir dir; Radio radio(fake);
        const WriteResult r = writeToRadio(radio, base, work, {dir.path(), false, {}});
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(quint8(fake.eeprom[0xc90]), quint8(0x55));
        QCOMPARE(quint8(fake.eeprom[0xe71]), quint8(4));
        QCOMPARE(r.radioImage, fake.eeprom);
    }
    void writeCanOverwriteConflictWhenAsked() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        const QByteArray base = fake.eeprom;
        QByteArray work = base; setSettingValue(work, "squelch", 4);
        fake.eeprom[0xe72] = 0x20;
        QTemporaryDir dir; Radio radio(fake);
        const WriteResult r = writeToRadio(radio, base, work, {dir.path(), true, {}});
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(fake.eeprom.mid(0xe70, 8), work.mid(0xe70, 8));
    }
    void writeNeverSendsCalibrationEvenIfImageDiffers() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        const QByteArray base = fake.eeprom;
        QByteArray work = base; work[0x1e00] = 0x11; setSettingValue(work, "squelch", 4);
        QTemporaryDir dir; Radio radio(fake);
        const WriteResult r = writeToRadio(radio, base, work, {dir.path(), false, {}});
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(fake.eeprom[0x1e00], base[0x1e00]);
        for (const auto &w : fake.writes) QVERIFY(w.first + w.second <= 0x1d00 || w.first == 0x1ff2);
    }
    void writeReportsVerifyFailure() {
        FakeRadio fake; fake.eeprom = fixture("f4hwn_v43.bin");
        const QByteArray base = fake.eeprom;
        QByteArray work = base; setSettingValue(work, "squelch", 4);
        // a radio that acknowledges writes but does not keep them
        struct Forgetful : k5::Transport {
            FakeRadio &radio; QByteArray keep;
            explicit Forgetful(FakeRadio &r) : radio(r), keep(r.eeprom) {}
            void write(const QByteArray &b) override { radio.write(b); radio.eeprom = keep; }
            QByteArray read(int t) override { return radio.read(t); }
            void discardInput() override { radio.discardInput(); }
        } forgetful(fake);
        QTemporaryDir dir; Radio radio(forgetful);
        const WriteResult r = writeToRadio(radio, base, work, {dir.path(), false, {}});
        QCOMPARE(r.status, WriteResult::VerifyFailed);
        QCOMPARE(fake.writes.size(), 1);
        QVERIFY(!fake.wasReset);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
