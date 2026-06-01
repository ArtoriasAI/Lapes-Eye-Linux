#pragma once
#include <QDialog>
#include <QStringList>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QPrinter>
#include <QImage>
#include <QTimer>

namespace LapesEye {

class PrintDialog : public QDialog {
    Q_OBJECT
public:
    explicit PrintDialog(const QStringList& paths, QWidget* parent = nullptr);
    ~PrintDialog() { delete m_printer; }

private:
    void update_preview();
    void schedule_preview();
    void do_print();
    void draw_contact_sheet(QPainter& painter, QPrinter& printer);
    void draw_single(QPainter& painter, QPrinter& printer, const QImage& img,
                     const QString& caption);

    // Tryb
    QRadioButton* m_mode_single   = nullptr;  // jedno zdjęcie
    QRadioButton* m_mode_contact  = nullptr;  // arkusz kontaktowy

    // Pojedyncze zdjęcie
    QComboBox*    m_fit_mode      = nullptr;  // Dopasuj, Wypełnij, 100%
    QCheckBox*    m_caption_chk   = nullptr;  // drukuj nazwę pliku
    QCheckBox*    m_exif_chk      = nullptr;  // drukuj EXIF pod zdjęciem

    // Arkusz kontaktowy
    QSpinBox*     m_cols          = nullptr;  // liczba kolumn
    QSpinBox*     m_rows          = nullptr;  // liczba wierszy
    QCheckBox*    m_contact_names = nullptr;  // nazwy plików pod miniaturami
    QDoubleSpinBox* m_margin      = nullptr;  // margines mm
    QCheckBox*    m_border_chk    = nullptr;  // ramka wokół miniatur

    // Podgląd
    QLabel*       m_preview_lbl   = nullptr;
    QLabel*       m_info_lbl      = nullptr;
    QTimer*       m_preview_timer = nullptr;

    QStringList   m_paths;
    QImage        m_preview_src;  // miniatura pierwszego zdjęcia
    QList<QImage> m_thumbs;       // miniatury wszystkich zdjęć (cache)
    QPrinter*     m_printer = nullptr;  // trwały — zachowuje orientację
};

} // namespace LapesEye
