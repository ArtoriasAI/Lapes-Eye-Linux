#pragma once
// MetaStore.h — metadane pliku: EXIF (libexiv2) + własne tagi (.leye JSON)
//
// Plik .leye siedzi obok oryginału: foto.jpg → foto.jpg.leye
// Format .leye (JSON):
// {
//   "rating": 3,
//   "color_label": "green",
//   "keywords": ["pejzaż", "jesień"],
//   "flag": "pick",           // "pick" | "reject" | ""
//   "note": "...",
//   "lape_edits": { ... }    // zarezerwowane dla ustawień edycji Lape
// }

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QJsonObject>
#include <optional>

namespace LapesEye {

// Kolor etykiety — identyczny z Lape Bridge konwencją Adobe
enum class ColorLabel {
    None,
    Red,
    Yellow,
    Green,
    Blue,
    Purple
};

enum class PickFlag {
    None,
    Pick,
    Reject
};

struct ExifData {
    QString camera_make;
    QString camera_model;
    QString lens;
    QString date_taken;            // ISO 8601
    double  exposure_time  = 0;    // sekundy
    double  fnumber        = 0;
    int     iso            = 0;
    double  focal_length   = 0;
    int     width          = 0;
    int     height         = 0;
    // Nowe pola
    double  exposure_bias  = 0;    // kompensacja ekspozycji EV
    QString color_space;           // sRGB, AdobeRGB itp.
    int     bit_depth      = 0;    // głębia bitowa (8/12/14/16)
    QString white_balance;         // Auto, Daylight, Cloudy itp.
    // GPS
    double  gps_lat        = 0;
    double  gps_lon        = 0;
    bool    has_gps        = false;
};

struct FileMetadata {
    QString     path;

    // EXIF (read-only, z pliku)
    ExifData    exif;

    // Edytowalne — zapisywane do .leye
    int         rating      = 0;       // 0–5
    ColorLabel  color_label = ColorLabel::None;
    PickFlag    pick_flag   = PickFlag::None;
    QStringList keywords;
    QString     note;

    // Zarezerwowane dla przyszłej integracji z ACR w Lape
    int         rotation    = 0;  // 0/90/180/270 — obrót zapisywany w .leye
    QJsonObject lape_edits;

    bool        loaded_exif  = false;
    bool        loaded_leye  = false;
};

class MetaStore {
public:
    // Ładuje EXIF z pliku (libexiv2)
    static ExifData read_exif(const QString& path);
    static void     load_exif(FileMetadata& meta);  // lazy EXIF — wywołaj tylko gdy MetaPanel widoczny

    // Ładuje .leye (rating, labels, keywords)
    static FileMetadata load(const QString& path);

    // Zapisuje część edytowalną do .leye
    static bool save(const FileMetadata& meta);

    // Szybki odczyt tylko ratingu (dla miniatur)
    static int  read_rating(const QString& path);

    // Ścieżka do pliku .leye dla danego pliku
    static QString leye_path(const QString& path);      // stary format (migracja)
    static QString catalog_path(const QString& file_path); // nowy format: jeden plik per folder

    // Konwersje
    static QString color_label_name(ColorLabel c);
    static ColorLabel color_label_from_name(const QString& name);
    static QString pick_flag_name(PickFlag f);
};

} // namespace LapesEye
