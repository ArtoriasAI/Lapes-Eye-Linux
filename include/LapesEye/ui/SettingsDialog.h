#pragma once
#include <QDialog>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>

namespace LapesEye {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    static int  ram_cache_mb();
    static int  disk_cache_mb();
    static int  cache_clear_days();
    static int  thumb_size_default();
    static bool preload_full_quality();
    static bool histogram_visible();

    // Zewnętrzny edytor
    static QString external_editor_path();
    static QString external_editor_args();  // np. "--as-layer" dla Photoshopa
    static bool    external_editor_as_layer();  // true = otwórz jako warstwę w PS

signals:
    void cache_clear_requested();

private slots:
    void apply();
    void clear_disk_cache_now();
    void browse_editor();

private:
    void setup_ui();
    void load_values();
    void save_values();
    void update_size_labels();

    QSpinBox*    m_ram_cache_mb       = nullptr;
    QLabel*      m_ram_cache_label    = nullptr;
    QSpinBox*    m_disk_cache_mb      = nullptr;
    QLabel*      m_disk_cache_label   = nullptr;
    QLabel*      m_disk_cache_info    = nullptr;
    QComboBox*   m_clear_interval     = nullptr;
    QCheckBox*   m_clear_on_start     = nullptr;
    QCheckBox*   m_preload_full       = nullptr;
    QCheckBox*   m_histogram_visible  = nullptr;

    // Zewnętrzny edytor
    QLineEdit*   m_editor_path        = nullptr;
    QLineEdit*   m_editor_args        = nullptr;
    QCheckBox*   m_editor_as_layer    = nullptr;
    QPushButton* m_editor_browse      = nullptr;
};

} // namespace LapesEye
