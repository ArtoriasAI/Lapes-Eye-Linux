#include "LapesEye/core/MetaStore.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QRegularExpression>

// libexiv2
#include <exiv2/exiv2.hpp>

namespace LapesEye {

// ─── helpers ────────────────────────────────────────────────────────────────

QString MetaStore::leye_path(const QString& path) {
    return path + ".leye";
}

QString MetaStore::color_label_name(ColorLabel c) {
    switch (c) {
        case ColorLabel::Red:    return "red";
        case ColorLabel::Yellow: return "yellow";
        case ColorLabel::Green:  return "green";
        case ColorLabel::Blue:   return "blue";
        case ColorLabel::Purple: return "purple";
        default:                 return "";
    }
}

ColorLabel MetaStore::color_label_from_name(const QString& name) {
    if (name == "red")    return ColorLabel::Red;
    if (name == "yellow") return ColorLabel::Yellow;
    if (name == "green")  return ColorLabel::Green;
    if (name == "blue")   return ColorLabel::Blue;
    if (name == "purple") return ColorLabel::Purple;
    return ColorLabel::None;
}

QString MetaStore::pick_flag_name(PickFlag f) {
    switch (f) {
        case PickFlag::Pick:   return "pick";
        case PickFlag::Reject: return "reject";
        default:               return "";
    }
}

// ─── EXIF (libexiv2) ─────────────────────────────────────────────────────────

ExifData MetaStore::read_exif(const QString& path) {
    ExifData out;
    try {
        auto img = Exiv2::ImageFactory::open(path.toStdString());
        img->readMetadata();
        auto& exif = img->exifData();

        auto get_str = [&](const char* key) -> QString {
            auto it = exif.findKey(Exiv2::ExifKey(key));
            if (it == exif.end()) return {};
            return QString::fromStdString(it->toString());
        };
        auto get_rat = [&](const char* key) -> double {
            auto it = exif.findKey(Exiv2::ExifKey(key));
            if (it == exif.end()) return 0.0;
            return it->toFloat();
        };
        auto get_int = [&](const char* key) -> int {
            auto it = exif.findKey(Exiv2::ExifKey(key));
            if (it == exif.end()) return 0;
            // Exiv2 API różni się między wersjami:
            // < 0.28: toLong() | >= 0.28: toInt64() lub value().toInt64()
            #if EXIV2_VERSION >= EXIV2_MAKE_VERSION(0,28,0)
            return static_cast<int>(it->toInt64());
            #else
            return static_cast<int>(it->toLong());
            #endif
        };

        out.camera_make   = get_str("Exif.Image.Make");
        out.camera_model  = get_str("Exif.Image.Model");
        out.lens          = get_str("Exif.Photo.LensModel");
        if (out.lens.isEmpty())
            out.lens      = get_str("Exif.Image.LensModel");
        out.date_taken    = get_str("Exif.Photo.DateTimeOriginal");
        out.exposure_time = get_rat("Exif.Photo.ExposureTime");
        out.fnumber       = get_rat("Exif.Photo.FNumber");
        out.iso           = get_int("Exif.Photo.ISOSpeedRatings");
        out.focal_length  = get_rat("Exif.Photo.FocalLength");
        out.width         = get_int("Exif.Photo.PixelXDimension");
        out.height        = get_int("Exif.Photo.PixelYDimension");

        // Kompensacja ekspozycji
        out.exposure_bias = get_rat("Exif.Photo.ExposureBiasValue");

        // Przestrzeń kolorów
        int cs = get_int("Exif.Photo.ColorSpace");
        if      (cs == 1)      out.color_space = "sRGB";
        else if (cs == 2)      out.color_space = "Adobe RGB";
        else if (cs == 0xFFFF) out.color_space = "Uncalibrated";
        else if (cs > 0)       out.color_space = QString("CS %1").arg(cs);

        // Głębia bitowa
        out.bit_depth = get_int("Exif.Image.BitsPerSample");
        if (out.bit_depth == 0)
            out.bit_depth = get_int("Exif.Photo.BitsPerSample");

        // Balans bieli
        int wb = get_int("Exif.Photo.WhiteBalance");
        if      (wb == 0) out.white_balance = "Auto";
        else if (wb == 1) out.white_balance = "Manual";
        // Szczegółowy balans bieli (np. Daylight, Cloudy itp.)
        QString wb_detail = get_str("Exif.Photo.LightSource");
        if (!wb_detail.isEmpty() && wb_detail != "0")
            out.white_balance += wb_detail.isEmpty() ? "" : " (" + wb_detail + ")";

        // GPS
        auto lat_it = exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitude"));
        if (lat_it != exif.end()) {
            out.gps_lat = lat_it->toFloat();
            auto lon_it = exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLongitude"));
            if (lon_it != exif.end()) {
                out.gps_lon = lon_it->toFloat();
                out.has_gps = true;
            }
        }
    } catch (...) {
        // Plik bez EXIF (PNG, RAW bez danych, etc.) — zwróć pusty struct
    }
    return out;
}

// ─── Katalogi metadanych w folderze programu ──────────────────────────────────
// Lokalizacja: ~/.local/share/lapes-eye/catalog/
// Nazwa pliku: MD5 ścieżki folderu + ".json"
// Np: /run/media/tomek/Tomka/Zdjecia/Biedronki
//  →  ~/.local/share/lapes-eye/catalog/a3f8c2d1e4b5f6a7.json
// Zawartość: {"DSC001.jpg": {"rating":5,"flag":"pick",...}, ...}

static QString catalog_base_dir() {
    // Użyj QStandardPaths dla przenośności
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    // AppLocalDataLocation = ~/.local/share/lapes-eye (Qt używa applicationName)
    return base + "/catalog";
}

QString MetaStore::catalog_path(const QString& file_path) {
    QString folder = QFileInfo(file_path).dir().absolutePath();
    // Hashuj ścieżkę folderu — unikalna nazwa pliku JSON per folder
    quint32 hash = qHash(folder);
    QString hash_str = QString::number(hash, 16).rightJustified(8, '0');
    // Dodaj suffix z ostatniego członu ścieżki dla czytelności
    QString dir_name = QFileInfo(folder).fileName();
    // Usuń znaki niebezpieczne dla systemu plików
    dir_name.replace(QRegularExpression("[^a-zA-Z0-9_\\-ąćęłńóśźżĄĆĘŁŃÓŚŹŻ ]"), "_");
    dir_name = dir_name.left(30);  // max 30 znaków
    QString filename = QString("%1_%2.json").arg(hash_str, dir_name);
    QString dir = catalog_base_dir();
    QDir().mkpath(dir);  // utwórz katalog jeśli nie istnieje
    return dir + "/" + filename;
}

static QJsonObject read_catalog(const QString& catalog_path) {
    QFile f(catalog_path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    auto doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

static bool write_catalog(const QString& catalog_path, const QJsonObject& catalog) {
    QFile f(catalog_path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(catalog).toJson());
    return true;
}

static void apply_meta_from_json(FileMetadata& meta, const QJsonObject& obj) {
    meta.rating      = obj["rating"].toInt(0);
    meta.rotation    = obj["rotation"].toInt(0);
    meta.color_label = MetaStore::color_label_from_name(obj["color_label"].toString());
    meta.note        = obj["note"].toString();
    meta.lape_edits  = obj["lape_edits"].toObject();
    auto flag_str = obj["flag"].toString();
    if (flag_str == "pick")   meta.pick_flag = PickFlag::Pick;
    if (flag_str == "reject") meta.pick_flag = PickFlag::Reject;
    auto kw_arr = obj["keywords"].toArray();
    for (const auto& k : kw_arr) meta.keywords << k.toString();
}

static QJsonObject meta_to_json(const FileMetadata& meta) {
    QJsonObject obj;
    obj["rating"]      = meta.rating;
    if (meta.rotation != 0) obj["rotation"] = meta.rotation;
    obj["color_label"] = MetaStore::color_label_name(meta.color_label);
    obj["flag"]        = MetaStore::pick_flag_name(meta.pick_flag);
    obj["note"]        = meta.note;
    obj["lape_edits"]  = meta.lape_edits;
    QJsonArray kw;
    for (const auto& k : meta.keywords) kw.append(k);
    obj["keywords"] = kw;
    return obj;
}

// ─── .leye load/save ────────────────────────────────────────────────────────

void MetaStore::load_exif(FileMetadata& meta) {
    if (meta.loaded_exif) return;
    meta.exif = read_exif(meta.path);
    meta.loaded_exif = true;
}

FileMetadata MetaStore::load(const QString& path) {
    FileMetadata meta;
    meta.path = path;
    // LAZY EXIF — nie ładuj przy load(), tylko gdy MetaPanel potrzebuje
    // Wywołaj load_exif(meta) osobno gdy chcesz EXIF
    meta.loaded_exif = false;

    QString filename = QFileInfo(path).fileName();
    QString cat_path = catalog_path(path);

    // 1. Sprawdź nowy katalog zbiorczy
    QJsonObject catalog = read_catalog(cat_path);
    if (catalog.contains(filename)) {
        apply_meta_from_json(meta, catalog[filename].toObject());
        meta.loaded_leye = true;

        // Migracja: usuń stary .leye jeśli istnieje
        QString old_leye = path + ".leye";
        if (QFileInfo::exists(old_leye)) QFile::remove(old_leye);
        return meta;
    }

    // 2. Fallback: stary .leye per plik (migracja)
    QString old_leye = path + ".leye";
    QFile f(old_leye);
    if (f.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) {
            apply_meta_from_json(meta, doc.object());
            meta.loaded_leye = true;
        }
        f.close();
        // Migruj do katalogu zbiorczego
        catalog[filename] = meta_to_json(meta);
        write_catalog(cat_path, catalog);
        QFile::remove(old_leye);  // usuń stary plik
    }
    return meta;
}

bool MetaStore::save(const FileMetadata& meta) {
    QString filename = QFileInfo(meta.path).fileName();
    QString cat_path = catalog_path(meta.path);

    // Załaduj istniejący katalog i zaktualizuj wpis dla tego pliku
    QJsonObject catalog = read_catalog(cat_path);
    QJsonObject entry = meta_to_json(meta);

    // Nie zapisuj pustego wpisu (domyślne wartości) — oszczędza miejsce
    bool has_data = meta.rating != 0
                 || meta.rotation != 0
                 || meta.color_label != ColorLabel::None
                 || meta.pick_flag != PickFlag::None
                 || !meta.note.isEmpty()
                 || !meta.keywords.isEmpty()
                 || !meta.lape_edits.isEmpty();
    if (has_data)
        catalog[filename] = entry;
    else
        catalog.remove(filename);  // usuń pusty wpis

    if (catalog.isEmpty()) {
        // Katalog pusty — usuń plik żeby nie zaśmiecać
        QFile::remove(cat_path);
        return true;
    }
    return write_catalog(cat_path, catalog);
}

int MetaStore::read_rating(const QString& path) {
    QString filename = QFileInfo(path).fileName();
    QJsonObject catalog = read_catalog(catalog_path(path));
    if (catalog.contains(filename))
        return catalog[filename].toObject()["rating"].toInt(0);
    // Fallback: stary .leye
    QFile f(path + ".leye");
    if (!f.open(QIODevice::ReadOnly)) return 0;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return 0;
    return doc.object()["rating"].toInt(0);
}

} // namespace LapesEye
