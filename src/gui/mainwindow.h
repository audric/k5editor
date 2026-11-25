#pragma once
#include "sync.h"
#include <QMainWindow>
#include <QThread>

class ChannelModel;
class ChannelFilter;
class SettingsPanel;
class QComboBox;
class QLabel;
class QTableView;
class QProgressDialog;

// One conversation with the radio, run off the GUI thread.
class RadioJob : public QThread {
    Q_OBJECT
public:
    enum Kind { Read, Write };
    Kind kind = Read;
    QString port;
    QByteArray base, work;           // Write only
    k5::WriteOptions options;        // Write only
    // results
    QString error;
    QString firmware;
    QByteArray image;
    k5::WriteResult result;

signals:
    void progress(const QString &stage, int done, int total);

protected:
    void run() override;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    bool openFile(const QString &path);
    void showTab(int index);   // used by the screenshot option
    void newFile();            // empty image: no channels, default settings
    void importPresets(const QStringList &preselect = {});
    static QString userListsDir();   // where the user keeps their own channel lists

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void refreshPorts();
    void readFromRadio();
    void writeToRadio(bool overwriteConflicts = false);
    void startWrite(const QByteArray &base, bool overwriteConflicts);
    void openFileDialog();
    void saveFileDialog();
    void revert();
    void setImage(const QByteArray &image, const QString &firmware, const QString &source);
    void updateState();
    void notify(const QString &text, bool warning = false);
    bool confirmDiscard();
    bool isDirty() const { return m_work != m_base; }
    QString backupDir() const;
    void runJob(RadioJob *job, const QString &title, std::function<void(RadioJob *)> done);
    // channel actions
    QList<int> selectedRows() const;
    void copyChannels();
    void pasteChannels();
    void deleteChannels();
    void moveChannels(int delta);
    void fixBandMarkers();

    QByteArray m_base, m_work;
    QString m_firmware;              // empty when the image did not come from a radio
    QString m_source;
    bool m_fromRadio = false;
    QList<k5::Channel> m_clipboard;

    ChannelModel *m_model = nullptr;
    ChannelFilter *m_filter = nullptr;
    QTableView *m_table = nullptr;
    QList<SettingsPanel *> m_panels;
    QComboBox *m_ports = nullptr;
    QLabel *m_banner = nullptr;
    QLabel *m_status = nullptr;
    QAction *m_readAct = nullptr, *m_writeAct = nullptr, *m_saveAct = nullptr, *m_revertAct = nullptr, *m_fixAct = nullptr, *m_importAct = nullptr;
    QWidget *m_tabs = nullptr;
};
