#include "LapesEye/ui/CollectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QUuid>
#include <QFileInfo>

namespace LapesEye {

CollectionDialog::CollectionDialog(QWidget* parent, const Collection* existing)
    : QDialog(parent)
{
    setWindowTitle(existing ? "Edytuj kolekcję" : "Nowa kolekcja");
    setMinimumSize(420, 380);

    auto* layout = new QVBoxLayout(this);

    // Nazwa
    auto* form = new QFormLayout();
    m_name_edit = new QLineEdit(this);
    m_name_edit->setPlaceholderText("np. Najlepsze 2026, Sesja Kowalski...");
    if (existing) m_name_edit->setText(existing->name);
    form->addRow("Nazwa:", m_name_edit);
    layout->addLayout(form);

    // Lista plików
    layout->addWidget(new QLabel("Pliki w kolekcji:", this));
    m_file_list = new QListWidget(this);
    m_file_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    if (existing) {
        for (const QString& p : existing->static_paths) {
            auto* item = new QListWidgetItem(QFileInfo(p).fileName(), m_file_list);
            item->setData(Qt::UserRole, p);
            item->setToolTip(p);
        }
    }
    layout->addWidget(m_file_list, 1);

    m_count_lbl = new QLabel(this);
    update_count();
    layout->addWidget(m_count_lbl);

    // Przyciski plików
    auto* file_btns = new QHBoxLayout();
    auto* add_btn = new QPushButton("+ Dodaj pliki", this);
    auto* rem_btn = new QPushButton("− Usuń zaznaczone", this);
    rem_btn->setEnabled(false);
    file_btns->addWidget(add_btn);
    file_btns->addWidget(rem_btn);
    file_btns->addStretch();
    layout->addLayout(file_btns);

    QObject::connect(add_btn, &QPushButton::clicked, this, [this]() {
        QStringList files = QFileDialog::getOpenFileNames(
            this, "Wybierz pliki do kolekcji", QString(),
            "Obrazy (*.jpg *.jpeg *.png *.tif *.tiff *.raw *.cr2 *.nef *.arw *.psd *.psb);;Wszystkie pliki (*)");
        for (const QString& p : files) {
            // Sprawdź duplikat
            bool dup = false;
            for (int i = 0; i < m_file_list->count(); ++i)
                if (m_file_list->item(i)->data(Qt::UserRole).toString() == p) { dup=true; break; }
            if (!dup) {
                auto* item = new QListWidgetItem(QFileInfo(p).fileName(), m_file_list);
                item->setData(Qt::UserRole, p);
                item->setToolTip(p);
            }
        }
        update_count();
    });

    QObject::connect(rem_btn, &QPushButton::clicked, this, [this]() {
        for (auto* item : m_file_list->selectedItems())
            delete item;
        update_count();
    });

    QObject::connect(m_file_list, &QListWidget::itemSelectionChanged, this,
                     [rem_btn, this]() {
                         rem_btn->setEnabled(!m_file_list->selectedItems().isEmpty());
                     });

    // OK / Anuluj
    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(btns, &QDialogButtonBox::accepted, this, [this]() {
        if (m_name_edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "Brak nazwy", "Podaj nazwę kolekcji.");
            return;
        }
        accept();
    });
    QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(btns);

    m_id = existing ? existing->id : QUuid::createUuid().toString(QUuid::WithoutBraces);
}

Collection CollectionDialog::result() const {
    Collection c;
    c.id   = m_id;
    c.name = m_name_edit->text().trimmed();
    c.type = CollectionType::Static;
    for (int i = 0; i < m_file_list->count(); ++i)
        c.static_paths << m_file_list->item(i)->data(Qt::UserRole).toString();
    return c;
}

void CollectionDialog::update_count() {
    m_count_lbl->setText(QString("Pliki: %1").arg(m_file_list->count()));
}

void CollectionDialog::add_files_to_collection(const QStringList& paths,
                                                QWidget* parent) {
    auto cols = CollectionStore::load_all();

    // Wybierz kolekcję lub utwórz nową
    QStringList names;
    names << "[ + Nowa kolekcja ]";
    for (const auto& c : cols) names << c.name;

    bool ok;
    QString chosen = QInputDialog::getItem(
        parent, "Dodaj do kolekcji",
        QString("Wybierz kolekcję dla %1 pliku/plików:").arg(paths.size()),
        names, 0, false, &ok);
    if (!ok) return;

    if (chosen == names.first()) {
        // Nowa kolekcja z wybranymi plikami
        Collection nc;
        nc.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
        nc.type = CollectionType::Static;
        nc.static_paths = paths;
        CollectionDialog dlg(parent, &nc);
        if (dlg.exec() == QDialog::Accepted) {
            cols << dlg.result();
            CollectionStore::save_all(cols);
        }
    } else {
        // Dodaj do istniejącej
        int idx = names.indexOf(chosen) - 1;
        if (idx >= 0 && idx < cols.size()) {
            for (const QString& p : paths)
                if (!cols[idx].static_paths.contains(p))
                    cols[idx].static_paths << p;
            CollectionStore::save_all(cols);
            QMessageBox::information(parent, "Kolekcja",
                QString("Dodano %1 plik(ów) do kolekcji \"%2\".")
                .arg(paths.size()).arg(cols[idx].name));
        }
    }
}

} // namespace LapesEye
