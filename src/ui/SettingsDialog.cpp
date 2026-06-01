#include "LapesEye/ui/SettingsDialog.h"
#include "LapesEye/core/ThumbCache.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QPixmapCache>
#include <QLabel>
#include <QSlider>
#include <QMessageBox>
#include <QFileDialog>

namespace LapesEye {

// ─── Statyczne odczyty ustawień ───────────────────────────────────────────────

int SettingsDialog::ram_cache_mb() {
    QSettings s("Lape", "LapesEye");
    return s.value("cache/ram_mb", 4096).toInt();
}
int SettingsDialog::disk_cache_mb() {
    QSettings s("Lape", "LapesEye");
    return s.value("cache/disk_mb", 2048).toInt();
}
int SettingsDialog::cache_clear_days() {
    QSettings s("Lape", "LapesEye");
    return s.value("cache/clear_days", 30).toInt();
}
int SettingsDialog::thumb_size_default() {
    QSettings s("Lape", "LapesEye");
    return s.value("thumb_size", 220).toInt();
}
bool SettingsDialog::preload_full_quality() {
    QSettings s("Lape", "LapesEye");
    return s.value("cache/preload_full", true).toBool();
}
bool SettingsDialog::histogram_visible() {
    QSettings s("Lape", "LapesEye");
    return s.value("ui/histogram_visible", true).toBool();
}
QString SettingsDialog::external_editor_path() {
    QSettings s("Lape", "LapesEye");
    return s.value("editor/path", "").toString();
}
QString SettingsDialog::external_editor_args() {
    QSettings s("Lape", "LapesEye");
    return s.value("editor/args", "").toString();
}
bool SettingsDialog::external_editor_as_layer() {
    QSettings s("Lape", "LapesEye");
    return s.value("editor/as_layer", false).toBool();
}

// ─── Konstruktor ──────────────────────────────────────────────────────────────

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Ustawienia Lape's Eye");
    setMinimumWidth(480);
    setModal(true);
    setup_ui();
    load_values();
}

void SettingsDialog::setup_ui() {
    auto* layout = new QVBoxLayout(this);

    // ── Bufor RAM ──────────────────────────────────────────────────────────────
    auto* ram_box = new QGroupBox("Bufor RAM (QPixmapCache)", this);
    auto* ram_form = new QFormLayout(ram_box);
    ram_form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    m_ram_cache_mb = new QSpinBox(this);
    m_ram_cache_mb->setRange(256, 32768);
    m_ram_cache_mb->setSingleStep(256);
    m_ram_cache_mb->setSuffix(" MB");
    m_ram_cache_mb->setToolTip(
        "Miniatury przechowywane w RAM — dostęp natychmiastowy.\n"
        "Przy 64GB RAM można dać nawet 8192–16384 MB.");

    m_ram_cache_label = new QLabel(this);
    m_ram_cache_label->setStyleSheet("color: #888; font-size: 10px;");

    ram_form->addRow("Rozmiar:", m_ram_cache_mb);
    ram_form->addRow("", m_ram_cache_label);
    layout->addWidget(ram_box);

    QObject::connect(m_ram_cache_mb, qOverload<int>(&QSpinBox::valueChanged),
                     this, &SettingsDialog::update_size_labels);

    // ── Bufor dysku ────────────────────────────────────────────────────────────
    auto* disk_box = new QGroupBox("Bufor dysku (SQLite)", this);
    auto* disk_form = new QFormLayout(disk_box);

    m_disk_cache_mb = new QSpinBox(this);
    m_disk_cache_mb->setRange(128, 65536);
    m_disk_cache_mb->setSingleStep(256);
    m_disk_cache_mb->setSuffix(" MB");
    m_disk_cache_mb->setToolTip(
        "Miniatury zapisane na dysku — trwałe między sesjami.\n"
        "Większy = więcej zdjęć bez ponownego generowania miniatur.");

    m_disk_cache_label = new QLabel(this);
    m_disk_cache_label->setStyleSheet("color: #888; font-size: 10px;");

    m_disk_cache_info = new QLabel(this);
    m_disk_cache_info->setStyleSheet("color: #aaa; font-size: 10px;");

    disk_form->addRow("Rozmiar:", m_disk_cache_mb);
    disk_form->addRow("", m_disk_cache_label);
    disk_form->addRow("Bieżący rozmiar:", m_disk_cache_info);
    layout->addWidget(disk_box);

    QObject::connect(m_disk_cache_mb, qOverload<int>(&QSpinBox::valueChanged),
                     this, &SettingsDialog::update_size_labels);

    // ── Czyszczenie ────────────────────────────────────────────────────────────
    auto* clear_box = new QGroupBox("Automatyczne czyszczenie", this);
    auto* clear_form = new QFormLayout(clear_box);

    m_clear_interval = new QComboBox(this);
    m_clear_interval->addItem("Nigdy",        0);
    m_clear_interval->addItem("Co tydzień",   7);
    m_clear_interval->addItem("Co 2 tygodnie", 14);
    m_clear_interval->addItem("Co miesiąc",   30);
    m_clear_interval->addItem("Co 3 miesiące", 90);
    m_clear_interval->setToolTip("Usuwa miniatury plików które nie były używane przez podany czas.");

    m_clear_on_start = new QCheckBox("Wyczyść brakujące pliki przy starcie", this);
    m_clear_on_start->setToolTip("Usuwa miniatury dla plików których już nie ma na dysku.");

    m_preload_full = new QCheckBox("Wczytuj pełną jakość w tle", this);
    m_preload_full->setToolTip(
        "Po załadowaniu szybkiej miniatury automatycznie\n"
        "ładuje wersję wysokiej jakości w tle.");

    clear_form->addRow("Czyść cache starszy niż:", m_clear_interval);
    clear_form->addRow("", m_clear_on_start);
    clear_form->addRow("", m_preload_full);

    // Przycisk ręcznego czyszczenia
    auto* clear_now_btn = new QPushButton("🗑  Wyczyść cache teraz", this);
    clear_now_btn->setStyleSheet("padding: 4px 12px;");
    clear_form->addRow("", clear_now_btn);
    QObject::connect(clear_now_btn, &QPushButton::clicked,
                     this, &SettingsDialog::clear_disk_cache_now);

    layout->addWidget(clear_box);

    // ── Interfejs ─────────────────────────────────────────────────────────────
    auto* ui_box  = new QGroupBox("Interfejs", this);
    auto* ui_form = new QFormLayout(ui_box);
    m_histogram_visible = new QCheckBox("Pokazuj histogram w panelu podglądu", this);
    m_histogram_visible->setToolTip(
        "Histogram RGB pod miniaturą podglądu.\n"
        "Wyłącz jeśli wolisz mieć więcej miejsca na podgląd.");
    ui_form->addRow("", m_histogram_visible);
    layout->addWidget(ui_box);

    // ── Zewnętrzny edytor ─────────────────────────────────────────────────────
    auto* ed_box  = new QGroupBox("Zewnętrzny edytor (E)", this);
    auto* ed_form = new QFormLayout(ed_box);
    ed_form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    // Wiersz: pole ścieżki + przycisk Przeglądaj
    m_editor_path = new QLineEdit(this);
    m_editor_path->setPlaceholderText("/usr/bin/photoshop  lub  flatpak run com.adobe.Photoshop");
    m_editor_path->setToolTip("Ścieżka do pliku wykonywalnego edytora");

    m_editor_browse = new QPushButton("…", this);
    m_editor_browse->setFixedWidth(32);
    QObject::connect(m_editor_browse, &QPushButton::clicked,
                     this, &SettingsDialog::browse_editor);

    auto* path_row = new QHBoxLayout();
    path_row->addWidget(m_editor_path);
    path_row->addWidget(m_editor_browse);
    ed_form->addRow("Program:", path_row);

    // Dodatkowe argumenty
    m_editor_args = new QLineEdit(this);
    m_editor_args->setPlaceholderText("np. --new-window  (pozostaw puste jeśli nie potrzeba)");
    m_editor_args->setToolTip(
        "Argumenty przekazywane przed ścieżką pliku.\n"
        "Plik jest zawsze dodawany na końcu: program [args] plik.jpg");
    ed_form->addRow("Argumenty:", m_editor_args);

    // Opcja Photoshop — otwórz jako warstwę
    m_editor_as_layer = new QCheckBox("Otwórz jako warstwę w aktywnym dokumencie Photoshopa", this);
    m_editor_as_layer->setToolTip(
        "Gdy zaznaczone: zamiast uruchamiać program, wysyła plik do otwartego\n"
        "Photoshopa jako nową warstwę (wymaga Wine + Photoshop lub natywnego PS).\n"
        "Używa: File → Place Embedded / PS Actions / IPC przez Lape plugin.");
    ed_form->addRow("", m_editor_as_layer);

    // Info jak działa
    auto* ed_info = new QLabel(
        "<small style='color:#888'>Skrót <b>E</b> — otwórz zaznaczone zdjęcia w edytorze.<br>"
        "Skrót <b>Shift+E</b> — otwórz jako warstwę w Photoshopie (jeśli opcja zaznaczona).</small>",
        this);
    ed_info->setTextFormat(Qt::RichText);
    ed_form->addRow("", ed_info);

    layout->addWidget(ed_box);

    // ── Przyciski ──────────────────────────────────────────────────────────────
    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::Apply, this);
    btns->button(QDialogButtonBox::Ok)->setText("OK");
    btns->button(QDialogButtonBox::Cancel)->setText("Anuluj");
    btns->button(QDialogButtonBox::Apply)->setText("Zastosuj");

    QObject::connect(btns, &QDialogButtonBox::accepted, this, [this]() {
        apply(); accept();
    });
    QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QObject::connect(btns->button(QDialogButtonBox::Apply),
                     &QPushButton::clicked, this, &SettingsDialog::apply);

    layout->addWidget(btns);
}

void SettingsDialog::load_values() {
    m_ram_cache_mb->setValue(ram_cache_mb());
    m_disk_cache_mb->setValue(disk_cache_mb());
    m_preload_full->setChecked(preload_full_quality());

    QSettings s("Lape", "LapesEye");
    m_clear_on_start->setChecked(s.value("cache/clear_on_start", true).toBool());

    int days = cache_clear_days();
    for (int i = 0; i < m_clear_interval->count(); ++i) {
        if (m_clear_interval->itemData(i).toInt() == days) {
            m_clear_interval->setCurrentIndex(i);
            break;
        }
    }

    m_histogram_visible->setChecked(histogram_visible());

    // Zewnętrzny edytor
    m_editor_path->setText(external_editor_path());
    m_editor_args->setText(external_editor_args());
    m_editor_as_layer->setChecked(external_editor_as_layer());

    update_size_labels();
}

void SettingsDialog::update_size_labels() {
    int ram = m_ram_cache_mb->value();
    m_ram_cache_label->setText(
        QString("≈ %1 miniatur w pamięci (przy śr. 150 KB/szt.)")
            .arg(ram * 1024 / 150));

    int disk = m_disk_cache_mb->value();
    m_disk_cache_label->setText(
        QString("≈ %1 miniatur na dysku (przy śr. 80 KB/szt.)")
            .arg(disk * 1024 / 80));

    // Pokaż aktualny rozmiar bazy (jeśli dostępny przez settings)
    QSettings s("Lape", "LapesEye");
    double current_mb = s.value("cache/current_size_mb", 0.0).toDouble();
    if (current_mb > 0)
        m_disk_cache_info->setText(
            QString("%1 MB (%.0f%% limitu)")
                .arg(current_mb, 0, 'f', 1)
                .arg(current_mb / disk * 100));
    else
        m_disk_cache_info->setText("(nieznany — uruchom program żeby sprawdzić)");
}

void SettingsDialog::apply() {
    save_values();

    // Zastosuj RAM cache natychmiast
    int ram_kb = m_ram_cache_mb->value() * 1024;
    QPixmapCache::setCacheLimit(ram_kb);
}

void SettingsDialog::save_values() {
    QSettings s("Lape", "LapesEye");
    s.setValue("cache/ram_mb",       m_ram_cache_mb->value());
    s.setValue("cache/disk_mb",      m_disk_cache_mb->value());
    s.setValue("cache/clear_days",   m_clear_interval->currentData().toInt());
    s.setValue("cache/clear_on_start", m_clear_on_start->isChecked());
    s.setValue("cache/preload_full", m_preload_full->isChecked());
    s.setValue("ui/histogram_visible", m_histogram_visible->isChecked());

    // Zewnętrzny edytor
    s.setValue("editor/path",     m_editor_path->text().trimmed());
    s.setValue("editor/args",     m_editor_args->text().trimmed());
    s.setValue("editor/as_layer", m_editor_as_layer->isChecked());
}

void SettingsDialog::browse_editor() {
    QString path = QFileDialog::getOpenFileName(
        this, "Wybierz edytor", "/usr/bin",
        "Programy (*);; Wszystkie pliki (*)");
    if (!path.isEmpty())
        m_editor_path->setText(path);
}

void SettingsDialog::clear_disk_cache_now() {
    auto btn = QMessageBox::question(this, "Wyczyść cache",
        "Usunąć wszystkie miniatury z cache dysku?\n"
        "Zostaną wygenerowane ponownie przy następnym przeglądaniu.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    QPixmapCache::clear();

    // Sygnał do MainWindow żeby wyczyścił ThumbCache
    emit cache_clear_requested();
}

} // namespace LapesEye
