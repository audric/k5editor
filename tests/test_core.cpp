#include "f4hwn.h"
#include "fakeradio.h"
#include "presets.h"
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
    void initTestCase() { setPresetDirs({QStringLiteral(K5_LISTS_SOURCE_DIR)}); }

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

    void blankImageIsEmptyAndTakesChannels() {
        QByteArray img = blankImage();
        QCOMPARE(img.size(), MemSize);
        for (int i = 0; i < ChannelCount; ++i) {
            QVERIFY(decodeChannel(img, i).empty);
            QVERIFY(channelProblems(img, i).isEmpty());
        }
        QCOMPARE(freeSlotsFrom(img, 0), ChannelCount);
        // default settings, and nothing else: no password, no calibration
        QCOMPARE(settingValue(img, "squelch").toInt(), 1);
        QCOMPARE(settingValue(img, "logo1").toString(), QString("QUANSHENG"));
        QCOMPARE(settingValue(img, "logo2").toString(), QString("UV-K5"));
        QCOMPARE(findSetting("int_flock")->options.at(settingValue(img, "int_flock").toInt()), QString("DEFAULT+ (137-174, 400-470)"));
        QCOMPARE(findSetting("tot")->options.at(settingValue(img, "tot").toInt()), QString("1 min"));
        for (const char *id : {"sl1PriorCh1", "sl1PriorCh2", "sl2PriorCh1", "sl2PriorCh2", "sl3PriorCh1", "sl3PriorCh2"})
            QCOMPARE(settingValue(img, id).toInt(), 0);                  // no channels to point at
        int set = 0;
        for (const Setting &s : settings()) {
            const QVariant v = settingValue(img, s.id);
            QVERIFY2(v.isValid(), qPrintable(s.id));
            QByteArray again = img;
            if (s.kind != Setting::List || v.toInt() >= 0) set += setSettingValue(again, s.id, v) && again == img;     // stored as the program writes it
        }
        QVERIFY(set >= 70);
        QCOMPARE(img.mid(0x0e98, 8), QByteArray(8, '\xff'));
        QCOMPARE(img.mid(0x0c80, 0x0d60 - 0x0c80), QByteArray(0x0d60 - 0x0c80, '\xff'));     // VFOs
        QCOMPARE(img.mid(0x1c00, 0x1ff0 - 0x1c00), QByteArray(0x1ff0 - 0x1c00, '\xff'));     // DTMF contacts, calibration
        QVERIFY(planWrites(img, img).isEmpty());
        const ImportResult r = importPreset(img, *findPresetGroup("pmr446"), {});
        QVERIFY(r.ok);
        QCOMPARE(r.placed.first(), 0);
        QCOMPARE(decodeChannel(img, 15).name, QString("PMR 16"));
        QVERIFY(channelProblems(img, 15).isEmpty());
    }

    // ---- preset channel lists
    void presetGroupsAreWellFormed() {
        QStringList ids;
        for (const PresetGroup &g : presetGroups()) {
            QVERIFY2(!ids.contains(g.id), qPrintable(g.id));
            ids << g.id;
            QVERIFY2(!g.channels.isEmpty() && g.channels.size() <= ChannelCount, qPrintable(g.id));
            QVERIFY2(!g.name.isEmpty() && !g.note.isEmpty(), qPrintable(g.id));
            QByteArray img(MemSize, '\xff');
            QStringList names;
            QList<QPair<quint32, int>> seen;
            for (int i = 0; i < g.channels.size(); ++i) {
                const Channel &c = g.channels.at(i);
                encodeChannel(img, i, c);
                QVERIFY2(decodeChannel(img, i) == c, qPrintable(c.name));   // name fits, values storable
                QVERIFY2(channelProblems(img, i).isEmpty(), qPrintable(c.name));
                QVERIFY2(bandForFrequency(txFrequency(c)) >= 0, qPrintable(c.name));
                QVERIFY2(!c.name.isEmpty() && !names.contains(c.name), qPrintable(c.name));
                names << c.name;
                // repeaters of a region and satellite Doppler steps share frequencies; elsewhere a repeat is a mistake
                QVERIFY2(g.id.startsWith("rpt_it_") || g.id == "satellites" || !seen.contains({c.freq, c.modulation}), qPrintable(c.name));
                seen.append({c.freq, c.modulation});
            }
        }
        for (const char *id : {"cb_fm", "cb_am", "marine", "pmr446", "lpd433", "freenet"}) QVERIFY2(findPresetGroup(id), id);
        QVERIFY(!findPresetGroup("nope"));
    }
    void presetFrequencies() {
        auto ch = [](const char *group, const QString &name) {
            for (const Channel &c : findPresetGroup(group)->channels)
                if (c.name == name) return c;
            qFatal("no channel %s", qPrintable(name));
            return Channel{};
        };
        auto rx = [&](const char *g, const char *n) { return ch(g, n).freq; };
        auto tx = [&](const char *g, const char *n) { return txFrequency(ch(g, n)); };
        QCOMPARE(findPresetGroup("cb_fm")->channels.size(), 40);
        QCOMPARE(rx("cb_fm", "CB 01 FM"), 2696500u);
        QCOMPARE(rx("cb_fm", "CB 09 FM"), 2706500u);
        QCOMPARE(rx("cb_fm", "CB 19 FM"), 2718500u);
        QCOMPARE(rx("cb_fm", "CB 23 FM"), 2725500u);
        QCOMPARE(rx("cb_fm", "CB 24 FM"), 2723500u);
        QCOMPARE(rx("cb_fm", "CB 40 FM"), 2740500u);
        QCOMPARE(modulationNames().at(ch("cb_am", "CB 09 AM").modulation), QString("AM"));
        QCOMPARE(rx("cb_am", "CB 09 AM"), 2706500u);

        QCOMPARE(findPresetGroup("marine")->channels.size(), 56);          // 1-28 and 60-88 without 70
        QCOMPARE(rx("marine", "Marine 16"), 15680000u);
        QCOMPARE(tx("marine", "Marine 16"), 15680000u);
        QCOMPARE(ch("marine", "Marine 16").shift, 0);
        QCOMPARE(rx("marine", "Marine 01"), 16065000u);                    // ship side: listens to the coast station
        QCOMPARE(tx("marine", "Marine 01"), 15605000u);
        QCOMPARE(ch("marine", "Marine 01").shift, 2);
        QCOMPARE(ch("marine", "Marine 01").offset, 460000u);
        QCOMPARE(tx("marine", "Marine 28"), 15740000u);
        QCOMPARE(rx("marine", "Marine 28"), 16200000u);
        QCOMPARE(rx("marine", "Marine 88"), 15742500u);
        for (const Channel &c : findPresetGroup("marine")->channels) QVERIFY(c.name != "Marine 70");
        QCOMPARE(powerNames().at(ch("marine", "Marine 75").power), QString("LOW5"));

        QCOMPARE(findPresetGroup("pmr446")->channels.size(), 16);
        QCOMPARE(rx("pmr446", "PMR 01"), 44600625u);
        QCOMPARE(rx("pmr446", "PMR 16"), 44619375u);
        QCOMPARE(ch("pmr446", "PMR 01").narrow, true);
        QCOMPARE(powerNames().at(ch("pmr446", "PMR 01").power), QString("LOW4"));     // 0.5 W
        QCOMPARE(findPresetGroup("lpd433")->channels.size(), 69);
        QCOMPARE(rx("lpd433", "LPD 01"), 43307500u);
        QCOMPARE(rx("lpd433", "LPD 69"), 43477500u);
        QCOMPARE(powerNames().at(ch("lpd433", "LPD 01").power), QString("LOW1"));     // the lowest the radio has
        QCOMPARE(findPresetGroup("freenet")->channels.size(), 6);
        QCOMPARE(rx("freenet", "Freenet 1"), 14902500u);
        QCOMPARE(rx("freenet", "Freenet 6"), 14911250u);
        QCOMPARE(rx("cb43", "43MHz 01"), 4330000u);
        QCOMPARE(rx("frs_gmrs", "FRS 01"), 46256250u);
        QCOMPARE(rx("noaa", "NOAA WX1"), 16255000u);
        QCOMPARE(rx("ham", "2m FM"), 14550000u);
        QCOMPARE(modulationNames().at(ch("ham", "2m SSB").modulation), QString("USB"));
    }
    void shippedListFilesAreClean() {
        QVERIFY2(presetProblems().isEmpty(), qPrintable(presetProblems().join("\n")));
        QVERIFY(presetGroups().size() >= 30);
        QCOMPARE(presetGroups().first().id, QString("cb_fm"));          // the number prefix sets the order
        for (const PresetGroup &g : presetGroups()) {                    // every list survives being written and read back
            const PresetFile again = parsePresetCsv(presetCsv(g).toUtf8(), g.id);
            QVERIFY2(again.problems.isEmpty(), qPrintable(g.id));
            QCOMPARE(again.group.name, g.name);
            QCOMPARE(again.group.note, g.note);
            QVERIFY2(again.group.channels == g.channels, qPrintable(g.id));
        }
        const PresetGroup *sat = findPresetGroup("satellites");
        QVERIFY(sat);
        for (const Channel &c : sat->channels) {                         // amateur satellite segments only
            for (quint32 f : {c.freq, txFrequency(c)})
                QVERIFY2((f >= 14580000 && f <= 14600000) || (f >= 43500000 && f <= 43800000), qPrintable(c.name));
        }
    }
    void listFileFormat() {
        QCOMPARE(presetIdFromFileName("04-pmr446.csv"), QString("pmr446"));
        QCOMPARE(presetIdFromFileName("/x/My List.CSV"), QString("my list"));
        const PresetFile f = parsePresetCsv(
            "# name: Mine\n# note: my own\n# anything else is a comment\n\n"
            "Name;RX;TX;tx_tone;width;extra\n"
            "Home;145,500;;;narrow;x\n"
            "\"Rpt; nord\";145.6;145.0;88.5;;\n"
            "DCS one;433.5;;D023N;;\n"
            "Up;430.1;435.1;;;\n", "mine");
        QVERIFY2(f.problems.isEmpty(), qPrintable(f.problems.join("\n")));
        QCOMPARE(f.group.id, QString("mine"));
        QCOMPARE(f.group.name, QString("Mine"));
        QCOMPARE(f.group.note, QString("my own"));
        QCOMPARE(f.group.channels.size(), 4);
        const Channel a = f.group.channels.at(0), b = f.group.channels.at(1), up = f.group.channels.at(3);
        QCOMPARE(a.name, QString("Home"));
        QCOMPARE(a.freq, 14550000u);
        QCOMPARE(a.shift, 0);
        QCOMPARE(a.offset, 0u);
        QCOMPARE(a.narrow, true);
        QCOMPARE(modulationNames().at(a.modulation), QString("FM"));     // defaults
        QCOMPARE(powerNames().at(a.power), QString("LOW5"));
        QCOMPARE(stepsKHz().at(a.step), 12.5);
        QCOMPARE(a.scanLists, 0);
        QCOMPARE(b.name, QString("Rpt; nord"));
        QCOMPARE(b.shift, 2);
        QCOMPARE(b.offset, 60000u);
        QCOMPARE(txFrequency(b), 14500000u);
        QCOMPARE(toneText(b.txToneType, b.txToneCode), QString("88.5"));
        QCOMPARE(b.rxToneType, 0);
        QCOMPARE(b.narrow, false);
        QCOMPARE(toneText(f.group.channels.at(2).txToneType, f.group.channels.at(2).txToneCode), QString("D023N"));
        QCOMPARE(up.shift, 1);
        QCOMPARE(up.offset, 500000u);
        QCOMPARE(txFrequency(up), 43510000u);
    }
    void listsFromTheOtherProgramCanBeUsed() {
        // rt860cps writes SSB, Low and High
        const PresetFile f = parsePresetCsv("name,rx,mode,power\nA,144.3,SSB,High\nB,446.00625,fm,Low\nC,145.5,AM,low3\nD,145.5,,Mid\n", "x");
        QVERIFY2(f.problems.isEmpty(), qPrintable(f.problems.join("\n")));
        QCOMPARE(modulationNames().at(f.group.channels.at(0).modulation), QString("USB"));
        QCOMPARE(powerNames().at(f.group.channels.at(0).power), QString("HIGH"));
        QCOMPARE(powerNames().at(f.group.channels.at(1).power), QString("LOW5"));
        QCOMPARE(powerNames().at(f.group.channels.at(2).power), QString("LOW3"));
        QCOMPARE(powerNames().at(f.group.channels.at(3).power), QString("MID"));
    }
    void listFileProblemsAreReported() {
        const PresetFile f = parsePresetCsv(
            "name,rx,tx,tx_tone,mode\n"
            "Good,446.00625,,,\n"
            "Bad freq,abc,,,\n"
            "Too low,5.0,,,\n"
            "Bad tx,145.5,zzz,,\n"
            "Bad tone,145.5,,xyz,\n"
            "Bad mode,145.5,,,CW\n"
            "A name that is too long,145.5,,,\n"
            "Caff\xc3\xa8,145.5,,,\n", "x");
        QCOMPARE(f.group.channels.size(), 2);
        QCOMPARE(f.group.name, QString("x"));                            // no "# name:" line: the file name
        QCOMPARE(f.group.channels.at(1).name, QString("A name tha"));
        QCOMPARE(f.problems.size(), 7);
        QVERIFY(f.problems.at(0).startsWith("line 3:"));
        QVERIFY(f.problems.at(4).contains("FM, AM, USB"));
        QVERIFY(f.problems.at(5).contains("stored as"));
        QVERIFY(f.problems.at(6).startsWith("line 9:"));                 // only plain ASCII fits in a name
        QCOMPARE(parsePresetCsv("145.5,Home\n", "x").group.channels.size(), 0);     // no header row
        QVERIFY(!parsePresetCsv("145.5,Home\n", "x").problems.isEmpty());
        QVERIFY(!parsePresetCsv("", "x").problems.isEmpty());
    }
    void userListsAddAndReplace() {
        QTemporaryDir dir;
        auto write = [&](const char *name, const char *text) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        };
        write("pmr446.csv", "# name: My PMR\nname,rx\nOnly one,446.00625\n");
        write("99-club.csv", "# name: Club\nname,rx,tx\nClub rpt,145.6,145.0\nBroken,nope,\n");
        write("notes.txt", "not a list");
        const int before = int(presetGroups().size());
        int pmrAt = -1;
        for (int i = 0; i < before; ++i)
            if (presetGroups().at(i).id == "pmr446") pmrAt = i;
        QVERIFY(pmrAt >= 0);
        setPresetDirs({QStringLiteral(K5_LISTS_SOURCE_DIR), dir.path(), dir.filePath("missing")});
        QCOMPARE(presetGroups().size(), before + 1);
        QCOMPARE(findPresetGroup("pmr446")->name, QString("My PMR"));   // replaced, same place in the order
        QCOMPARE(findPresetGroup("pmr446")->channels.size(), 1);
        QCOMPARE(presetGroups().at(pmrAt).id, QString("pmr446"));
        QCOMPARE(presetGroups().last().id, QString("club"));
        QCOMPARE(presetGroups().last().channels.size(), 1);
        QCOMPARE(presetProblems().size(), 1);
        QVERIFY(presetProblems().first().startsWith("99-club.csv: line 4:"));
        setPresetDirs({QStringLiteral(K5_LISTS_SOURCE_DIR)});
        QCOMPARE(presetGroups().size(), before);
        QCOMPARE(findPresetGroup("pmr446")->channels.size(), 16);
    }
    void italianAirportsAndRepeaters() {
        int total = 0;
        for (const char *id : {"air_it_north", "air_it_centre", "air_it_south"}) {
            const PresetGroup *g = findPresetGroup(id);
            QVERIFY2(g, id);
            QVERIFY(presetUsesAirband(*g));
            total += int(g->channels.size());
            for (const Channel &c : g->channels) {
                QCOMPARE(modulationNames().at(c.modulation), QString("AM"));
                QVERIFY2(c.freq >= 11800000 && c.freq <= 13697500, qPrintable(c.name));
                QVERIFY2(c.name.startsWith("LI"), qPrintable(c.name));
            }
        }
        QVERIFY(total > 100);
        QVERIFY(!presetUsesAirband(*findPresetGroup("pmr446")));
        QVERIFY(presetUsesAirband(*findPresetGroup("distress")));

        int lists = 0;
        total = 0;
        for (const PresetGroup &g : presetGroups()) {
            if (!g.id.startsWith("rpt_it_")) continue;
            ++lists;
            total += int(g.channels.size());
            for (const Channel &c : g.channels) {
                const quint32 tx = txFrequency(c);
                const bool vhf = c.freq >= 14400000 && c.freq <= 14600000, uhf = c.freq >= 43000000 && c.freq <= 44000000;
                QVERIFY2(vhf || uhf, qPrintable(c.name));
                QVERIFY2(c.shift != 0, qPrintable(c.name));
                // the input stays inside the same amateur band
                QVERIFY2(vhf ? tx >= 14400000 && tx <= 14600000 : tx >= 43000000 && tx <= 44000000, qPrintable(c.name));
                QCOMPARE(modulationNames().at(c.modulation), QString("FM"));
                QCOMPARE(c.rxToneType, 0);
            }
        }
        QVERIFY(lists >= 15);
        QVERIFY(total > 300);
        QVERIFY(findPresetGroup("rpt_it_lazio"));
    }
    void importFillsEmptySlotsInOrder() {
        QByteArray img = fixture("f4hwn_v43.bin");                       // slots 1 to 10 in use
        const QByteArray before = img;
        const PresetGroup &g = *findPresetGroup("freenet");
        const ImportResult r = importPreset(img, g, {});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.placed, (QList<int>{10, 11, 12, 13, 14, 15}));
        for (int i = 0; i < 10; ++i) QCOMPARE(decodeChannel(img, i), decodeChannel(before, i));     // existing channels untouched
        QCOMPARE(decodeChannel(img, 10).name, QString("Freenet 1"));
        QCOMPARE(decodeChannel(img, 15).name, QString("Freenet 6"));
        QCOMPARE(decodeChannel(img, 10).scanLists, 0);
        QVERIFY(channelProblems(img, 10).isEmpty());
        QVERIFY(decodeChannel(img, 16).empty);
        // only channel memory, attributes and names change
        QCOMPARE(img.mid(0x0e70, 0x0d60 - 0x0e70 + 0x1f0), before.mid(0x0e70, 0x0d60 - 0x0e70 + 0x1f0));
        QCOMPARE(img.mid(0x1d00), before.mid(0x1d00));
        // a start slot in the middle skips used slots, and the channels can go into a scan list
        ImportOptions o;
        o.firstSlot = 13;
        o.scanList = 3;
        const ImportResult r2 = importPreset(img, g, o);
        QVERIFY(r2.ok);
        QCOMPARE(r2.placed, (QList<int>{16, 17, 18, 19, 20, 21}));
        QCOMPARE(decodeChannel(img, 16).scanLists, 4);
        QCOMPARE(decodeChannel(img, 10).scanLists, 0);
    }
    void importLocksTransmitOnTheAirBand() {
        const PresetGroup &g = *findPresetGroup("distress");            // two air band and two marine channels
        QByteArray img = fixture("f4hwn_v43.bin");
        ImportResult r = importPreset(img, g, {});
        QVERIFY(r.ok);
        int locked = 0;
        for (int s : r.placed) {
            const Channel c = decodeChannel(img, s);
            QCOMPARE(c.txLock, isAirband(c));
            locked += c.txLock;
        }
        QCOMPARE(locked, 2);
        img = fixture("f4hwn_v43.bin");
        ImportOptions o;
        o.lockAirband = false;
        r = importPreset(img, g, o);
        for (int s : r.placed) QVERIFY(!decodeChannel(img, s).txLock);
    }
    void importIsAllOrNothing() {
        QByteArray img = fixture("f4hwn_v43.bin");
        const QByteArray before = img;
        QCOMPARE(freeSlotsFrom(img, 0), 190);
        QCOMPARE(freeSlotsFrom(img, 180), 20);
        ImportOptions o;
        o.firstSlot = 180;
        const ImportResult r = importPreset(img, *findPresetGroup("cb_fm"), o);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("40"));
        QCOMPARE(img, before);
        o.firstSlot = 500;
        QVERIFY(!importPreset(img, *findPresetGroup("cb_fm"), o).ok);
        QByteArray small = img.left(100);
        QVERIFY(!importPreset(small, *findPresetGroup("freenet"), {}).ok);
        QCOMPARE(img, before);
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
