#pragma once
#include "f4hwn.h"
#include <QHash>
#include <QWidget>

class QLabel;

// A form for all settings of one group, generated from the settings table.
class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    SettingsPanel(const QString &group, QWidget *parent = nullptr);
    void setImages(QByteArray *work, const QByteArray *base);
    void reload();

signals:
    void edited();
    void rejected(const QString &reason);

private:
    void apply(const QString &id, const QVariant &value);
    void refreshOne(const k5::Setting &setting);
    QString m_group;
    QByteArray *m_work = nullptr;
    const QByteArray *m_base = nullptr;
    QHash<QString, QWidget *> m_editors;
    QHash<QString, QLabel *> m_labels;
    bool m_loading = false;
};
