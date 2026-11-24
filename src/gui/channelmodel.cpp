#include "channelmodel.h"
#include <QApplication>
#include <QBrush>
#include <QComboBox>
#include <QPalette>
#include <QStyle>
#include <cmath>

using k5::Channel;

namespace {

const char *kHeaders[ChannelModel::ColumnCount] = {"#", "Name", "Frequency", "Shift", "Offset", "RX tone", "TX tone",
                                                  "Mode", "Width", "Power", "Step", "L1", "L2", "L3", "Compander",
                                                  "Scrambler", "Busy lock", "TX lock", "Reverse", "PTT ID"};
const char *kHeaderTips[ChannelModel::ColumnCount] = {
    "Memory slot", "Up to 10 characters", "Receive frequency in MHz. Clear it to delete the channel.",
    "Direction of the transmit shift", "Transmit shift in MHz", "Tone needed to open the squelch",
    "Tone sent when transmitting", "Modulation", "Channel width", "Transmit power. USER follows the SetPwr setting.",
    "Tuning step in kHz", "Member of scan list 1", "Member of scan list 2", "Member of scan list 3", "Compander",
    "Scrambler", "Do not transmit while the channel is busy", "Never transmit on this channel",
    "Swap receive and transmit frequencies", "DTMF code sent with PTT"};

const QStringList kShift{"", "+", "-"};
const QStringList kWidth{"Wide", "Narrow"};

QString mhzText(quint32 v) {
    QString s = QString::number(v / 100000.0, 'f', 5);
    return s;
}

bool isCheckColumn(int c) {
    return c == ChannelModel::List1 || c == ChannelModel::List2 || c == ChannelModel::List3 ||
           c == ChannelModel::BusyLock || c == ChannelModel::TxLock || c == ChannelModel::Reverse;
}

bool checkValue(const Channel &c, int col) {
    switch (col) {
    case ChannelModel::List1: return c.scanLists & 1;
    case ChannelModel::List2: return c.scanLists & 2;
    case ChannelModel::List3: return c.scanLists & 4;
    case ChannelModel::BusyLock: return c.busyLock;
    case ChannelModel::TxLock: return c.txLock;
    case ChannelModel::Reverse: return c.reverse;
    }
    return false;
}

QString stepText(int i) {
    return i >= 0 && i < k5::stepsKHz().size() ? QString::number(k5::stepsKHz().at(i)) : QStringLiteral("?%1").arg(i);
}

QString at(const QStringList &l, int i) { return i >= 0 && i < l.size() ? l.at(i) : QStringLiteral("?%1").arg(i); }

QStringList options(int col) {
    switch (col) {
    case ChannelModel::Shift: return kShift;
    case ChannelModel::RxTone:
    case ChannelModel::TxTone: return k5::allToneTexts();
    case ChannelModel::Mode: return k5::modulationNames();
    case ChannelModel::Width: return kWidth;
    case ChannelModel::Power: return k5::powerNames();
    case ChannelModel::Step: {
        QStringList l;
        for (int i = 0; i < k5::stepsKHz().size(); ++i) l << stepText(i);
        return l;
    }
    case ChannelModel::Compander: return k5::companderNames();
    case ChannelModel::Scrambler: return k5::scramblerNames();
    case ChannelModel::PttId: return k5::pttIdNames();
    }
    return {};
}

QString text(const Channel &c, int col) {
    switch (col) {
    case ChannelModel::Name: return c.name;
    case ChannelModel::Freq: return mhzText(c.freq);
    case ChannelModel::Shift: return at(kShift, c.shift);
    case ChannelModel::Offset: return c.offset || c.shift ? mhzText(c.offset) : QString();
    case ChannelModel::RxTone: return k5::toneText(c.rxToneType, c.rxToneCode);
    case ChannelModel::TxTone: return k5::toneText(c.txToneType, c.txToneCode);
    case ChannelModel::Mode: return at(k5::modulationNames(), c.modulation);
    case ChannelModel::Width: return c.narrow ? kWidth[1] : kWidth[0];
    case ChannelModel::Power: return at(k5::powerNames(), c.power);
    case ChannelModel::Step: return stepText(c.step);
    case ChannelModel::Compander: return at(k5::companderNames(), c.compander);
    case ChannelModel::Scrambler: return at(k5::scramblerNames(), c.scrambler);
    case ChannelModel::PttId: return at(k5::pttIdNames(), c.pttId);
    }
    return {};
}

bool parseMhz(const QString &input, quint32 &out) {
    QString t = input.trimmed();
    t.replace(',', '.');
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (!ok || v < 0 || v > 20000) return false;
    out = quint32(std::llround(v * 100000.0));
    return true;
}

} // namespace

ChannelModel::ChannelModel(QObject *parent) : QAbstractTableModel(parent) {}

void ChannelModel::setImages(QByteArray *work, const QByteArray *base) {
    beginResetModel();
    m_work = work;
    m_base = base;
    endResetModel();
}

void ChannelModel::reload() {
    beginResetModel();
    endResetModel();
}

int ChannelModel::rowCount(const QModelIndex &parent) const { return parent.isValid() || !ready() ? 0 : k5::ChannelCount; }
int ChannelModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

Channel ChannelModel::channel(int row) const { return ready() ? k5::decodeChannel(*m_work, row) : Channel{}; }

QVariant ChannelModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return {};
    if (role == Qt::DisplayRole) return QString::fromLatin1(kHeaders[section]);
    if (role == Qt::ToolTipRole) return QString::fromLatin1(kHeaderTips[section]);
    return {};
}

QVariant ChannelModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || !ready()) return {};
    const int row = index.row(), col = index.column();
    const Channel c = k5::decodeChannel(*m_work, row);

    if (role == EmptyRole) return c.empty;
    if (role == OptionsRole) return options(col);
    if (col == Num) {
        const QStringList problems = k5::channelProblems(*m_work, row);
        if (role == Qt::DisplayRole) return row + 1;
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignRight | Qt::AlignVCenter);
        if (role == Qt::ToolTipRole && !problems.isEmpty()) return problems.join('\n');
        if (role == Qt::DecorationRole && !problems.isEmpty())
            return QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
        if (role == Qt::ForegroundRole && c.empty) return QApplication::palette().brush(QPalette::Disabled, QPalette::Text);
        return {};
    }
    if (c.empty) {
        if (role == Qt::EditRole && (col == Freq || col == Name)) return QString();
        return {};
    }
    if (isCheckColumn(col)) {
        if (role == Qt::CheckStateRole) return checkValue(c, col) ? Qt::Checked : Qt::Unchecked;
    } else if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return text(c, col);
    }
    if (role == Qt::TextAlignmentRole && (col == Freq || col == Offset)) return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::FontRole && m_base && m_base->size() == k5::MemSize) {
        // changed since the image was read: shown in bold
        const Channel b = k5::decodeChannel(*m_base, row);
        const bool changed = b.empty || (isCheckColumn(col) ? checkValue(b, col) != checkValue(c, col) : text(b, col) != text(c, col));
        if (changed) {
            QFont f = QApplication::font();
            f.setBold(true);
            return f;
        }
    }
    return {};
}

Qt::ItemFlags ChannelModel::flags(const QModelIndex &index) const {
    if (!index.isValid() || !ready()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    const int col = index.column();
    if (col == Num) return f;
    const bool empty = k5::decodeChannel(*m_work, index.row()).empty;
    if (empty) return col == Freq ? f | Qt::ItemIsEditable : f;
    return isCheckColumn(col) ? f | Qt::ItemIsUserCheckable : f | Qt::ItemIsEditable;
}

bool ChannelModel::fail(const QString &reason) {
    m_error = reason;
    emit rejected(reason);
    return false;
}

bool ChannelModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || !ready()) return false;
    const int row = index.row(), col = index.column();
    Channel c = k5::decodeChannel(*m_work, row);
    const Channel before = c;

    if (isCheckColumn(col)) {
        if (role != Qt::CheckStateRole || c.empty) return false;
        const bool on = value.toInt() == Qt::Checked;
        auto setBit = [&](int bit) { c.scanLists = on ? c.scanLists | bit : c.scanLists & ~bit; };
        switch (col) {
        case List1: setBit(1); break;
        case List2: setBit(2); break;
        case List3: setBit(4); break;
        case BusyLock: c.busyLock = on; break;
        case TxLock: c.txLock = on; break;
        case Reverse: c.reverse = on; break;
        }
    } else {
        if (role != Qt::EditRole) return false;
        const QString t = value.toString().trimmed();
        if (col == Freq) {
            if (t.isEmpty()) {
                c = Channel{};
            } else {
                quint32 f = 0;
                if (!parseMhz(t, f) || k5::bandForFrequency(f) < 0)
                    return fail(tr("\"%1\" is not a frequency the radio can tune (18 to 1300 MHz).").arg(t));
                if (c.empty) {
                    c = Channel{};
                    c.empty = false;
                    c.step = 4;          // 12.5 kHz
                }
                c.freq = f;
            }
        } else if (c.empty) {
            return false;
        } else if (col == Name) {
            if (t.size() > 10) return fail(tr("Names can have at most 10 characters."));
            for (const QChar ch : t)
                if (ch.unicode() < 0x20 || ch.unicode() > 0x7e) return fail(tr("Names can only use plain ASCII characters."));
            c.name = t;
        } else if (col == Offset) {
            quint32 o = 0;
            if (!t.isEmpty() && !parseMhz(t, o)) return fail(tr("\"%1\" is not a valid offset in MHz.").arg(t));
            c.offset = o;
        } else if (col == RxTone || col == TxTone) {
            int type = 0, code = 0;
            if (!k5::parseTone(t, type, code)) return fail(tr("\"%1\" is not a known tone. Use for example 88.5, D023N or D023I.").arg(t));
            (col == RxTone ? c.rxToneType : c.txToneType) = type;
            (col == RxTone ? c.rxToneCode : c.txToneCode) = code;
        } else {
            const int i = options(col).indexOf(t);
            if (i < 0) return fail(tr("\"%1\" is not one of the choices for this column.").arg(t));
            switch (col) {
            case Shift: c.shift = i; break;
            case Mode: c.modulation = i; break;
            case Width: c.narrow = i == 1; break;
            case Power: c.power = i; break;
            case Step: c.step = i; break;
            case Compander: c.compander = i; break;
            case Scrambler: c.scrambler = i; break;
            case PttId: c.pttId = i; break;
            }
        }
    }
    if (c == before) return true;
    setChannel(row, c);
    return true;
}

void ChannelModel::setChannel(int row, const Channel &c) {
    if (!ready() || row < 0 || row >= k5::ChannelCount) return;
    k5::encodeChannel(*m_work, row, c);
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    emit edited();
}

void ChannelModel::swapChannels(int a, int b) {
    if (!ready() || a == b) return;
    const Channel ca = channel(a), cb = channel(b);
    k5::encodeChannel(*m_work, a, cb);
    k5::encodeChannel(*m_work, b, ca);
    emit dataChanged(index(qMin(a, b), 0), index(qMax(a, b), ColumnCount - 1));
    emit edited();
}

int ChannelModel::problemCount() const {
    int n = 0;
    if (ready())
        for (int i = 0; i < k5::ChannelCount; ++i) n += !k5::channelProblems(*m_work, i).isEmpty();
    return n;
}

// Channels whose only fault is a band marker left by another firmware.
static bool onlyBandMarkerWrong(const QByteArray &image, int row) {
    if (k5::channelProblems(image, row).isEmpty()) return false;
    QByteArray copy = image;
    k5::encodeChannel(copy, row, k5::decodeChannel(copy, row));
    return k5::channelProblems(copy, row).isEmpty();
}

int ChannelModel::bandMarkerProblemCount() const {
    int n = 0;
    if (ready())
        for (int i = 0; i < k5::ChannelCount; ++i) n += onlyBandMarkerWrong(*m_work, i);
    return n;
}

int ChannelModel::fixBandMarkers() {
    int n = 0;
    if (!ready()) return 0;
    for (int i = 0; i < k5::ChannelCount; ++i)
        if (onlyBandMarkerWrong(*m_work, i)) {
            k5::encodeChannel(*m_work, i, k5::decodeChannel(*m_work, i));
            ++n;
        }
    if (n) {
        emit dataChanged(index(0, 0), index(k5::ChannelCount - 1, ColumnCount - 1));
        emit edited();
    }
    return n;
}

bool ChannelFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
    const QAbstractItemModel *m = sourceModel();
    const bool empty = m->data(m->index(row, ChannelModel::Num, parent), ChannelModel::EmptyRole).toBool();
    if (m_hideEmpty && empty) return false;
    if (m_text.isEmpty()) return true;
    if (empty) return false;
    for (int col : {int(ChannelModel::Num), int(ChannelModel::Name), int(ChannelModel::Freq)})
        if (m->data(m->index(row, col, parent), Qt::DisplayRole).toString().contains(m_text, Qt::CaseInsensitive)) return true;
    return false;
}

QWidget *ChannelDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const QStringList choices = index.data(ChannelModel::OptionsRole).toStringList();
    if (choices.isEmpty()) return QStyledItemDelegate::createEditor(parent, option, index);
    auto *combo = new QComboBox(parent);
    for (const QString &c : choices) combo->addItem(c.isEmpty() ? tr("(none)") : c, c);
    combo->setMaxVisibleItems(20);
    return combo;
}

void ChannelDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        combo->setCurrentIndex(qMax(0, combo->findData(index.data(Qt::EditRole).toString())));
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void ChannelDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        model->setData(index, combo->currentData().toString(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}
