#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QSet>

namespace LapesEye {

struct ScannedFile {
    QString  path;
    QString  name;
    qint64   size      = 0;
    qint64   mtime     = 0;
    int      width     = 0;
    int      height    = 0;
    bool     is_raw    = false;
    bool     is_psd    = false;
    bool     is_dir    = false;   // ← NOWE: katalog
    bool     has_leye  = false;
};

class FileScanner : public QObject {
    Q_OBJECT
public:
    explicit FileScanner(QObject* parent = nullptr);

    static const QStringList& supported_extensions();
    static bool is_supported(const QString& path);
    static bool is_raw(const QString& path);
    static bool is_psd(const QString& path);

    // Skanuj: najpierw foldery (posortowane), potem pliki (posortowane)
    QList<ScannedFile> scan_sync(const QString& dir_path, bool recursive = false);

signals:
    void file_found(const ScannedFile& file);
    void scan_finished(int total);

private:
    static ScannedFile make_file_entry(const QFileInfo& fi);
    static ScannedFile make_dir_entry(const QFileInfo& fi);
};

} // namespace LapesEye
