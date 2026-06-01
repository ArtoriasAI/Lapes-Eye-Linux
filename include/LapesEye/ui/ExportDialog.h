#pragma once
#include <QDialog>
#include <QStringList>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QLabel>
#include <QProgressDialog>
#include <QTimer>
#include <QImage>

namespace LapesEye {

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(const QStringList& paths, QWidget* parent = nullptr);

private:
    void browse_output();
    void start_export();
    void schedule_preview();   // debounce — odczekaj 300ms i odśwież
    void update_preview();     // właściwe przerysowanie podglądu

    // Cel
    QLineEdit*    m_output_dir  = nullptr;

    // Format
    QComboBox*    m_format      = nullptr;
    QSpinBox*     m_quality     = nullptr;
    QLabel*       m_quality_lbl = nullptr;

    // Zmiana rozmiaru
    QCheckBox*    m_resize_chk  = nullptr;
    QRadioButton* m_by_width    = nullptr;
    QRadioButton* m_by_height   = nullptr;
    QRadioButton* m_by_percent  = nullptr;
    QRadioButton* m_by_longer   = nullptr;
    QSpinBox*     m_resize_val  = nullptr;
    QLabel*       m_resize_unit = nullptr;

    // Nazewnictwo
    QComboBox*    m_naming      = nullptr;
    QLineEdit*    m_prefix      = nullptr;
    QSpinBox*     m_start_nr    = nullptr;

    // Watermark
    QCheckBox*    m_wm_chk      = nullptr;
    QLineEdit*    m_wm_text     = nullptr;
    QComboBox*    m_wm_pos      = nullptr;
    QSpinBox*     m_wm_size     = nullptr;
    QSpinBox*     m_wm_opacity  = nullptr;
    QSpinBox*     m_wm_count    = nullptr;
    QSpinBox*     m_wm_spacing  = nullptr;

    // Metadane
    QCheckBox*    m_keep_exif   = nullptr;
    QCheckBox*    m_keep_iptc   = nullptr;

    // Podgląd watermark
    QLabel*       m_preview_lbl = nullptr;   // widget wyświetlający podgląd
    QLabel*       m_info_lbl    = nullptr;
    QTimer*       m_preview_timer = nullptr; // debounce

    QImage        m_preview_src;             // miniatura pierwszego pliku (cache)
    QStringList   m_paths;
};

} // namespace LapesEye
