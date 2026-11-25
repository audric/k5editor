#include "presets.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <cmath>

namespace k5 {

namespace {

QStringList g_dirs, g_problems;
QList<PresetGroup> g_groups;

const QStringList kWidthNames{"Wide", "Narrow"};
constexpr int kDefaultPower = 5;        // LOW5, about 1 W
constexpr int kDefaultStep = 4;         // 12.5 kHz, as for a channel typed into the table

QString csvField(const QString &v) {
    if (!v.contains(',') && !v.contains('"') && !v.startsWith('#') && v == v.trimmed()) return v;
    QString q = v;
    return '"' + q.replace('"', "\"\"") + '"';
}

QStringList csvSplit(const QString &line, QChar sep) {
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line.at(i + 1) == '"') { field += '"'; ++i; }
            else if (c == '"') quoted = false;
            else field += c;
        } else if (c == '"') quoted = true;
        else if (c == sep) { fields << field.trimmed(); field.clear(); }
        else field += c;
    }
    fields << field.trimmed();
    return fields;
}

// Index of `text` in `names`, ignoring case; -1 if not there.
int nameIndex(const QStringList &names, const QString &text) {
    for (int i = 0; i < names.size(); ++i)
        if (names.at(i).compare(text, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

QString freqText(quint32 freq10Hz) { return QString::number(freq10Hz / 100000.0, 'f', 5); }

// MHz, with a point or a comma, that the radio can tune.
bool parseFreq(QString text, quint32 &out) {
    bool ok = false;
    const double v = text.replace(',', '.').toDouble(&ok);
    if (!ok || v < 0 || v > 20000) return false;
    const quint32 f = quint32(std::llround(v * 100000.0));
    if (bandForFrequency(f) < 0) return false;
    out = f;
    return true;
}

// Names other programs use for the same thing, so their lists can be used as they are.
int modeIndex(const QString &v) { return v.compare(QLatin1String("SSB"), Qt::CaseInsensitive) == 0 ? 2 : nameIndex(modulationNames(), v); }
int powerIndex(const QString &v) {
    static const QStringList general{"Low", "Mid", "High"};
    const int g = nameIndex(general, v);
    return g >= 0 ? kDefaultPower + g : nameIndex(powerNames(), v);
}

void loadPresets() {
    g_groups.clear();
    g_problems.clear();
    for (const QString &dirPath : g_dirs) {
        const QDir dir(dirPath);
        for (const QString &fileName : dir.entryList({QStringLiteral("*.csv")}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
            QFile f(dir.filePath(fileName));
            if (!f.open(QIODevice::ReadOnly)) {
                g_problems << QStringLiteral("%1: %2").arg(fileName, f.errorString());
                continue;
            }
            const PresetFile parsed = parsePresetCsv(f.readAll(), presetIdFromFileName(fileName));
            for (const QString &p : parsed.problems) g_problems << QStringLiteral("%1: %2").arg(fileName, p);
            if (parsed.group.channels.isEmpty()) continue;
            bool replaced = false;
            for (PresetGroup &g : g_groups)
                if (g.id == parsed.group.id) {
                    g = parsed.group;
                    replaced = true;
                }
            if (!replaced) g_groups << parsed.group;
        }
    }
}

} // namespace

QString presetIdFromFileName(const QString &fileName) {
    static const QRegularExpression order(QStringLiteral("^[0-9]+[-_ ]"));
    QString id = QFileInfo(fileName).completeBaseName();
    return id.remove(order).toLower();
}

quint32 txFrequency(const Channel &c) {
    if (c.shift == 1) return c.freq + c.offset;
    if (c.shift == 2) return c.offset <= c.freq ? c.freq - c.offset : 0;
    return c.freq;
}

QString presetCsv(const PresetGroup &group) {
    QString out = QStringLiteral("# name: %1\n# note: %2\nname,rx,tx,tx_tone,rx_tone,mode,width,power\n").arg(group.name, group.note);
    for (const Channel &c : group.channels)
        out += QStringList{csvField(c.name), freqText(c.freq), c.shift ? freqText(txFrequency(c)) : QString(),
                           toneText(c.txToneType, c.txToneCode), toneText(c.rxToneType, c.rxToneCode),
                           modulationNames().at(c.modulation), kWidthNames.at(c.narrow), powerNames().at(c.power)}.join(',') + '\n';
    return out;
}

PresetFile parsePresetCsv(const QByteArray &bytes, const QString &id) {
    PresetFile result;
    PresetGroup &g = result.group;
    g.id = id;
    g.name = id;
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(bytes);
    if (utf8.hasError()) text = QString::fromLatin1(bytes);
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);

    QStringList columns;
    QChar sep = ',';
    QByteArray scratch(MemSize, '\xff');
    int lineNo = 0;
    for (const QString &rawLine : text.split('\n')) {
        ++lineNo;
        const QString line = rawLine.trimmed();
        auto problem = [&](const QString &what) { result.problems << QStringLiteral("line %1: %2").arg(lineNo).arg(what); };
        if (line.isEmpty()) continue;
        if (line.startsWith('#')) {
            const QString body = line.mid(1).trimmed();
            if (body.startsWith(QLatin1String("name:"), Qt::CaseInsensitive)) g.name = body.mid(5).trimmed();
            else if (body.startsWith(QLatin1String("note:"), Qt::CaseInsensitive)) g.note = body.mid(5).trimmed();
            continue;
        }
        if (columns.isEmpty()) {
            sep = line.count(';') > line.count(',') ? QChar(';') : QChar(',');
            for (const QString &c : csvSplit(line, sep)) columns << c.toLower();
            if (!columns.contains(QLatin1String("name")) || !columns.contains(QLatin1String("rx"))) {
                problem(QStringLiteral("the first row must name the columns and include \"name\" and \"rx\""));
                return result;
            }
            continue;
        }
        const QStringList fields = csvSplit(line, sep);
        auto value = [&](const char *column) {
            const qsizetype i = columns.indexOf(QLatin1String(column));
            return i >= 0 && i < fields.size() ? fields.at(i) : QString();
        };
        Channel c;
        c.empty = false;
        c.power = kDefaultPower;
        c.step = kDefaultStep;
        c.name = value("name");
        if (!parseFreq(value("rx"), c.freq)) {
            problem(QStringLiteral("\"%1\" is not a frequency in MHz the radio covers (18 to 1300); row left out").arg(value("rx")));
            continue;
        }
        quint32 tx = c.freq;
        if (!value("tx").isEmpty() && !parseFreq(value("tx"), tx)) {
            problem(QStringLiteral("\"%1\" is not a frequency in MHz the radio covers (18 to 1300); row left out").arg(value("tx")));
            continue;
        }
        c.shift = tx > c.freq ? 1 : tx < c.freq ? 2 : 0;
        c.offset = tx > c.freq ? tx - c.freq : c.freq - tx;
        bool ok = parseTone(value("tx_tone"), c.txToneType, c.txToneCode) && parseTone(value("rx_tone"), c.rxToneType, c.rxToneCode);
        if (!ok) {
            problem(QStringLiteral("tone not understood (use 88.5, D023N or D023I); row left out"));
            continue;
        }
        auto choice = [&](const char *column, const QStringList &names, int index) {
            if (value(column).isEmpty()) return true;
            if (index >= 0) return true;
            problem(QStringLiteral("%1 \"%2\" is not one of %3; row left out").arg(QLatin1String(column), value(column), names.join(QStringLiteral(", "))));
            return false;
        };
        const int mode = modeIndex(value("mode")), width = nameIndex(kWidthNames, value("width")), power = powerIndex(value("power"));
        // all three are checked, so that every wrong value of a row is reported
        ok = choice("mode", modulationNames(), mode);
        ok = choice("width", kWidthNames, width) && ok;
        ok = choice("power", powerNames(), power) && ok;
        if (!ok) continue;
        if (mode >= 0) c.modulation = mode;
        if (width >= 0) c.narrow = width == 1;
        if (power >= 0) c.power = power;
        // what the radio can actually store: names are cut to 10 characters
        encodeChannel(scratch, 0, c);
        const Channel stored = decodeChannel(scratch, 0);
        const QStringList damage = channelProblems(scratch, 0);
        if (!damage.isEmpty()) {
            problem(damage.join(QStringLiteral("; ")) + QStringLiteral("; row left out"));
            continue;
        }
        if (stored.name != c.name) problem(QStringLiteral("name \"%1\" stored as \"%2\"").arg(c.name, stored.name));
        g.channels << stored;
    }
    if (columns.isEmpty()) result.problems << QStringLiteral("no rows");
    return result;
}

void setPresetDirs(const QStringList &dirs) {
    g_dirs = dirs;
    loadPresets();
}
QStringList presetDirs() { return g_dirs; }
QStringList presetProblems() { return g_problems; }
const QList<PresetGroup> &presetGroups() { return g_groups; }

bool isAirband(const Channel &c) {
    const quint32 tx = txFrequency(c);
    return tx >= 11800000 && tx <= 13700000;
}

bool presetUsesAirband(const PresetGroup &group) {
    for (const Channel &c : group.channels)
        if (isAirband(c)) return true;
    return false;
}

const PresetGroup *findPresetGroup(const QString &id) {
    for (const PresetGroup &g : presetGroups())
        if (g.id == id) return &g;
    return nullptr;
}

int freeSlotsFrom(const QByteArray &image, int firstSlot) {
    int n = 0;
    for (int i = qMax(0, firstSlot); i < ChannelCount; ++i) n += decodeChannel(image, i).empty;
    return n;
}

ImportResult importPreset(QByteArray &image, const PresetGroup &group, const ImportOptions &options) {
    ImportResult r;
    if (image.size() != MemSize || options.firstSlot < 0 || options.firstSlot >= ChannelCount) {
        r.error = QStringLiteral("Bad starting slot.");
        return r;
    }
    QList<int> placed;
    for (int i = options.firstSlot; i < ChannelCount && placed.size() < group.channels.size(); ++i)
        if (decodeChannel(image, i).empty) placed << i;
    if (placed.size() < group.channels.size()) {
        r.error = QStringLiteral("\"%1\" needs %2 empty slots but only %3 are free from slot %4 on.")
                      .arg(group.name).arg(group.channels.size()).arg(placed.size()).arg(options.firstSlot + 1);
        return r;
    }
    for (int i = 0; i < placed.size(); ++i) {
        Channel c = group.channels.at(i);
        if (options.scanList >= 1 && options.scanList <= 3) c.scanLists = 1 << (options.scanList - 1);
        if (options.lockAirband && isAirband(c)) c.txLock = true;
        encodeChannel(image, placed.at(i), c);
    }
    r.ok = true;
    r.placed = placed;
    return r;
}

} // namespace k5
