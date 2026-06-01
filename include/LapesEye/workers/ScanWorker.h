#pragma once
// ScanWorker.h — asynchroniczny skan folderu (v0.2)
// Na razie FileScanner::scan_sync() wystarczy dla małych folderów
#include <QObject>
#include <QString>
namespace LapesEye {
class ScanWorker : public QObject {
    Q_OBJECT
public:
    explicit ScanWorker(QObject* p = nullptr) : QObject(p) {}
    // TODO v0.2
};
} // namespace LapesEye
