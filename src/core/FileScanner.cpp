#include "LapesEye/core/FileScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QImageReader>
#include <QCollator>

namespace LapesEye {

namespace {
const QStringList RAW_EXT = {
    "cr2","cr3","nef","nrw","arw","srf","sr2",
    "raf","rw2","orf","pef","ptx","dng","raw",
    "rwl","x3f","3fr","fff","iiq"
};
const QStringList RASTER_EXT = {
    "jpg","jpeg","png","tif","tiff","bmp",
    "webp","avif","heic","gif"
};
const QStringList PSD_EXT = {"psd","psb"};
} // anon

const QStringList& FileScanner::supported_extensions() {
    static QStringList all = []() {
        QStringList out;
        out << RASTER_EXT << RAW_EXT << PSD_EXT;
        return out;
    }();
    return all;
}

bool FileScanner::is_supported(const QString& path) {
    return supported_extensions().contains(QFileInfo(path).suffix().toLower());
}
bool FileScanner::is_raw(const QString& path) {
    return RAW_EXT.contains(QFileInfo(path).suffix().toLower());
}
bool FileScanner::is_psd(const QString& path) {
    return PSD_EXT.contains(QFileInfo(path).suffix().toLower());
}

FileScanner::FileScanner(QObject* parent) : QObject(parent) {}

ScannedFile FileScanner::make_dir_entry(const QFileInfo& fi) {
    ScannedFile f;
    f.path   = fi.absoluteFilePath();
    f.name   = fi.fileName();
    f.mtime  = fi.lastModified().toSecsSinceEpoch();
    f.is_dir = true;
    return f;
}

ScannedFile FileScanner::make_file_entry(const QFileInfo& fi) {
    ScannedFile f;
    f.path   = fi.absoluteFilePath();
    f.name   = fi.fileName();
    f.size   = fi.size();
    f.mtime  = fi.lastModified().toSecsSinceEpoch();
    f.is_raw = is_raw(f.path);
    f.is_psd = is_psd(f.path);
    f.has_leye = QFileInfo::exists(f.path + ".leye");

    // Rozmiar tylko dla rastrowych (szybko, bez decode)
    if (!f.is_raw && !f.is_psd) {
        QImageReader reader(f.path);
        auto sz = reader.size();
        f.width  = sz.width();
        f.height = sz.height();
    }
    return f;
}

QList<ScannedFile> FileScanner::scan_sync(const QString& dir_path, bool recursive) {
    QList<ScannedFile> dirs, files;

    QDir dir(dir_path);

    // Foldery (bez . i ..)
    const auto subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                            QDir::Name | QDir::IgnoreCase);
    for (const auto& fi : subdirs)
        dirs.append(make_dir_entry(fi));

    // Pliki graficzne
    QStringList filters;
    for (const auto& ext : supported_extensions())
        filters << ("*." + ext) << ("*." + ext.toUpper());

    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (recursive) flags |= QDirIterator::Subdirectories;

    QDirIterator it(dir_path, filters, QDir::Files, flags);
    while (it.hasNext()) {
        it.next();
        files.append(make_file_entry(it.fileInfo()));
    }

    // Numeryczne sortowanie: 1,2,3,10,11 zamiast 1,10,11,2
    QCollator col;
    col.setNumericMode(true);
    col.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&col](const ScannedFile& a, const ScannedFile& b) {
        return col.compare(a.name, b.name) < 0;
    });

    // Foldery pierwsze, potem pliki
    return dirs + files;
}

} // namespace LapesEye
