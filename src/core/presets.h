// Ready-made channel lists (CB, marine VHF, PMR446 ...) that can be added to an image.
// Each list is a CSV file in a lists folder; see lists/README.md for the format.
#pragma once
#include "f4hwn.h"

namespace k5 {

struct PresetGroup {
    QString id;
    QString name;
    QString note;           // one line: where it applies, what to know
    QList<Channel> channels;
};
// One list as CSV text, and back. `id` is the file name without ".csv" and
// without a leading "NN-" (which only sets the order).
QString presetCsv(const PresetGroup &group);
struct PresetFile {
    PresetGroup group;
    QStringList problems;    // "line 7: ..." for rows that were left out or changed
};
PresetFile parsePresetCsv(const QByteArray &text, const QString &id);
QString presetIdFromFileName(const QString &fileName);

// Folders the lists are read from, in order. A file in a later folder replaces
// the list with the same id from an earlier one; other files add lists.
void setPresetDirs(const QStringList &dirs);    // also reloads
QStringList presetDirs();
QStringList presetProblems();                   // from the last load, "file: line N: ..."
const QList<PresetGroup> &presetGroups();
const PresetGroup *findPresetGroup(const QString &id);

// Frequency the channel transmits on, in 10 Hz units.
quint32 txFrequency(const Channel &channel);
// True if the channel transmits in the aeronautical band (118-137 MHz), where
// transmitting is never appropriate from this radio.
bool isAirband(const Channel &channel);
bool presetUsesAirband(const PresetGroup &group);

// Empty channel slots at or after firstSlot.
int freeSlotsFrom(const QByteArray &image, int firstSlot);

struct ImportOptions {
    int firstSlot = 0;
    int scanList = 0;           // 1-3: also put the channels in that scan list
    bool lockAirband = true;    // switch on "TX lock" for the 118-137 MHz channels
};
struct ImportResult {
    bool ok = false;
    QString error;
    QList<int> placed;       // where the channels went, in order
};
// Puts the group's channels into the empty slots at or after firstSlot, in
// order, skipping slots that are in use. Changes nothing unless everything fits.
ImportResult importPreset(QByteArray &image, const PresetGroup &group, const ImportOptions &options);

} // namespace k5
