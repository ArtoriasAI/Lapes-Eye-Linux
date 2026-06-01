#pragma once
// Collection.h — kolekcje statyczne i smart (v0.2)
// Na razie stub — struktura danych gotowa do implementacji

#include <QString>
#include <QStringList>
#include "LapesEye/core/MetaStore.h"

namespace LapesEye {

enum class CollectionType { Static, Smart };

struct SmartCriteria {
    int        min_rating    = 0;
    ColorLabel color_label   = ColorLabel::None;
    QString    keyword;
    QString    folder_path;
};

struct Collection {
    QString         id;       // UUID
    QString         name;
    CollectionType  type;

    // Static
    QStringList     static_paths;

    // Smart
    SmartCriteria   criteria;
};

// Persystencja kolekcji: ~/.config/lape/bridge/collections.json
class CollectionStore {
public:
    static QList<Collection> load_all();
    static bool save_all(const QList<Collection>& cols);
    static QStringList resolve_smart(const Collection& col,
                                      const QString& root_dir);
};

} // namespace LapesEye
