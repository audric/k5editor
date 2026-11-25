#include "gui/mainwindow.h"
#include "presets.h"
#include "serialtransport.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

// k5editor                         start the editor
// k5editor <image.bin>             open an image file
// k5editor --dump <port> <file>    read the radio's memory to a file, no window
int main(int argc, char **argv) {
    QTextStream out(stdout), err(stderr);
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--dump")) {
        try {
            k5::SerialTransport transport(QString::fromLocal8Bit(argv[2]));
            k5::Radio radio(transport);
            out << "firmware: " << radio.hello() << Qt::endl;
            const QByteArray image = radio.readAll();
            QFile f(QString::fromLocal8Bit(argv[3]));
            if (!f.open(QIODevice::WriteOnly) || f.write(image) != image.size()) {
                err << "cannot write " << argv[3] << Qt::endl;
                return 1;
            }
            out << "read " << image.size() << " bytes" << Qt::endl;
            return 0;
        } catch (const std::exception &e) {
            err << e.what() << Qt::endl;
            return 1;
        }
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("k5editor"));
    QCoreApplication::setApplicationName(QStringLiteral("k5editor"));
    {
        // Shipped lists: next to an installed binary, else the source tree this was built from.
        QString shipped = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../" K5_LISTS_INSTALL_DIR));
        if (!QDir(shipped).exists()) shipped = QStringLiteral(K5_LISTS_SOURCE_DIR);
        k5::setPresetDirs({QDir::cleanPath(shipped), MainWindow::userListsDir()});
    }
    MainWindow window;
    window.show();
    const QStringList args = app.arguments();
    QString screenshot;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--screenshot") && i + 1 < args.size()) screenshot = args.at(++i);   // for testing
        else if (args.at(i) == QLatin1String("--tab") && i + 1 < args.size()) window.showTab(args.at(++i).toInt());      // for testing
        else if (args.at(i) == QLatin1String("--new")) QTimer::singleShot(0, &window, [&window] { window.newFile(); });   // for testing
        else if (args.at(i) == QLatin1String("--import")) QTimer::singleShot(100, &window, [&window] { window.importPresets(qEnvironmentVariable("K5_IMPORT_TICK").split(',', Qt::SkipEmptyParts)); });   // for testing
        else window.openFile(args.at(i));
    }
    if (!screenshot.isEmpty())
        QTimer::singleShot(400, &window, [&] {
            QWidget *modal = QApplication::activeModalWidget();
            (modal ? modal : &window)->grab().save(screenshot);
            if (modal) modal->close();
            app.quit();
        });
    return app.exec();
}
