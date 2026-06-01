#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include "LapesEye/core/Collection.h"

namespace LapesEye {

// Dialog tworzenia lub edycji kolekcji statycznej
class CollectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit CollectionDialog(QWidget* parent = nullptr,
                              const Collection* existing = nullptr);

    Collection result() const;

    // Statyczna metoda — dodaj zaznaczone pliki do nowej/istniejącej kolekcji
    static void add_files_to_collection(const QStringList& paths, QWidget* parent);

private:
    void update_count();

    QLineEdit*   m_name_edit  = nullptr;
    QListWidget* m_file_list  = nullptr;
    QLabel*      m_count_lbl  = nullptr;
    QString      m_id;  // istniejące ID lub puste dla nowej
};

} // namespace LapesEye
