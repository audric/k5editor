#include "mainwindow.h"
#include "channelmodel.h"
#include "presets.h"
#include "serialtransport.h"
#include "settingspanel.h"
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

using k5::Channel;

// "1 channel" / "3 channels"
static QString counted(int n, const char *one, const char *many) {
    return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
}

void RadioJob::run() {
    try {
        k5::SerialTransport transport(port);
        k5::Radio radio(transport);
        if (kind == Read) {
            firmware = radio.hello();
            image = radio.readAll([this](int d, int t) { emit progress(tr("Reading the radio"), d, t); });
        } else {
            options.progress = [this](const QString &stage, int d, int t) { emit progress(stage, d, t); };
            result = k5::writeToRadio(radio, base, work, options);
        }
    } catch (const std::exception &e) {
        error = QString::fromStdString(e.what());
    }
}

// What differs between two images, in words, for the confirmation before writing.
static QStringList describeChanges(const QByteArray &from, const QByteArray &to) {
    QStringList lines;
    for (int i = 0; i < k5::ChannelCount; ++i) {
        const Channel a = k5::decodeChannel(from, i), b = k5::decodeChannel(to, i);
        const bool rawSame = from.mid(i * 16, 16) == to.mid(i * 16, 16) && from[0xd60 + i] == to[0xd60 + i] &&
                             from.mid(0xf50 + i * 16, 16) == to.mid(0xf50 + i * 16, 16);
        if (rawSame) continue;
        if (a.empty && !b.empty)
            lines << QObject::tr("Channel %1: new, %2 MHz \"%3\"").arg(i + 1).arg(b.freq / 100000.0, 0, 'f', 5).arg(b.name);
        else if (!a.empty && b.empty)
            lines << QObject::tr("Channel %1: deleted (was %2 MHz \"%3\")").arg(i + 1).arg(a.freq / 100000.0, 0, 'f', 5).arg(a.name);
        else if (!b.empty)
            lines << QObject::tr("Channel %1 \"%2\": changed").arg(i + 1).arg(b.name);
    }
    for (const k5::Setting &s : k5::settings()) {
        const QVariant a = k5::settingValue(from, s.id), b = k5::settingValue(to, s.id);
        if (a == b) continue;
        auto show = [&](const QVariant &v) {
            if (s.kind == k5::Setting::List) return v.toInt() >= 0 ? s.options.at(v.toInt()) : QObject::tr("(not set)");
            if (s.kind == k5::Setting::Bool) return v.toBool() ? QObject::tr("on") : QObject::tr("off");
            return v.toString();
        };
        lines << QStringLiteral("%1: %2 → %3").arg(s.label, show(a), show(b));
    }
    return lines;
}

MainWindow::MainWindow() {
    buildUi();
    refreshPorts();
    updateState();
}

void MainWindow::buildUi() {
    setWindowTitle(tr("K5 Editor"));
    resize(1280, 760);

    m_model = new ChannelModel(this);
    m_filter = new ChannelFilter(this);
    m_filter->setSourceModel(m_model);

    // ---- toolbar
    auto *bar = addToolBar(tr("Main"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->addWidget(new QLabel(tr(" Port ")));
    m_ports = new QComboBox;
    m_ports->setMinimumWidth(150);
    bar->addWidget(m_ports);
    auto *rescan = bar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("Rescan"));
    rescan->setToolTip(tr("Look for serial ports again"));
    connect(rescan, &QAction::triggered, this, &MainWindow::refreshPorts);
    bar->addSeparator();
    m_readAct = bar->addAction(style()->standardIcon(QStyle::SP_ArrowDown), tr("Read from radio"));
    m_writeAct = bar->addAction(style()->standardIcon(QStyle::SP_ArrowUp), tr("Write to radio"));
    bar->addSeparator();
    auto *newAct = bar->addAction(style()->standardIcon(QStyle::SP_FileIcon), tr("New"));
    newAct->setToolTip(tr("Start an empty image: no channels, default settings"));
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &MainWindow::newFile);
    auto *openAct = bar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open image…"));
    m_saveAct = bar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save image as…"));
    bar->addSeparator();
    m_revertAct = bar->addAction(style()->standardIcon(QStyle::SP_DialogResetButton), tr("Undo all changes"));
    m_readAct->setShortcut(QKeySequence(tr("Ctrl+R")));
    m_writeAct->setShortcut(QKeySequence(tr("Ctrl+W")));
    openAct->setShortcut(QKeySequence::Open);
    m_saveAct->setShortcut(QKeySequence::Save);
    connect(m_readAct, &QAction::triggered, this, &MainWindow::readFromRadio);
    connect(m_writeAct, &QAction::triggered, this, [this] { writeToRadio(); });
    connect(openAct, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(m_saveAct, &QAction::triggered, this, &MainWindow::saveFileDialog);
    connect(m_revertAct, &QAction::triggered, this, &MainWindow::revert);

    // ---- menu
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addActions({m_readAct, m_writeAct});
    fileMenu->addSeparator();
    fileMenu->addActions({newAct, openAct, m_saveAct});
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Open backup folder"), this, [this] {
        QDir().mkpath(backupDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(backupDir()));
    });
    fileMenu->addAction(tr("Choose backup folder…"), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Backup folder"), backupDir());
        if (!dir.isEmpty()) QSettings().setValue("backupDir", dir);
        updateState();
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);
    auto *channelsMenu = menuBar()->addMenu(tr("&Channels"));
    m_importAct = channelsMenu->addAction(tr("Add ready-made channel lists…"), this, [this] { importPresets(); });
    m_importAct->setToolTip(tr("CB, marine VHF, PMR446, LPD433, Freenet, Italian airports and repeaters, and more"));

    // ---- channels tab
    auto *channelsPage = new QWidget;
    auto *filterEdit = new QLineEdit;
    filterEdit->setPlaceholderText(tr("Filter by name, frequency or number"));
    filterEdit->setClearButtonEnabled(true);
    auto *hideEmpty = new QCheckBox(tr("Hide empty slots"));
    m_table = new QTableView;
    m_table->setModel(m_filter);
    m_table->setItemDelegate(new ChannelDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 8);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    m_table->setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(filterEdit, &QLineEdit::textChanged, m_filter, &ChannelFilter::setText);
    connect(hideEmpty, &QCheckBox::toggled, m_filter, &ChannelFilter::setHideEmpty);

    auto addTableAction = [this](const QString &text, const QKeySequence &key, void (MainWindow::*slot)()) {
        auto *a = new QAction(text, m_table);
        a->setShortcut(key);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(a, &QAction::triggered, this, slot);
        m_table->addAction(a);
        return a;
    };
    addTableAction(tr("Copy"), QKeySequence::Copy, &MainWindow::copyChannels);
    addTableAction(tr("Paste"), QKeySequence::Paste, &MainWindow::pasteChannels);
    addTableAction(tr("Delete channel"), QKeySequence::Delete, &MainWindow::deleteChannels);
    auto *up = new QAction(tr("Move up"), m_table);
    up->setShortcut(QKeySequence(tr("Alt+Up")));
    up->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(up, &QAction::triggered, this, [this] { moveChannels(-1); });
    auto *down = new QAction(tr("Move down"), m_table);
    down->setShortcut(QKeySequence(tr("Alt+Down")));
    down->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(down, &QAction::triggered, this, [this] { moveChannels(1); });
    m_table->addActions({up, down});
    m_fixAct = new QAction(this);
    connect(m_fixAct, &QAction::triggered, this, &MainWindow::fixBandMarkers);
    auto *fixButton = new QToolBar;
    fixButton->addAction(m_fixAct);
    fixButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto *importButton = new QPushButton(tr("Add channel lists…"));
    importButton->setToolTip(m_importAct->toolTip());
    connect(importButton, &QPushButton::clicked, this, [this] { importPresets(); });

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(filterEdit, 1);
    filterRow->addWidget(hideEmpty);
    filterRow->addWidget(importButton);
    filterRow->addWidget(fixButton);
    auto *channelsLayout = new QVBoxLayout(channelsPage);
    channelsLayout->addLayout(filterRow);
    channelsLayout->addWidget(m_table, 1);

    // ---- tabs
    auto *tabs = new QTabWidget;
    tabs->addTab(channelsPage, tr("Channels"));
    QStringList groups;
    for (const k5::Setting &s : k5::settings())
        if (!groups.contains(s.group)) groups << s.group;
    for (const QString &g : groups) {
        auto *panel = new SettingsPanel(g);
        m_panels << panel;
        tabs->addTab(panel, g);
        connect(panel, &SettingsPanel::edited, this, &MainWindow::updateState);
        connect(panel, &SettingsPanel::rejected, this, [this](const QString &why) { notify(why, true); });
    }
    m_tabs = tabs;

    m_banner = new QLabel;
    m_banner->setWordWrap(true);
    m_banner->setMargin(8);
    m_banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_banner->hide();

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 0);
    layout->addWidget(m_banner);
    layout->addWidget(tabs, 1);
    setCentralWidget(central);

    m_status = new QLabel;
    statusBar()->addPermanentWidget(m_status, 1);

    connect(m_model, &ChannelModel::edited, this, &MainWindow::updateState);
    connect(m_model, &ChannelModel::rejected, this, [this](const QString &why) { notify(why, true); });
}

void MainWindow::showTab(int index) { static_cast<QTabWidget *>(m_tabs)->setCurrentIndex(index); }

QString MainWindow::backupDir() const {
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/backups");
    return QSettings().value("backupDir", fallback).toString();
}

void MainWindow::refreshPorts() {
    const QString current = m_ports->currentText();
    m_ports->clear();
    m_ports->addItems(k5::availableSerialPorts());
    const int i = m_ports->findText(current);
    if (i >= 0) m_ports->setCurrentIndex(i);
    updateState();
}

void MainWindow::notify(const QString &text, bool warning) {
    m_banner->setText(text);
    const QPalette pal = palette();
    const QColor accent = warning ? QColor(0xb4, 0x53, 0x09) : pal.color(QPalette::Highlight);
    m_banner->setStyleSheet(QStringLiteral("QLabel { border: 1px solid %1; border-left: 5px solid %1; border-radius: 3px; }").arg(accent.name()));
    m_banner->setVisible(!text.isEmpty());
}

void MainWindow::updateState() {
    const bool have = m_work.size() == k5::MemSize;
    const bool known = !m_fromRadio || k5::firmwareSupported(m_firmware);
    const bool port = m_ports->count() > 0;
    m_readAct->setEnabled(port);
    m_writeAct->setEnabled(have && known && port && (isDirty() || !m_fromRadio));
    m_saveAct->setEnabled(have);
    m_revertAct->setEnabled(have && isDirty());
    m_tabs->setEnabled(have && known);
    m_importAct->setEnabled(have && known);

    const int markers = have ? m_model->bandMarkerProblemCount() : 0;
    m_fixAct->setText(tr("Fix %1").arg(counted(markers, "band marker", "band markers")));
    m_fixAct->setToolTip(tr("These channels carry a band marker left by another firmware. Fixing only corrects the marker."));
    m_fixAct->setVisible(markers > 0);

    QStringList parts;
    if (!port) parts << tr("No serial port found. Plug in the programming cable and press Rescan.");
    if (have) {
        parts << m_source;
        const int changes = int(describeChanges(m_base, m_work).size());
        parts << (isDirty() ? counted(qMax(1, changes), "unsaved change", "unsaved changes") : tr("No changes"));
        const int problems = m_model->problemCount();
        if (problems) parts << tr("%1 flagged").arg(counted(problems, "channel", "channels"));
    } else if (port) {
        parts << tr("Read from the radio, open an image file or start a new one to begin.");
    }
    m_status->setText(parts.join(QStringLiteral("   •   ")));
    setWindowTitle(have ? tr("%1%2 — K5 Editor").arg(m_source, isDirty() ? QStringLiteral(" *") : QString()) : tr("K5 Editor"));
}

void MainWindow::setImage(const QByteArray &image, const QString &firmware, const QString &source) {
    m_base = image;
    m_work = image;
    m_firmware = firmware;
    m_source = source;
    m_model->setImages(&m_work, &m_base);
    for (SettingsPanel *p : m_panels) p->setImages(&m_work, &m_base);
    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(ChannelModel::Name, 120);
    updateState();
}

bool MainWindow::confirmDiscard() {
    if (m_work.size() != k5::MemSize || !isDirty()) return true;
    return QMessageBox::question(this, tr("Discard changes?"),
                                 tr("You have changes that were not written to the radio or saved to a file. Discard them?"),
                                 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (confirmDiscard()) event->accept();
    else event->ignore();
}

void MainWindow::runJob(RadioJob *job, const QString &title, std::function<void(RadioJob *)> done) {
    job->setParent(this);
    job->port = m_ports->currentText();
    auto *dialog = new QProgressDialog(title, QString(), 0, 100, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    dialog->setValue(0);
    connect(job, &RadioJob::progress, dialog, [dialog](const QString &stage, int d, int t) {
        dialog->setLabelText(stage + QStringLiteral("…"));
        dialog->setMaximum(t);
        dialog->setValue(d);
    });
    connect(job, &QThread::finished, this, [this, job, dialog, done] {
        dialog->close();
        dialog->deleteLater();
        if (!job->error.isEmpty())
            QMessageBox::critical(this, tr("Radio"), job->error);
        else
            done(job);
        job->deleteLater();
    });
    job->start();
}

void MainWindow::readFromRadio() {
    if (!confirmDiscard()) return;
    auto *job = new RadioJob;
    job->kind = RadioJob::Read;
    runJob(job, tr("Reading the radio…"), [this](RadioJob *j) {
        m_fromRadio = true;
        setImage(j->image, j->firmware, tr("Radio on %1, firmware %2").arg(j->port, j->firmware));
        if (!k5::firmwareSupported(j->firmware))
            notify(tr("This radio runs \"%1\". This program only understands F4HWN v4.3, so editing and writing are switched off. "
                      "You can still save the memory image to a file as a backup.").arg(j->firmware), true);
        else
            notify(QString());
    });
}

void MainWindow::writeToRadio(bool overwriteConflicts) {
    if (m_work.size() != k5::MemSize) return;
    if (m_fromRadio) {
        startWrite(m_base, overwriteConflicts);
        return;
    }
    // The image came from a file: compare it with what the radio holds now.
    auto *job = new RadioJob;
    job->kind = RadioJob::Read;
    runJob(job, tr("Reading the radio to compare…"), [this](RadioJob *j) {
        if (!k5::firmwareSupported(j->firmware)) {
            QMessageBox::warning(this, tr("Write to radio"),
                                 tr("The radio runs \"%1\". This program only writes to F4HWN v4.3. Nothing was written.").arg(j->firmware));
            return;
        }
        startWrite(j->image, false);
    });
}

void MainWindow::startWrite(const QByteArray &base, bool overwriteConflicts) {
    const auto plan = k5::planWrites(base, m_work);
    if (plan.isEmpty()) {
        QMessageBox::information(this, tr("Write to radio"), tr("The radio already holds these channels and settings. Nothing to write."));
        return;
    }
    if (!overwriteConflicts) {
        QStringList lines = describeChanges(base, m_work);
        const int total = int(lines.size());
        if (total > 25) {
            lines = lines.mid(0, 25);
            lines << tr("… and %1 more").arg(total - 25);
        }
        if (lines.isEmpty()) lines << tr("Some bytes outside the listed channels and settings differ.");
        QMessageBox box(QMessageBox::Question, tr("Write to radio"),
                        tr("Write %1 to the radio?").arg(counted(qMax(1, total), "change", "changes")), QMessageBox::Cancel, this);
        QString info = tr("A backup of the radio's current memory is saved first in:\n%1\n\n"
                          "Calibration data is never written. The radio restarts when the write is verified.").arg(backupDir());
        if (base.mid(k5::ProgEnd, k5::ExtraStart - k5::ProgEnd) != m_work.mid(k5::ProgEnd, k5::ExtraStart - k5::ProgEnd))
            info += tr("\n\nThe calibration area in this image differs from the radio's. It will be left as it is on the radio.");
        box.setInformativeText(info);
        box.setDetailedText(lines.join('\n'));
        auto *go = box.addButton(tr("Write"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() != go) return;
    }

    auto *job = new RadioJob;
    job->kind = RadioJob::Write;
    job->base = base;
    job->work = m_work;
    job->options.backupDir = backupDir();
    job->options.overwriteConflicts = overwriteConflicts;
    runJob(job, tr("Writing to the radio…"), [this, base](RadioJob *j) {
        const k5::WriteResult &r = j->result;
        switch (r.status) {
        case k5::WriteResult::Ok:
            m_fromRadio = true;
            setImage(r.radioImage, r.firmware, tr("Radio on %1, firmware %2").arg(j->port, r.firmware));
            notify(tr("Written and verified. The radio is restarting. Backup of the previous contents: %1").arg(r.backupPath));
            break;
        case k5::WriteResult::NothingToWrite:
            QMessageBox::information(this, tr("Write to radio"), tr("Nothing to write."));
            break;
        case k5::WriteResult::UnsupportedFirmware:
            QMessageBox::warning(this, tr("Write to radio"),
                                 tr("The radio runs \"%1\". This program only writes to F4HWN v4.3. Nothing was written.").arg(r.firmware));
            break;
        case k5::WriteResult::Conflict: {
            QStringList where;
            for (int o : r.conflictOffsets) where << QStringLiteral("0x%1").arg(o, 4, 16, QLatin1Char('0'));
            QMessageBox box(QMessageBox::Warning, tr("The radio changed"),
                            tr("The radio changed some of the same memory since it was read (for example through its own menu). Nothing was written."),
                            QMessageBox::Cancel, this);
            box.setInformativeText(tr("Read the radio again to see its current values, or overwrite them with yours.\nBackup: %1").arg(r.backupPath));
            box.setDetailedText(tr("Changed blocks: %1").arg(where.join(QStringLiteral(", "))));
            auto *over = box.addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
            box.exec();
            if (box.clickedButton() == over) {
                if (m_fromRadio) writeToRadio(true);
                else startWrite(base, true);
            }
            break;
        }
        case k5::WriteResult::VerifyFailed:
            QMessageBox::critical(this, tr("Write to radio"),
                                  tr("The radio did not keep everything that was written, so it was not restarted. "
                                     "Try writing again. The memory as it was before is saved in:\n%1").arg(r.backupPath));
            break;
        }
    });
}

bool MainWindow::openFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open image"), tr("Cannot open %1: %2").arg(path, f.errorString()));
        return false;
    }
    const QByteArray data = f.readAll();
    if (data.size() != k5::MemSize) {
        QMessageBox::warning(this, tr("Open image"), tr("%1 is %2 bytes; a memory image is exactly %3 bytes.").arg(path).arg(data.size()).arg(k5::MemSize));
        return false;
    }
    m_fromRadio = false;
    setImage(data, QString(), QFileInfo(path).fileName());
    notify(tr("Opened %1. The file is read as an F4HWN v4.3 image; an image from another firmware will show wrong values.").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::newFile() {
    if (!confirmDiscard()) return;
    m_fromRadio = false;
    setImage(k5::blankImage(), QString(), tr("New image"));
    notify(tr("New, empty image: no channels, default settings. Writing it to a radio replaces all the radio's channels and "
              "settings with the ones shown here. Calibration is never written."));
}

void MainWindow::openFileDialog() {
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open image"), QSettings().value("lastDir").toString(),
                                                      tr("Memory images (*.bin *.img);;All files (*)"));
    if (path.isEmpty()) return;
    QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
    openFile(path);
}

void MainWindow::saveFileDialog() {
    QString path = QFileDialog::getSaveFileName(this, tr("Save image"), QSettings().value("lastDir").toString(),
                                                tr("Memory images (*.bin)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".bin");
    QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(m_work) != m_work.size()) {
        QMessageBox::warning(this, tr("Save image"), tr("Cannot save %1: %2").arg(path, f.errorString()));
        return;
    }
    notify(tr("Saved %1. Changes are still not on the radio until you write them.").arg(path));
}

void MainWindow::revert() {
    if (QMessageBox::question(this, tr("Undo all changes"), tr("Go back to the image as it was read or opened?")) != QMessageBox::Yes) return;
    m_work = m_base;
    m_model->reload();
    for (SettingsPanel *p : m_panels) p->reload();
    updateState();
}

QList<int> MainWindow::selectedRows() const {
    QList<int> rows;
    for (const QModelIndex &i : m_table->selectionModel()->selectedRows()) rows << m_filter->mapToSource(i).row();
    if (rows.isEmpty() && m_table->currentIndex().isValid()) rows << m_filter->mapToSource(m_table->currentIndex()).row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

void MainWindow::copyChannels() {
    m_clipboard.clear();
    for (int r : selectedRows()) m_clipboard << m_model->channel(r);
    statusBar()->showMessage(tr("%1 copied").arg(counted(int(m_clipboard.size()), "channel", "channels")), 3000);
}

void MainWindow::pasteChannels() {
    const QList<int> rows = selectedRows();
    if (m_clipboard.isEmpty() || rows.isEmpty()) return;
    const int first = rows.first();
    const int count = qMin(int(m_clipboard.size()), k5::ChannelCount - first);
    int overwritten = 0;
    for (int i = 0; i < count; ++i) overwritten += !m_model->channel(first + i).empty;
    if (overwritten && QMessageBox::question(this, tr("Paste"), tr("This replaces %1 starting at slot %2. Continue?").arg(counted(overwritten, "existing channel", "existing channels")).arg(first + 1)) != QMessageBox::Yes)
        return;
    for (int i = 0; i < count; ++i) m_model->setChannel(first + i, m_clipboard.at(i));
}

void MainWindow::deleteChannels() {
    for (int r : selectedRows())
        if (!m_model->channel(r).empty) m_model->setChannel(r, Channel{});
}

void MainWindow::moveChannels(int delta) {
    QList<int> rows = selectedRows();
    if (rows.isEmpty()) return;
    if (rows.first() + delta < 0 || rows.last() + delta >= k5::ChannelCount) return;
    if (delta > 0) std::reverse(rows.begin(), rows.end());
    for (int r : rows) m_model->swapChannels(r, r + delta);
    m_table->clearSelection();
    QItemSelection selection;
    for (int r : rows) {
        const QModelIndex i = m_filter->mapFromSource(m_model->index(r + delta, 0));
        if (i.isValid()) selection.select(i, i);
    }
    m_table->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    const QModelIndex cur = m_filter->mapFromSource(m_model->index(rows.last() + delta, qMax(0, m_table->currentIndex().column())));
    if (cur.isValid()) m_table->selectionModel()->setCurrentIndex(cur, QItemSelectionModel::NoUpdate);
}

void MainWindow::fixBandMarkers() {
    const int n = m_model->fixBandMarkers();
    notify(tr("Corrected the band marker of %1. Write to the radio to apply.").arg(counted(n, "channel", "channels")));
}

QString MainWindow::userListsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/k5editor/lists");
}

void MainWindow::importPresets(const QStringList &preselect) {
    if (m_work.size() != k5::MemSize || (m_fromRadio && !k5::firmwareSupported(m_firmware))) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add ready-made channel lists"));
    dialog.resize(680, 640);

    auto *list = new QListWidget;
    auto addItem = [list](const k5::PresetGroup &g, bool checked) {
        auto *item = new QListWidgetItem(tr("%1  —  %2").arg(g.name, counted(int(g.channels.size()), "channel", "channels")));
        item->setToolTip(g.note);
        item->setData(Qt::UserRole, g.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        list->addItem(item);
    };
    for (const k5::PresetGroup &g : k5::presetGroups()) addItem(g, preselect.contains(g.id) || preselect.contains(QStringLiteral("all")));
    auto *openFolder = new QPushButton(tr("Open my lists folder"));
    openFolder->setToolTip(tr("Your own lists live in %1 as CSV files. A file named like a shipped list replaces it.").arg(userListsDir()));
    auto *reload = new QPushButton(tr("Reload"));
    reload->setToolTip(tr("Read the list files again after you change them"));
    auto *fileProblems = new QPushButton;
    fileProblems->setFlat(true);
    auto showFileProblems = [fileProblems] {
        const int n = int(k5::presetProblems().size());
        fileProblems->setVisible(n > 0);
        fileProblems->setText(tr("%1 in the list files…").arg(counted(n, "problem", "problems")));
    };
    showFileProblems();
    connect(fileProblems, &QPushButton::clicked, &dialog, [&dialog] {
        QMessageBox box(QMessageBox::Information, tr("List files"), tr("Some rows of the list files were left out or changed."), QMessageBox::Ok, &dialog);
        box.setDetailedText(k5::presetProblems().join(QLatin1Char('\n')));
        box.exec();
    });
    connect(openFolder, &QPushButton::clicked, &dialog, [this] {
        const QDir dir(userListsDir());
        dir.mkpath(QStringLiteral("."));
        const QString shipped = k5::presetDirs().value(0);
        if (!QFile::exists(dir.filePath("README.md")) && QFile::exists(shipped + "/README.md")) QFile::copy(shipped + "/README.md", dir.filePath("README.md"));
        QFile where(dir.filePath("SHIPPED-LISTS.txt"));
        if (where.open(QIODevice::WriteOnly)) where.write(tr("The lists that come with the program are in:\n%1\nCopy one here to change it. See README.md for the format.\n").arg(shipped).toUtf8());
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()))) notify(tr("Your lists folder is %1").arg(dir.absolutePath()));
    });
    auto *note = new QLabel;
    note->setWordWrap(true);
    note->setMinimumHeight(note->fontMetrics().lineSpacing() * 2);

    int firstFree = 0;
    while (firstFree < k5::ChannelCount - 1 && !k5::decodeChannel(m_work, firstFree).empty) ++firstFree;
    auto *start = new QSpinBox;
    start->setRange(1, k5::ChannelCount);
    start->setValue(firstFree + 1);
    auto *scanList = new QComboBox;
    scanList->addItems({tr("no scan list"), tr("scan list 1"), tr("scan list 2"), tr("scan list 3")});
    auto *airLock = new QCheckBox(tr("Switch on TX lock for the 118–137 MHz channels, so the radio never transmits on them"));
    airLock->setChecked(true);
    auto *summary = new QLabel;
    auto *room = new QProgressBar;
    room->setTextVisible(true);
    auto *warning = new QLabel;
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("color: #c0392b; font-weight: bold;"));
    auto *legal = new QLabel(tr("Channels are added with transmit switched on at the listed frequency. Transmitting on many of "
                                "these bands needs a licence or an approved radio; check what applies where you are."));
    legal->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *addButton = buttons->addButton(tr("Add"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto chosen = [&] {
        QList<const k5::PresetGroup *> groups;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Checked)
                if (const k5::PresetGroup *g = k5::findPresetGroup(list->item(i)->data(Qt::UserRole).toString())) groups << g;
        return groups;
    };
    auto refresh = [&] {
        int wanted = 0;
        bool air = false;
        for (const k5::PresetGroup *g : chosen()) {
            wanted += int(g->channels.size());
            air = air || k5::presetUsesAirband(*g);
        }
        const int freeAll = k5::freeSlotsFrom(m_work, 0);
        const int free = k5::freeSlotsFrom(m_work, start->value() - 1);
        const bool fits = wanted <= free;

        // the bar shows the radio's channel memory: what is used now plus what is ticked
        room->setRange(0, k5::ChannelCount);
        room->setValue(qMin(k5::ChannelCount, k5::ChannelCount - freeAll + wanted));
        room->setFormat(tr("%1 in use + %2 selected of %3").arg(k5::ChannelCount - freeAll).arg(wanted).arg(k5::ChannelCount));
        room->setStyleSheet(fits ? QString() : QStringLiteral("QProgressBar { text-align: center; } QProgressBar::chunk { background: #c0392b; }"));

        QStringList lines, warnings;
        QString channelLine = tr("Channels: %1 selected, %2 free").arg(wanted).arg(free);
        if (start->value() > 1 && free != freeAll) channelLine += tr(" from slot %1 on (%2 free in all)").arg(start->value()).arg(freeAll);
        if (fits && wanted) channelLine += tr(", %1 left afterwards").arg(free - wanted);
        lines << channelLine;
        if (!fits) warnings << tr("Not enough room: %1 too many. Untick some lists%2.").arg(counted(wanted - free, "channel", "channels"),
                                                                                       free != freeAll ? tr(" or start at a lower slot") : QString());
        if (wanted == 0) lines << tr("Tick the lists to add. Channels already in the radio are not touched.");
        summary->setText(lines.join(QLatin1Char('\n')));
        warning->setText(warnings.join(QLatin1Char('\n')));
        warning->setVisible(!warnings.isEmpty());
        addButton->setEnabled(wanted > 0 && fits);
        airLock->setVisible(air);
    };
    connect(list, &QListWidget::itemChanged, &dialog, refresh);
    connect(start, &QSpinBox::valueChanged, &dialog, refresh);
    connect(list, &QListWidget::currentItemChanged, &dialog, [note](QListWidgetItem *item) { note->setText(item ? item->toolTip() : QString()); });
    list->setCurrentRow(0);
    refresh();
    connect(reload, &QPushButton::clicked, &dialog, [&] {
        k5::setPresetDirs(k5::presetDirs());
        list->clear();
        for (const k5::PresetGroup &g : k5::presetGroups()) addItem(g, false);
        list->setCurrentRow(0);
        showFileProblems();
        refresh();
    });

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Start at slot")));
    row->addWidget(start);
    row->addSpacing(16);
    row->addWidget(new QLabel(tr("Put them in")));
    row->addWidget(scanList);
    row->addStretch(1);
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(openFolder);
    folderRow->addWidget(reload);
    folderRow->addWidget(fileProblems);
    folderRow->addStretch(1);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(list, 1);
    layout->addWidget(note);
    layout->addLayout(folderRow);
    layout->addLayout(row);
    layout->addWidget(airLock);
    layout->addWidget(room);
    layout->addWidget(summary);
    layout->addWidget(warning);
    layout->addWidget(legal);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    QByteArray image = m_work;      // all lists or none
    k5::ImportOptions options;
    options.firstSlot = start->value() - 1;
    options.scanList = scanList->currentIndex();
    options.lockAirband = airLock->isChecked();
    int added = 0, first = -1;
    bool air = false;
    QStringList names;
    for (const k5::PresetGroup *g : chosen()) {
        const k5::ImportResult r = k5::importPreset(image, *g, options);
        if (!r.ok) {
            notify(tr("Nothing was added. %1").arg(r.error), true);
            return;
        }
        added += int(r.placed.size());
        if (first < 0) first = r.placed.first();
        names << g->name;
        air = air || k5::presetUsesAirband(*g);
    }
    QString lockNote;
    if (air) lockNote = options.lockAirband ? tr(" The 118–137 MHz channels have TX lock on.") : tr(" Do not transmit on the air band channels.");
    m_work = image;
    m_model->reload();
    updateState();
    if (first >= 0) {
        const QModelIndex at = m_filter->mapFromSource(m_model->index(first, ChannelModel::Name));
        if (at.isValid()) m_table->scrollTo(at, QAbstractItemView::PositionAtTop);
    }
    notify(tr("Added %1 (%2), starting at slot %3. They are on the radio only after you write.")
               .arg(counted(added, "channel", "channels"), names.join(QStringLiteral(", "))).arg(first + 1) + lockNote);
}
