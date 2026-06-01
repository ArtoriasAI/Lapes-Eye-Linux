#include "LapesEye/core/Collection.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QDirIterator>

namespace LapesEye {

static QString collections_path() {
    QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(cfg);
    return cfg + "/collections.json";
}

QList<Collection> CollectionStore::load_all() {
    QList<Collection> result;
    QFile f(collections_path());
    if (!f.open(QIODevice::ReadOnly)) return result;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return result;
    for (const auto& val : doc.array()) {
        QJsonObject obj = val.toObject();
        Collection c;
        c.id   = obj["id"].toString();
        c.name = obj["name"].toString();
        c.type = obj["type"].toString() == "smart"
                 ? CollectionType::Smart : CollectionType::Static;
        if (c.type == CollectionType::Static) {
            for (const auto& p : obj["paths"].toArray())
                c.static_paths << p.toString();
        } else {
            QJsonObject cr = obj["criteria"].toObject();
            c.criteria.min_rating  = cr["min_rating"].toInt(0);
            c.criteria.keyword     = cr["keyword"].toString();
            c.criteria.folder_path = cr["folder_path"].toString();
        }
        if (!c.id.isEmpty() && !c.name.isEmpty())
            result << c;
    }
    return result;
}

bool CollectionStore::save_all(const QList<Collection>& cols) {
    QJsonArray arr;
    for (const auto& c : cols) {
        QJsonObject obj;
        obj["id"]   = c.id;
        obj["name"] = c.name;
        obj["type"] = c.type == CollectionType::Smart ? "smart" : "static";
        if (c.type == CollectionType::Static) {
            QJsonArray paths;
            for (const auto& p : c.static_paths) paths << p;
            obj["paths"] = paths;
        } else {
            QJsonObject cr;
            cr["min_rating"]  = c.criteria.min_rating;
            cr["keyword"]     = c.criteria.keyword;
            cr["folder_path"] = c.criteria.folder_path;
            obj["criteria"]   = cr;
        }
        arr << obj;
    }
    QFile f(collections_path());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(arr).toJson());
    return true;
}

QStringList CollectionStore::resolve_smart(const Collection& col,
                                            const QString& root_dir) {
    QStringList result;
    QString base = col.criteria.folder_path.isEmpty()
                   ? root_dir : col.criteria.folder_path;
    if (base.isEmpty()) return result;
    QDirIterator it(base, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        if (col.criteria.min_rating > 0) {
            auto meta = MetaStore::load(path);
            if (meta.rating < col.criteria.min_rating) continue;
        }
        result << path;
    }
    return result;
}

} // namespace LapesEye
