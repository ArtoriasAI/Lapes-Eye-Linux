#include "LapesEye/ui/MetaPanel.h"
#include "LapesEye/core/MetaStore.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>

namespace LapesEye {

MetaPanel::MetaPanel(QWidget* parent) : QWidget(parent) {
    setup_ui();
}

void MetaPanel::setup_ui() {
    // Cały panel w scroll area — dużo pól
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* inner = new QWidget(scroll);
    auto* outer = new QVBoxLayout(inner);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(6);

    // ── Nazwa pliku (edytowalna) ──────────────────────────────────────────────
    auto* name_box = new QGroupBox("Plik", inner);
    auto* name_lay = new QVBoxLayout(name_box);
    m_filename = new QLineEdit(inner);
    m_filename->setPlaceholderText("Nazwa pliku...");
    m_filename->setStyleSheet("font-size: 11px; padding: 3px;");
    name_lay->addWidget(m_filename);

    // Problem 4: zaznacz całą nazwę bez rozszerzenia przy kliknięciu w pole
    m_filename->installEventFilter(this);

    // Enter lub utrata focusu → zatwierdź rename (editingFinished obejmuje oba)
    QObject::connect(m_filename, &QLineEdit::editingFinished,
                     this, &MetaPanel::on_rename_committed);

    outer->addWidget(name_box);

    // ── EXIF ──────────────────────────────────────────────────────────────────
    auto* exif_box = new QGroupBox("EXIF", inner);
    auto* exif_form = new QFormLayout(exif_box);
    exif_form->setLabelAlignment(Qt::AlignRight);
    exif_form->setHorizontalSpacing(8);
    exif_form->setVerticalSpacing(3);

    auto mk = [&]() -> QLabel* {
        auto* l = new QLabel("—", inner);
        l->setStyleSheet("color: #aaa; font-size: 10px;");
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        return l;
    };

    m_lbl_camera     = mk();
    m_lbl_lens       = mk();
    m_lbl_date       = mk();
    m_lbl_exposure   = mk();
    m_lbl_iso        = mk();
    m_lbl_focal      = mk();
    m_lbl_dims       = mk();
    m_lbl_colorspace = mk();
    exif_form->addRow("Aparat:",      m_lbl_camera);
    exif_form->addRow("Obiektyw:",    m_lbl_lens);
    exif_form->addRow("Data:",        m_lbl_date);
    exif_form->addRow("Ekspoz.:",     m_lbl_exposure);
    exif_form->addRow("ISO:",         m_lbl_iso);
    exif_form->addRow("Ogniskowa:",   m_lbl_focal);
    exif_form->addRow("Wymiary:",     m_lbl_dims);
    exif_form->addRow("Kolor:",       m_lbl_colorspace);

    outer->addWidget(exif_box);

    // ── Flaga: Wybrane / Odrzucone ────────────────────────────────────────────
    auto* flag_box = new QGroupBox("Oznaczenie", inner);
    auto* flag_lay = new QHBoxLayout(flag_box);
    flag_lay->setSpacing(4);

    m_btn_pick = new QPushButton("✓  Wybrane", inner);
    m_btn_pick->setCheckable(true);
    m_btn_pick->setStyleSheet(R"(
        QPushButton { background:#2a2a2a; border:1px solid #444; border-radius:4px;
                      color:#ccc; padding:5px 8px; font-size:11px; }
        QPushButton:checked { background:#1a5c1a; border-color:#2ECC71; color:#2ECC71; }
        QPushButton:hover:!checked { background:#333; }
    )");

    m_btn_reject = new QPushButton("✗  Odrzucone", inner);
    m_btn_reject->setCheckable(true);
    m_btn_reject->setStyleSheet(R"(
        QPushButton { background:#2a2a2a; border:1px solid #444; border-radius:4px;
                      color:#ccc; padding:5px 8px; font-size:11px; }
        QPushButton:checked { background:#5c1a1a; border-color:#E53434; color:#E53434; }
        QPushButton:hover:!checked { background:#333; }
    )");

    flag_lay->addWidget(m_btn_pick);
    flag_lay->addWidget(m_btn_reject);
    outer->addWidget(flag_box);

    // m_file_counter tworzony tutaj, ale dodawany do layoutu po addStretch (na dole)
    m_file_counter = new QLabel("Pliki: —", inner);
    m_file_counter->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Pick/Reject są wzajemnie wykluczające
    QObject::connect(m_btn_pick, &QPushButton::clicked, this, [this](bool checked) {
        if (checked) {
            m_btn_reject->setChecked(false);
            m_current_flag = PickFlag::Pick;
        } else {
            m_current_flag = PickFlag::None;
        }
        save();
        emit flag_changed(m_current_path);
        emit return_focus();  // zwróć fokus do siatki
    });
    QObject::connect(m_btn_reject, &QPushButton::clicked, this, [this](bool checked) {
        if (checked) {
            m_btn_pick->setChecked(false);
            m_current_flag = PickFlag::Reject;
        } else {
            m_current_flag = PickFlag::None;
        }
        save();
        emit flag_changed(m_current_path);
        emit return_focus();
    });

    // ── Zapisz ────────────────────────────────────────────────────────────────
    m_save_btn = new QPushButton("Zapisz (.leye)", inner);
    m_save_btn->setEnabled(false);
    m_save_btn->setVisible(false);
    outer->addWidget(m_save_btn);
    outer->addStretch();  // wypycha counter_box na sam dół

    // ── Licznik plików — na samym dole jak na zrzucie ─────────────────────────
    auto* counter_lay2 = new QHBoxLayout();
    counter_lay2->setContentsMargins(8, 2, 8, 6);
    m_file_counter->setParent(inner);  // już stworzony wyżej, tylko przepinamy layout
    counter_lay2->addWidget(m_file_counter);
    outer->addLayout(counter_lay2);

    QObject::connect(m_save_btn, &QPushButton::clicked, this, &MetaPanel::save);

    scroll->setWidget(inner);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);
}

// ─── Programowe ustawienie flagi (skróty Z/X) ────────────────────────────────

void MetaPanel::set_flag(PickFlag flag) {
    if (m_current_path.isEmpty()) return;
    if (m_current_flag == flag) flag = PickFlag::None;  // toggle
    m_current_flag = flag;
    m_btn_pick->setChecked(flag == PickFlag::Pick);
    m_btn_reject->setChecked(flag == PickFlag::Reject);
    save();
    emit flag_changed(m_current_path);
}

// ─── Event filter ────────────────────────────────────────────────────────────

bool MetaPanel::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_filename) {
        // Zaznacz nazwę bez rozszerzenia przy focus
        if (event->type() == QEvent::FocusIn) {
            QTimer::singleShot(0, this, [this]() {
                if (!m_filename) return;
                QString name = m_filename->text();
                int dot = name.lastIndexOf('.');
                if (dot > 0)
                    m_filename->setSelection(0, dot);
                else
                    m_filename->selectAll();
            });
        }
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            bool is_enter = (ke->key() == Qt::Key_Return)
                         || (ke->key() == Qt::Key_Enter)
                         || (ke->nativeScanCode() == 36)
                         || (ke->nativeScanCode() == 104);
            if (is_enter) {
                m_filename->disconnect(SIGNAL(editingFinished()));
                on_rename_committed();
                m_filename->clearFocus();
                QObject::connect(m_filename, &QLineEdit::editingFinished,
                                 this, &MetaPanel::on_rename_committed);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ─── Ładowanie ───────────────────────────────────────────────────────────────

bool MetaPanel::is_editing_filename() const {
    return m_filename && m_filename->hasFocus();
}

void MetaPanel::load(const FileMetadata& meta_const) {
    // Lazy EXIF — ładuj tylko gdy MetaPanel wyświetla dane
    FileMetadata meta = meta_const;
    if (!meta.loaded_exif)
        MetaStore::load_exif(meta);

    m_current_path = meta.path;

    // Problem 5: blokuj sygnały podczas setText — inaczej editingFinished
    // triggeruje on_rename_committed ze starą nazwą i kasuje rename
    m_filename->blockSignals(true);
    QFileInfo fi(meta.path);
    m_filename->setText(fi.fileName());
    m_filename->setProperty("original_name", fi.fileName());
    m_filename->blockSignals(false);

    // ── EXIF ─────────────────────────────────────────────────────────────────
    auto set = [](QLabel* l, const QString& v) {
        l->setText(v.trimmed().isEmpty() ? "—" : v.trimmed());
    };

    set(m_lbl_camera, (meta.exif.camera_make + " " + meta.exif.camera_model).trimmed());
    set(m_lbl_lens,   meta.exif.lens);
    set(m_lbl_date,   meta.exif.date_taken);

    if (meta.exif.exposure_time > 0) {
        QString exp = meta.exif.exposure_time < 1
            ? QString("1/%1s").arg(qRound(1.0 / meta.exif.exposure_time))
            : QString("%1s").arg(meta.exif.exposure_time, 0, 'f', 1);
        QString fnum = meta.exif.fnumber > 0
            ? QString("  f/%1").arg(meta.exif.fnumber, 0, 'f', 1) : "";
        set(m_lbl_exposure, exp + fnum);
    } else {
        set(m_lbl_exposure, "");
    }


    set(m_lbl_iso, meta.exif.iso > 0 ? QString::number(meta.exif.iso) : "");
    set(m_lbl_focal, meta.exif.focal_length > 0
        ? QString("%1 mm").arg(meta.exif.focal_length, 0, 'f', 0) : "");
    set(m_lbl_dims, meta.exif.width > 0
        ? QString("%1 × %2 px").arg(meta.exif.width).arg(meta.exif.height) : "");

    // Przestrzeń kolorów
    set(m_lbl_colorspace, meta.exif.color_space);




    // ── Flagi i etykieta ──────────────────────────────────────────────────────
    m_current_flag = meta.pick_flag;
    m_btn_pick->setChecked(meta.pick_flag == PickFlag::Pick);
    m_btn_reject->setChecked(meta.pick_flag == PickFlag::Reject);

    m_save_btn->setEnabled(false);
}

// ─── Rename ──────────────────────────────────────────────────────────────────

void MetaPanel::on_rename_committed() {
    if (m_current_path.isEmpty()) return;
    QString new_name = m_filename->text().trimmed();
    QString orig = m_filename->property("original_name").toString();
    if (new_name.isEmpty() || new_name == orig) return;

    // Nie pozwól zmienić rozszerzenia — przywróć oryginalne
    QFileInfo fi_orig(orig);
    QFileInfo fi_new(new_name);
    QString orig_ext = fi_orig.suffix().toLower();
    QString new_ext  = fi_new.suffix().toLower();
    if (orig_ext != new_ext) {
        // Przywróć rozszerzenie
        new_name = fi_new.completeBaseName() + "." + fi_orig.suffix();
        m_filename->setText(new_name);
    }
    if (new_name == orig) return;

    emit rename_requested(m_current_path, new_name);

    // Zaktualizuj zapamiętaną nazwę
    m_filename->setProperty("original_name", new_name);

    // Zaktualizuj ścieżkę
    QFileInfo fi(m_current_path);
    m_current_path = fi.dir().filePath(new_name);
}

// ─── Licznik plików ───────────────────────────────────────────────────────────

void MetaPanel::set_file_count(int loaded, int total) {
    if (!m_file_counter) return;
    if (total == 0)
        m_file_counter->setText("Pliki: —");
    else if (loaded < total)
        m_file_counter->setText(QString("Pliki: %1 / %2").arg(loaded).arg(total));
    else
        m_file_counter->setText(QString("Pliki: %1").arg(total));
}

// ─── Zapis metadanych ─────────────────────────────────────────────────────────

void MetaPanel::save() {
    if (m_current_path.isEmpty()) return;

    FileMetadata meta = MetaStore::load(m_current_path);
    meta.pick_flag = m_current_flag;

    if (MetaStore::save(meta)) {
        m_save_btn->setEnabled(false);
        emit metadata_saved(m_current_path);
    }
}

} // namespace LapesEye
