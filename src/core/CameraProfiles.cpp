#include "LapesEye/core/CameraProfiles.h"
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QSysInfo>

namespace LapesEye {

// ─── Ścieżki wyszukiwania profili DCP/ICC ────────────────────────────────────

static QStringList profile_search_dirs() {
    QStringList dirs;

#if defined(Q_OS_WIN)
    // Adobe DNG Converter / Camera Raw (Windows)
    dirs << "C:/Program Files/Adobe/Adobe DNG Converter/Resources/Profiles/Camera"
         << "C:/Program Files (x86)/Adobe/Adobe DNG Converter/Resources/Profiles/Camera"
         << QDir::homePath() + "/AppData/Roaming/Adobe/CameraRaw/CameraProfiles"
         << QDir::homePath() + "/AppData/Local/Adobe/CameraRaw/CameraProfiles";
    // RawTherapee (Windows)
    dirs << "C:/Program Files/RawTherapee/dcpprofiles"
         << "C:/Program Files/RawTherapee/iccprofiles/input";
    // darktable (Windows)
    dirs << "C:/Program Files/darktable/share/darktable/color/input";
#elif defined(Q_OS_LINUX)
    // Adobe Camera Raw przez Wine / DNG Converter
    dirs << QDir::homePath() + "/.adobe/CameraRaw/CameraProfiles"
         << QDir::homePath() + "/.wine/drive_c/Program Files/Adobe/Adobe DNG Converter/Resources/Profiles/Camera";
    // darktable (Linux)
    dirs << "/usr/share/darktable/color/input"
         << "/usr/local/share/darktable/color/input"
         << QDir::homePath() + "/.config/darktable/color/input"
         << QDir::homePath() + "/.local/share/darktable/color/input";
    // RawTherapee (Linux)
    dirs << "/usr/share/rawtherapee/dcpprofiles"
         << "/usr/local/share/rawtherapee/dcpprofiles"
         << QDir::homePath() + "/.config/RawTherapee/dcpprofiles";
    // Systemowe ICC
    dirs << "/usr/share/color/icc"
         << "/usr/local/share/color/icc";
#elif defined(Q_OS_MAC)
    dirs << "/Library/Application Support/Adobe/CameraRaw/CameraProfiles"
         << QDir::homePath() + "/Library/Application Support/Adobe/CameraRaw/CameraProfiles"
         << "/Applications/Adobe DNG Converter.app/Contents/Resources/Profiles/Camera"
         << "/usr/local/share/darktable/color/input";
#endif

    // Lape's Eye własny cache pobranych profili
    QString cache = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                    + "/profiles";
    dirs << cache;

    return dirs;
}

// ─── Generowanie wariantów nazwy pliku profilu ────────────────────────────────

static QStringList profile_candidates(const QString& camera_model) {
    // Normalizuj nazwę: "ILCE-7M3" → różne warianty
    QStringList candidates;
    QString model = camera_model.trimmed();

    // Bezpośrednie warianty
    candidates << model + ".dcp"
               << model + ".icc"
               << model + ".icm";

    // Z małymi literami
    candidates << model.toLower() + ".dcp"
               << model.toLower() + ".icc";

    // Zamień spacje na podkreślniki i myślniki
    QString underscored = model;
    underscored.replace(' ', '_');
    candidates << underscored + ".dcp"
               << underscored.toLower() + ".dcp";

    // Adobe DCP format: "Sony ILCE-7M3" → "Sony ILCE-7M3.dcp"
    // darktable format: "sony_ilce-7m3.icc"
    // Też bez myślników
    QString nohyphen = model;
    nohyphen.replace('-', "");
    candidates << nohyphen + ".dcp"
               << nohyphen.toLower() + ".icc";

    return candidates;
}

// ─── Główna funkcja wyszukiwania ──────────────────────────────────────────────

QString find_camera_profile(const QString& camera_make,
                             const QString& camera_model) {
    // Buduj listę nazw do szukania
    QStringList names;

    // "Sony ILCE-7M3" (make + model)
    if (!camera_make.isEmpty() && !camera_model.isEmpty())
        names << camera_make.trimmed() + " " + camera_model.trimmed();

    // Sam model
    if (!camera_model.isEmpty())
        names << camera_model.trimmed();

    // Szukaj w każdym katalogu
    for (const QString& dir_path : profile_search_dirs()) {
        QDir dir(dir_path);
        if (!dir.exists()) continue;

        for (const QString& name : names) {
            for (const QString& candidate : profile_candidates(name)) {
                QString full = dir.filePath(candidate);
                if (QFileInfo::exists(full)) {
                    qDebug() << "[Profile] Znaleziono:" << full;
                    return full;
                }
            }
        }

        // Szukaj rekurencyjnie jeden poziom głębiej (podfoldery producenta)
        for (const QString& subdir : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QDir sub(dir.filePath(subdir));
            for (const QString& name : names) {
                for (const QString& candidate : profile_candidates(name)) {
                    QString full = sub.filePath(candidate);
                    if (QFileInfo::exists(full)) {
                        qDebug() << "[Profile] Znaleziono (subdir):" << full;
                        return full;
                    }
                }
            }
        }
    }

    qDebug() << "[Profile] Brak profilu dla:" << camera_make << camera_model
             << "— używam macierzy wbudowanej";
    return {};
}

} // namespace LapesEye
