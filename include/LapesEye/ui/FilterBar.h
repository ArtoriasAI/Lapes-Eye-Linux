#pragma once
#include "LapesEye/ui/ThumbnailGrid.h"
#include "LapesEye/ui/CompareView.h"
#include <QWidget>
#include <QToolButton>
#include <QLineEdit>
#include <QComboBox>
#include <QPixmap>

namespace LapesEye {

class FilterBar : public QWidget {
    Q_OBJECT
public:
    explicit FilterBar(QWidget* parent = nullptr);

signals:
    void filter_changed(const GridFilter& filter);
    void view_mode_changed(CompareMode mode);
    void advanced_search_requested();
    void rotate_requested(int degrees);  // -90 lub +90

private:
    void emit_filter();
    void setup_ui();

    // Ikony trybów rysowane programowo
    static QPixmap make_single_icon();
    static QPixmap make_split2_icon();
    static QPixmap make_split4_icon();

    QToolButton* m_all_btn     = nullptr;
    QToolButton* m_pick_only   = nullptr;
    QToolButton* m_reject_only = nullptr;
    QToolButton* m_raw_only    = nullptr;
    QToolButton* m_psd_only    = nullptr;
    QToolButton* m_rotate_ccw  = nullptr;
    QToolButton* m_rotate_cw   = nullptr;
    QLineEdit*   m_search      = nullptr;
    QComboBox*   m_sort_combo  = nullptr;

    // Tryby widoku
    QToolButton* m_single_btn  = nullptr;
    QToolButton* m_split2_btn  = nullptr;
    QToolButton* m_split4_btn  = nullptr;
};

} // namespace LapesEye
