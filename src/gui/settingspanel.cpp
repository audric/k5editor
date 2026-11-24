#include "settingspanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

using k5::Setting;

SettingsPanel::SettingsPanel(const QString &group, QWidget *parent) : QWidget(parent), m_group(group) {
    auto *inner = new QWidget;
    auto *form = new QFormLayout(inner);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(16);

    for (const Setting &s : k5::settings()) {
        if (s.group != group) continue;
        const QString id = s.id;
        QWidget *editor = nullptr;
        switch (s.kind) {
        case Setting::List: {
            auto *combo = new QComboBox;
            for (int i = 0; i < s.options.size(); ++i)
                if (s.options.at(i) != QLatin1String("N/U")) combo->addItem(s.options.at(i), i);
            combo->setMaxVisibleItems(20);
            combo->setPlaceholderText(tr("(not set)"));
            combo->setFocusPolicy(Qt::StrongFocus);
            connect(combo, &QComboBox::activated, this, [this, id, combo](int) { apply(id, combo->currentData()); });
            editor = combo;
            break;
        }
        case Setting::Bool: {
            auto *box = new QCheckBox;
            connect(box, &QCheckBox::clicked, this, [this, id](bool on) { apply(id, on); });
            editor = box;
            break;
        }
        case Setting::Int: {
            auto *spin = new QSpinBox;
            spin->setRange(s.min, s.max);
            spin->setSingleStep(s.step);
            spin->setKeyboardTracking(false);
            connect(spin, &QSpinBox::valueChanged, this, [this, id](int v) { apply(id, v); });
            editor = spin;
            break;
        }
        case Setting::Text: {
            auto *edit = new QLineEdit;
            edit->setMaxLength(s.maxLength);
            edit->setMinimumWidth(qMax(60, 12 * s.maxLength));
            connect(edit, &QLineEdit::editingFinished, this, [this, id, edit] { apply(id, edit->text()); });
            editor = edit;
            break;
        }
        }
        auto *label = new QLabel(s.label);
        label->setBuddy(editor);
        form->addRow(label, editor);
        m_editors.insert(id, editor);
        m_labels.insert(id, label);
    }

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
    setEnabled(false);
}

void SettingsPanel::setImages(QByteArray *work, const QByteArray *base) {
    m_work = work;
    m_base = base;
    reload();
}

void SettingsPanel::reload() {
    const bool ready = m_work && m_work->size() == k5::MemSize;
    setEnabled(ready);
    if (!ready) return;
    for (const Setting &s : k5::settings())
        if (s.group == m_group) refreshOne(s);
}

void SettingsPanel::refreshOne(const Setting &s) {
    QWidget *editor = m_editors.value(s.id);
    const QVariant v = k5::settingValue(*m_work, s.id);
    m_loading = true;
    {
        const QSignalBlocker block(editor);
        QString note;
        switch (s.kind) {
        case Setting::List: {
            auto *combo = static_cast<QComboBox *>(editor);
            combo->setCurrentIndex(combo->findData(v.toInt()));
            if (combo->currentIndex() < 0) note = tr("The radio holds a value this firmware does not use; it will apply its default.");
            break;
        }
        case Setting::Bool:
            static_cast<QCheckBox *>(editor)->setChecked(v.toBool());
            break;
        case Setting::Int:
            static_cast<QSpinBox *>(editor)->setValue(v.toInt());
            if (v.toInt() < s.min || v.toInt() > s.max)
                note = tr("The radio holds no valid value here; it will apply its default.");
            break;
        case Setting::Text:
            static_cast<QLineEdit *>(editor)->setText(v.toString());
            break;
        }
        editor->setToolTip(note);
    }
    m_loading = false;
    QFont f = font();
    f.setBold(m_base && m_base->size() == k5::MemSize && k5::settingValue(*m_base, s.id) != v);
    m_labels.value(s.id)->setFont(f);
}

void SettingsPanel::apply(const QString &id, const QVariant &value) {
    if (m_loading || !m_work) return;
    const Setting *s = k5::findSetting(id);
    if (k5::settingValue(*m_work, id) == value) return;
    if (!k5::setSettingValue(*m_work, id, value)) {
        if (id == QLatin1String("int_flock"))
            emit rejected(tr("UNLOCK ALL can only be switched on in the radio's own menu."));
        else
            emit rejected(tr("That value is not accepted for \"%1\".").arg(s->label));
    } else {
        emit edited();
    }
    reload();    // some settings share bytes, so refresh the whole group
}
