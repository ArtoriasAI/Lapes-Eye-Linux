#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QDateEdit>
#include <QCheckBox>
#include <QGroupBox>
#include "LapesEye/ui/ThumbnailGrid.h"

namespace LapesEye {

class AdvancedSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit AdvancedSearchDialog(const GridFilter& current, QWidget* parent = nullptr);
    GridFilter result() const;

private:
    // Nazwa
    QLineEdit*   m_name        = nullptr;

    // Data
    QCheckBox*   m_date_from_chk = nullptr;
    QDateEdit*   m_date_from     = nullptr;
    QCheckBox*   m_date_to_chk   = nullptr;
    QDateEdit*   m_date_to       = nullptr;

    // Rozmiar
    QCheckBox*   m_size_min_chk  = nullptr;
    QSpinBox*    m_size_min      = nullptr;  // MB
    QCheckBox*   m_size_max_chk  = nullptr;
    QSpinBox*    m_size_max      = nullptr;

    // Wymiary
    QCheckBox*   m_dim_chk       = nullptr;
    QSpinBox*    m_dim_w_min     = nullptr;
    QSpinBox*    m_dim_h_min     = nullptr;

    // EXIF
    QLineEdit*   m_camera        = nullptr;
    QLineEdit*   m_lens          = nullptr;
    QSpinBox*    m_iso_min       = nullptr;
    QSpinBox*    m_iso_max       = nullptr;
    // Ogniskowa
    QDoubleSpinBox* m_focal_min  = nullptr;
    QDoubleSpinBox* m_focal_max  = nullptr;
    // Przysłona
    QDoubleSpinBox* m_fnum_min   = nullptr;
    QDoubleSpinBox* m_fnum_max   = nullptr;
    // Czas ekspozycji (1/N)
    QSpinBox*    m_exp_min       = nullptr;  // mianownik min (szybszy)
    QSpinBox*    m_exp_max       = nullptr;  // mianownik max (wolniejszy)

    // Typ pliku
    QCheckBox*   m_only_raw      = nullptr;
    QCheckBox*   m_only_psd      = nullptr;
    QCheckBox*   m_only_jpg      = nullptr;
};

} // namespace LapesEye
