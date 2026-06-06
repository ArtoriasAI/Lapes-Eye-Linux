#include "LapesEye/ui/MainWindow.h"
#include "LapesEye/ui/FolderPanel.h"
#include "LapesEye/ui/ThumbnailGrid.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QDir>
#include <QIcon>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char* argv[]) {
#if LEYE_HAS_GL
    // Wymuś GLX (nie EGL) — OpenGL 4.5 Core Profile niedostępny przez EGL na X11
    if (!qEnvironmentVariableIsSet("QT_XCB_GL_INTEGRATION"))
        qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
    // Domyślny format GL dla wszystkich QOpenGLWidget
    QSurfaceFormat fmt;
    fmt.setVersion(4, 5);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);
#endif
    QApplication app(argc, argv);
    app.setApplicationName("Lape's Eye");
    app.setOrganizationName("Lape");
    app.setApplicationVersion("0.5.7");
    app.setDesktopFileName("lapes-eye");

    // Ikona aplikacji — wielorozdzielcza z QRC
    QIcon appIcon;
    appIcon.addFile(":/icons/lapes-eye-16.png",  QSize(16,16));
    appIcon.addFile(":/icons/lapes-eye-22.png",  QSize(22,22));
    appIcon.addFile(":/icons/lapes-eye-32.png",  QSize(32,32));
    appIcon.addFile(":/icons/lapes-eye-48.png",  QSize(48,48));
    appIcon.addFile(":/icons/lapes-eye-64.png",  QSize(64,64));
    appIcon.addFile(":/icons/lapes-eye-128.png", QSize(128,128));
    appIcon.addFile(":/icons/lapes-eye-256.png", QSize(256,256));
    app.setWindowIcon(appIcon);

    // Upewnij się że katalogi konfiguracyjne istnieją
    QString cfg = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir().mkpath(cfg + "/lape/bridge");
    QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache);

    LapesEye::MainWindow win;

    // Globalnie wyeliminuj focus rect z QScrollArea — KDE Breeze rysuje niebieską
    // ramkę na QScrollArea::viewport() gdy widget ma StrongFocus. Dotyczy to
    // ThumbnailGrid::m_scroll. Ustawiamy to po stworzeniu okna (po załadowaniu stylu).
    app.setStyleSheet(app.styleSheet() +
        "QScrollArea { border: none; outline: none; }"
        "QScrollArea:focus { border: none; outline: none; }"
        "QScrollArea > QWidget > QWidget { outline: none; }"
        "QAbstractScrollArea { border: none; outline: none; }"
        "QAbstractScrollArea:focus { border: none; outline: none; }"
    );

    win.show();

    // Obsługa argumentów:
    // - folder: otwórz bezpośrednio
    // - plik: otwórz folder zawierający + zaznacz plik
    // Dzięki temu "Otwórz w Lape's Eye" z menedżera plików działa dla obu
    const auto args = QApplication::arguments();
    if (args.size() > 1) {
        QString arg = args[1];
        QString startDir;
        QString selectFile;

        if (QDir(arg).exists()) {
            // To jest folder
            startDir = arg;
        } else if (QFileInfo(arg).isFile()) {
            // To jest plik — otwórz jego folder i zaznacz plik
            startDir  = QFileInfo(arg).absolutePath();
            selectFile = arg;
        }

        if (!startDir.isEmpty()) {
            QTimer::singleShot(0, &win, [&win, startDir, selectFile]() {
                win.open_folder(startDir);
                if (!selectFile.isEmpty()) {
                    // Zaznacz konkretny plik po otwarciu folderu
                    QTimer::singleShot(500, &win, [&win, selectFile]() {
                        win.select_file(selectFile);
                    });
                }
            });
        }
    }

    return app.exec();
}
