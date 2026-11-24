#pragma once
#include "f4hwn.h"
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

// The 200 memory channels of the working image, one row per slot.
class ChannelModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Num, Name, Freq, Shift, Offset, RxTone, TxTone, Mode, Width, Power, Step, List1, List2, List3,
                  Compander, Scrambler, BusyLock, TxLock, Reverse, PttId, ColumnCount };
    enum { OptionsRole = Qt::UserRole + 1, EmptyRole };

    explicit ChannelModel(QObject *parent = nullptr);
    // Both images stay owned by the caller; call reload() after replacing their contents.
    void setImages(QByteArray *work, const QByteArray *base);
    void reload();

    int rowCount(const QModelIndex & = {}) const override;
    int columnCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    k5::Channel channel(int row) const;
    void setChannel(int row, const k5::Channel &channel);
    void swapChannels(int a, int b);
    int bandMarkerProblemCount() const;
    int fixBandMarkers();
    int problemCount() const;
    QString lastError() const { return m_error; }

signals:
    void edited();
    void rejected(const QString &reason);

private:
    bool ready() const { return m_work && m_work->size() == k5::MemSize; }
    bool fail(const QString &reason);
    QByteArray *m_work = nullptr;
    const QByteArray *m_base = nullptr;
    QString m_error;
};

class ChannelFilter : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setText(const QString &text) { m_text = text.trimmed(); invalidateFilter(); }
    void setHideEmpty(bool hide) { m_hideEmpty = hide; invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    QString m_text;
    bool m_hideEmpty = false;
};

// Offers a drop-down for columns that have a fixed set of values.
class ChannelDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
};
