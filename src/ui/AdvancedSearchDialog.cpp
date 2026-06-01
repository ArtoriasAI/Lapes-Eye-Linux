#include "LapesEye/ui/AdvancedSearchDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QScrollArea>

namespace LapesEye {

AdvancedSearchDialog::AdvancedSearchDialog(const GridFilter& cur, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Wyszukiwanie zaawansowane");
    setMinimumWidth(440);

    auto* root = new QVBoxLayout(this);

    // Scroll area na wypadek małego ekranu
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget(scroll);
    scroll->setWidget(inner);
    auto* layout = new QVBoxLayout(inner);
    layout->setSpacing(10);
    root->addWidget(scroll, 1);

    // ── Nazwa pliku ───────────────────────────────────────────────────────────
    auto* name_box = new QGroupBox("Nazwa pliku", inner);
    auto* name_lay = new QFormLayout(name_box);
    m_name = new QLineEdit(cur.name_contains, inner);
    m_name->setPlaceholderText("Zawiera tekst...");
    m_name->setClearButtonEnabled(true);
    name_lay->addRow("Zawiera:", m_name);
    layout->addWidget(name_box);

    // ── Typ pliku ─────────────────────────────────────────────────────────────
    auto* type_box = new QGroupBox("Typ pliku", inner);
    auto* type_lay = new QHBoxLayout(type_box);
    m_only_jpg = new QCheckBox("JPEG", inner);
    m_only_raw = new QCheckBox("RAW", inner);
    m_only_psd = new QCheckBox("PSD", inner);
    m_only_jpg->setChecked(cur.only_jpg);
    m_only_raw->setChecked(cur.only_raw);
    m_only_psd->setChecked(cur.only_psd);
    type_lay->addWidget(m_only_jpg);
    type_lay->addWidget(m_only_raw);
    type_lay->addWidget(m_only_psd);
    type_lay->addStretch();
    layout->addWidget(type_box);

    // ── Data modyfikacji ──────────────────────────────────────────────────────
    auto* date_box = new QGroupBox("Data modyfikacji", inner);
    auto* date_lay = new QFormLayout(date_box);

    auto* date_from_row = new QHBoxLayout();
    m_date_from_chk = new QCheckBox("Od:", inner);
    m_date_from_chk->setChecked(cur.use_date_from);
    m_date_from = new QDateEdit(cur.use_date_from ? cur.date_from
                                                  : QDate::currentDate().addYears(-1), inner);
    m_date_from->setCalendarPopup(true);
    m_date_from->setEnabled(cur.use_date_from);
    date_from_row->addWidget(m_date_from_chk);
    date_from_row->addWidget(m_date_from, 1);
    date_lay->addRow(date_from_row);

    auto* date_to_row = new QHBoxLayout();
    m_date_to_chk = new QCheckBox("Do:", inner);
    m_date_to_chk->setChecked(cur.use_date_to);
    m_date_to = new QDateEdit(cur.use_date_to ? cur.date_to : QDate::currentDate(), inner);
    m_date_to->setCalendarPopup(true);
    m_date_to->setEnabled(cur.use_date_to);
    date_to_row->addWidget(m_date_to_chk);
    date_to_row->addWidget(m_date_to, 1);
    date_lay->addRow(date_to_row);

    QObject::connect(m_date_from_chk, &QCheckBox::toggled, m_date_from, &QDateEdit::setEnabled);
    QObject::connect(m_date_to_chk,   &QCheckBox::toggled, m_date_to,   &QDateEdit::setEnabled);
    layout->addWidget(date_box);

    // ── Rozmiar pliku ─────────────────────────────────────────────────────────
    auto* size_box = new QGroupBox("Rozmiar pliku (MB)", inner);
    auto* size_lay = new QFormLayout(size_box);

    auto* size_min_row = new QHBoxLayout();
    m_size_min_chk = new QCheckBox("Min:", inner);
    m_size_min_chk->setChecked(cur.use_size_min);
    m_size_min = new QSpinBox(inner);
    m_size_min->setRange(0, 10000); m_size_min->setSuffix(" MB");
    m_size_min->setValue(cur.use_size_min ? (int)cur.size_min_mb : 1);
    m_size_min->setEnabled(cur.use_size_min);
    size_min_row->addWidget(m_size_min_chk);
    size_min_row->addWidget(m_size_min, 1);
    size_lay->addRow(size_min_row);

    auto* size_max_row = new QHBoxLayout();
    m_size_max_chk = new QCheckBox("Max:", inner);
    m_size_max_chk->setChecked(cur.use_size_max);
    m_size_max = new QSpinBox(inner);
    m_size_max->setRange(0, 10000); m_size_max->setSuffix(" MB");
    m_size_max->setValue(cur.use_size_max ? (int)cur.size_max_mb : 50);
    m_size_max->setEnabled(cur.use_size_max);
    size_max_row->addWidget(m_size_max_chk);
    size_max_row->addWidget(m_size_max, 1);
    size_lay->addRow(size_max_row);

    QObject::connect(m_size_min_chk, &QCheckBox::toggled, m_size_min, &QSpinBox::setEnabled);
    QObject::connect(m_size_max_chk, &QCheckBox::toggled, m_size_max, &QSpinBox::setEnabled);
    layout->addWidget(size_box);

    // ── Wymiary ───────────────────────────────────────────────────────────────
    auto* dim_box = new QGroupBox("Wymiary minimalne", inner);
    auto* dim_lay = new QHBoxLayout(dim_box);
    m_dim_chk = new QCheckBox("Włącz:", inner);
    m_dim_chk->setChecked(cur.use_dim);
    m_dim_w_min = new QSpinBox(inner);
    m_dim_w_min->setRange(0, 100000); m_dim_w_min->setSuffix(" px szer.");
    m_dim_w_min->setValue(cur.use_dim ? cur.dim_w_min : 1000);
    m_dim_w_min->setEnabled(cur.use_dim);
    m_dim_h_min = new QSpinBox(inner);
    m_dim_h_min->setRange(0, 100000); m_dim_h_min->setSuffix(" px wys.");
    m_dim_h_min->setValue(cur.use_dim ? cur.dim_h_min : 1000);
    m_dim_h_min->setEnabled(cur.use_dim);
    dim_lay->addWidget(m_dim_chk);
    dim_lay->addWidget(m_dim_w_min, 1);
    dim_lay->addWidget(m_dim_h_min, 1);
    QObject::connect(m_dim_chk, &QCheckBox::toggled, m_dim_w_min, &QSpinBox::setEnabled);
    QObject::connect(m_dim_chk, &QCheckBox::toggled, m_dim_h_min, &QSpinBox::setEnabled);
    layout->addWidget(dim_box);

    // ── EXIF ──────────────────────────────────────────────────────────────────
    auto* exif_box = new QGroupBox("Dane EXIF", inner);
    auto* exif_lay = new QFormLayout(exif_box);
    m_camera = new QLineEdit(cur.camera_contains, inner);
    m_camera->setPlaceholderText("np. Sony, Canon, Nikon...");
    m_camera->setClearButtonEnabled(true);
    m_lens = new QLineEdit(cur.lens_contains, inner);
    m_lens->setPlaceholderText("np. 24-105, 50mm...");
    m_lens->setClearButtonEnabled(true);

    auto* iso_row = new QHBoxLayout();
    m_iso_min = new QSpinBox(inner);
    m_iso_min->setRange(0, 102400); m_iso_min->setSpecialValueText("—");
    m_iso_min->setValue(cur.iso_min);
    m_iso_max = new QSpinBox(inner);
    m_iso_max->setRange(0, 102400); m_iso_max->setSpecialValueText("—");
    m_iso_max->setValue(cur.iso_max);
    iso_row->addWidget(new QLabel("ISO od:", inner));
    iso_row->addWidget(m_iso_min, 1);
    iso_row->addWidget(new QLabel("do:", inner));
    iso_row->addWidget(m_iso_max, 1);

    exif_lay->addRow("Aparat:", m_camera);
    exif_lay->addRow("Obiektyw:", m_lens);
    exif_lay->addRow(iso_row);

    // Ogniskowa (mm)
    auto* focal_row = new QHBoxLayout();
    m_focal_min = new QDoubleSpinBox(inner);
    m_focal_min->setRange(0, 2000); m_focal_min->setSuffix(" mm");
    m_focal_min->setSpecialValueText("—"); m_focal_min->setValue(cur.focal_min);
    m_focal_max = new QDoubleSpinBox(inner);
    m_focal_max->setRange(0, 2000); m_focal_max->setSuffix(" mm");
    m_focal_max->setSpecialValueText("—"); m_focal_max->setValue(cur.focal_max);
    focal_row->addWidget(new QLabel("od:", inner));
    focal_row->addWidget(m_focal_min, 1);
    focal_row->addWidget(new QLabel("do:", inner));
    focal_row->addWidget(m_focal_max, 1);
    exif_lay->addRow("Ogniskowa:", focal_row);

    // Przysłona f/
    auto* fnum_row = new QHBoxLayout();
    m_fnum_min = new QDoubleSpinBox(inner);
    m_fnum_min->setRange(0, 64); m_fnum_min->setPrefix("f/");
    m_fnum_min->setSpecialValueText("—"); m_fnum_min->setSingleStep(0.5);
    m_fnum_min->setValue(cur.fnumber_min);
    m_fnum_max = new QDoubleSpinBox(inner);
    m_fnum_max->setRange(0, 64); m_fnum_max->setPrefix("f/");
    m_fnum_max->setSpecialValueText("—"); m_fnum_max->setSingleStep(0.5);
    m_fnum_max->setValue(cur.fnumber_max);
    fnum_row->addWidget(new QLabel("od:", inner));
    fnum_row->addWidget(m_fnum_min, 1);
    fnum_row->addWidget(new QLabel("do:", inner));
    fnum_row->addWidget(m_fnum_max, 1);
    exif_lay->addRow("Przysłona:", fnum_row);

    // Czas ekspozycji 1/N
    auto* exp_row = new QHBoxLayout();
    auto* exp_lbl = new QLabel("Czas 1/N (szybszy→wolniejszy):", inner);
    exp_lbl->setToolTip("np. Min=200, Max=50 → zdjęcia od 1/200s do 1/50s");
    m_exp_min = new QSpinBox(inner);
    m_exp_min->setRange(0, 32000); m_exp_min->setPrefix("1/");
    m_exp_min->setSpecialValueText("—"); m_exp_min->setValue(cur.exposure_denom_min);
    m_exp_max = new QSpinBox(inner);
    m_exp_max->setRange(0, 32000); m_exp_max->setPrefix("1/");
    m_exp_max->setSpecialValueText("—"); m_exp_max->setValue(cur.exposure_denom_max);
    exp_row->addWidget(new QLabel("szybszy:", inner));
    exp_row->addWidget(m_exp_min, 1);
    exp_row->addWidget(new QLabel("wolniejszy:", inner));
    exp_row->addWidget(m_exp_max, 1);
    exif_lay->addRow(exp_lbl);
    exif_lay->addRow(exp_row);

    layout->addWidget(exif_box);

    layout->addStretch();

    // Przyciski
    auto* btn_row = new QHBoxLayout();
    auto* clear_btn = new QPushButton("Wyczyść wszystko", this);
    clear_btn->setFlat(true);
    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btn_row->addWidget(clear_btn);
    btn_row->addStretch();
    btn_row->addWidget(btns);
    root->addLayout(btn_row);

    QObject::connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QObject::connect(clear_btn, &QPushButton::clicked, this, [this]() {
        m_name->clear();
        m_only_jpg->setChecked(false);
        m_only_raw->setChecked(false);
        m_only_psd->setChecked(false);
        m_date_from_chk->setChecked(false);
        m_date_to_chk->setChecked(false);
        m_size_min_chk->setChecked(false);
        m_size_max_chk->setChecked(false);
        m_dim_chk->setChecked(false);
        m_camera->clear(); m_lens->clear();
        m_iso_min->setValue(0); m_iso_max->setValue(0);
        m_focal_min->setValue(0); m_focal_max->setValue(0);
        m_fnum_min->setValue(0); m_fnum_max->setValue(0);
        m_exp_min->setValue(0); m_exp_max->setValue(0);
    });
}

GridFilter AdvancedSearchDialog::result() const {
    GridFilter f;
    f.name_contains   = m_name->text().trimmed();
    f.only_jpg        = m_only_jpg->isChecked();
    f.only_raw        = m_only_raw->isChecked();
    f.only_psd        = m_only_psd->isChecked();
    f.use_date_from   = m_date_from_chk->isChecked();
    f.date_from       = m_date_from->date();
    f.use_date_to     = m_date_to_chk->isChecked();
    f.date_to         = m_date_to->date();
    f.use_size_min    = m_size_min_chk->isChecked();
    f.size_min_mb     = m_size_min->value();
    f.use_size_max    = m_size_max_chk->isChecked();
    f.size_max_mb     = m_size_max->value();
    f.use_dim         = m_dim_chk->isChecked();
    f.dim_w_min       = m_dim_w_min->value();
    f.dim_h_min       = m_dim_h_min->value();
    f.camera_contains = m_camera->text().trimmed();
    f.lens_contains   = m_lens->text().trimmed();
    f.iso_min         = m_iso_min->value();
    f.iso_max         = m_iso_max->value();
    f.focal_min       = m_focal_min->value();
    f.focal_max       = m_focal_max->value();
    f.fnumber_min     = m_fnum_min->value();
    f.fnumber_max     = m_fnum_max->value();
    f.exposure_denom_min = m_exp_min->value();
    f.exposure_denom_max = m_exp_max->value();
    return f;
}

} // namespace LapesEye
