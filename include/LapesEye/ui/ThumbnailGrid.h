#pragma once
#include "LapesEye/core/FileScanner.h"
#include "LapesEye/core/MetaStore.h"
#include "LapesEye/workers/ThumbWorker.h"

#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QKeyEvent>
#include <QDate>
#include <QShowEvent>
#include <QMap>
#include <QSet>
#include <QList>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QRubberBand>
#include "LapesEye/ui/RubberOverlay.h"

namespace LapesEye {

class ThumbnailItem;

enum class SortMode {
    NameAsc,
    NameDesc,
    DateAsc,
    DateDesc,
    SizeAsc,
    SizeDesc,
    TypeAsc,
};

struct GridFilter {
    int        min_rating    = 0;
    ColorLabel color_label   = ColorLabel::None;
    PickFlag   pick_flag     = PickFlag::None;
    QString    name_contains;
    bool       only_raw      = false;
    bool       only_psd      = false;
    bool       only_jpg      = false;
    SortMode   sort_mode     = SortMode::NameAsc;

    bool       use_date_from = false;
    QDate      date_from;
    bool       use_date_to   = false;
    QDate      date_to;

    bool       use_size_min  = false;
    qint64     size_min_mb   = 0;
    bool       use_size_max  = false;
    qint64     size_max_mb   = 0;

    bool       use_dim       = false;
    int        dim_w_min     = 0;
    int        dim_h_min     = 0;

    QString    camera_contains;
    QString    lens_contains;
    int        iso_min       = 0;
    int        iso_max       = 0;
    double     focal_min     = 0;
    double     focal_max     = 0;
    double     fnumber_min   = 0;
    double     fnumber_max   = 0;
    int        exposure_denom_min = 0;
    int        exposure_denom_max = 0;

    bool is_advanced() const {
        return use_date_from || use_date_to || use_size_min || use_size_max
            || use_dim || !camera_contains.isEmpty() || !lens_contains.isEmpty()
            || iso_min > 0 || iso_max > 0 || only_jpg
            || focal_min > 0 || focal_max > 0
            || fnumber_min > 0 || fnumber_max > 0
            || exposure_denom_min > 0 || exposure_denom_max > 0;
    }
};

class ThumbnailGrid : public QWidget {
    Q_OBJECT
public:
    explicit ThumbnailGrid(ThumbWorker* worker, QWidget* parent = nullptr);

    void load_folder(const QString& dir_path);
    void load_collection(const QStringList& paths, const QString& title);
    void set_filter(const GridFilter& filter);
    const GridFilter& current_filter() const { return m_filter; }
    void set_thumb_size(int size);
    void apply_thumb_size_now(int size);  // natychmiastowe zastosowanie przy starcie
    int  thumb_size() const { return m_thumb_size; }
    QString current_dir() const { return m_current_dir; }

    QStringList selected_paths() const;
    QStringList selected_paths_ordered() const;
    QString     primary_path()   const;
    QStringList visible_file_paths() const;

    void open_primary();   // Enter — otwiera zaznaczony plik/folder
    void select_all();
    void deselect_all();
    void delete_selected();
    void refresh_metadata(const QString& path);
    void start_rename_selected();
    void select_and_rename(const QString& path);
    void reapply_filter();
    void load_folder_reselect(const QString& dir, const QString& select_path);
    void rename_item(const QString& old_path, const QString& new_name);
    void add_item_in_place(const QString& path);
    void remove_items_in_place(const QStringList& paths);
    void copy_selected(bool cut = false);
    void paste_here();
    void duplicate_selected();
    void select_path(const QString& path);
    void statusBar_message(const QString& msg);
    void apply_batch_rename(const QList<QPair<QString,QString>>& pairs);

    bool has_item(const QString& path) const {
        for (const auto& f : m_visible) if (f.path == path) return true;
        return false;
    }
    bool is_drag_active() const { return m_drag_active; }
    bool clipboard_empty() const { return m_clipboard_paths.isEmpty(); }
    class ThumbnailCanvas* canvas() const { return m_canvas; }

signals:
    // Emitowany z wątku tła po zakończeniu rename na dysku
    void rename_completed(const QString& old_path, const QString& new_path);
    void watcher_unblock();
    void scan_finished();
    void fullscreen_requested(const QStringList& paths, int index);
    void thumb_progress(int loaded, int total);
    void status_message(const QString& msg);
    void selection_changed(const QStringList& paths);
    void primary_changed(const QString& path);
    void open_in_lape(const QString& path);
    void open_as_layer(const QString& path);
    void folder_navigate(const QString& path);
    void context_menu(const QStringList& selected, const QPoint& pos);
    void context_menu_background(const QPoint& pos);
    void folder_renamed(const QString& old_path, const QString& new_path);
    void rename_done();
    void preview_rename(const QString& old_path, const QString& new_path);

public slots:
    void on_rename_completed(const QString& old_path, const QString& new_path);
    void rotate_selected(int degrees);  // +90 lub -90

protected:
    void keyPressEvent(QKeyEvent* event)       override;
    void mousePressEvent(QMouseEvent* event)   override;
    void mouseMoveEvent(QMouseEvent* event)    override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event)  override;
    void dropEvent(QDropEvent* event)          override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event)      override;
    void showEvent(QShowEvent* event)          override;
    void focusOutEvent(QFocusEvent* event)     override;
    void paintEvent(QPaintEvent* event)        override;

private slots:
    void on_thumb_ready(const QString& path, const QPixmap& pix, ThumbQuality quality);
    void on_item_clicked(const QString& path, Qt::KeyboardModifiers mods);
    void on_item_double_clicked(const QString& path);
    void on_context_menu(const QString& path, const QPoint& pos);
    void on_rename_requested(const QString& old_path, const QString& new_name);
    void on_item_drag_move(const QPoint& press_global, const QPoint& cur_global);
    void on_item_drag_released(const QPoint& global_pos);
    void do_zoom_rebuild();
    void request_visible_thumbs();
    void request_all_thumbs_background();

private:
    // ─── Wirtualna siatka ────────────────────────────────────────────────────
    void    virt_full_rebuild();          // Pełna przebudowa (zmiana cols/rozmiaru)
    void    virt_update_visible_rows();   // Aktualizacja podczas scrollowania O(widoczne)
    int     virt_cols()       const;      // Liczba kolumn przy bieżącej szerokości
    int     virt_cell_size()  const;      // Rozmiar komórki siatki (px)
    int     effective_thumb_size() const; // Efektywny rozmiar miniatury
    int     virt_total_height() const;    // Całkowita wysokość siatki (px)
    QRect   virt_item_rect(int idx) const; // Prostokąt komórki dla indeksu idx

    // ─── Pozostałe prywatne ──────────────────────────────────────────────────
    void navigate_to_index(int idx);
    void rebuild_layout();                // alias dla virt_full_rebuild (kompatybilność)
    void sync_canvas();                   // synchronizuje m_visible → ThumbnailCanvas
    QString dir_at(const QPoint& pos_in_grid) const;  // folder pod pozycją kursorą
    void apply_filter_and_rebuild();
    bool passes_filter(const ScannedFile& f) const;
    bool is_item_visible(ThumbnailItem* item) const;
    void invalidate_meta_cache(const QString& path);
    const FileMetadata& cached_meta(const QString& path) const;
    void start_drag_selected();
    void update_rubber_selection(const QRect& r);
    void update_rubber_band_visual();

    // ─── Widgety ─────────────────────────────────────────────────────────────
    QWidget*      m_container = nullptr;
    QScrollArea*  m_scroll         = nullptr;
    class ThumbnailCanvas* m_canvas = nullptr;  // nowy canvas - rysuje wszystko
    QLabel*       m_loading_label  = nullptr;
    ThumbWorker*  m_worker    = nullptr;

    // ─── Dane plików ─────────────────────────────────────────────────────────
    QList<ScannedFile>            m_all_files;
    QList<ScannedFile>            m_visible;
    QSet<QString>                 m_selected;
    QString                       m_primary;
    QString                       m_shift_anchor;  // punkt kotwicy Shift+zaznaczenie
    GridFilter                    m_filter;
    int                           m_thumb_size    = 200;
    int                           m_pending_size  = 200;
    QString                       m_current_dir;
    bool                          m_collection_mode = false;
    bool                          m_pending_rebuild = false;
    mutable QMap<QString, FileMetadata> m_meta_cache;
    static constexpr int META_CACHE_MAX = 500;  // max wpisów w cache

    // ─── Wirtualna siatka — stan ─────────────────────────────────────────────
    // m_items zawiera tylko AKTYWNE widgety (widoczne + bufor)
    QMap<QString, ThumbnailItem*> m_items;
    // m_pool zawiera wolne widgety do reużycia (nie delete, recykling)
    QList<ThumbnailItem*>         m_pool;
    int                           m_virt_first_visible_row = -1;
    int                           m_virt_last_visible_row  = -1;

    // ─── Timery ──────────────────────────────────────────────────────────────
    QTimer*       m_zoom_timer    = nullptr;
    QTimer*       m_visible_timer = nullptr;
    QTimer*       m_rubber_timer  = nullptr;
    QTimer*       m_resize_timer  = nullptr;
    QFileSystemWatcher* m_fs_watcher = nullptr;  // obserwuje aktualny folder

    // ─── Stan myszy i drag ───────────────────────────────────────────────────
    QPoint        m_drag_start_pos;
    QString       m_click_path;
    Qt::KeyboardModifiers m_click_mods = Qt::NoModifier;
    bool          m_drag_active   = false;
    bool          m_rename_nav_block = false;  // blokuj nawigację klawiaturową po rename

    // ─── Schowek ─────────────────────────────────────────────────────────────
    bool          m_cut_mode       = false;
    QStringList   m_clipboard_paths;

    // ─── Liczniki ────────────────────────────────────────────────────────────
    int           m_thumbs_loaded = 0;

    // ─── Rubber band selection ───────────────────────────────────────────────
    RubberOverlay* m_overlay        = nullptr;
    QWidget*       m_empty_cover    = nullptr;
    QRect          m_rubber_rect;
    bool           m_rubber_active  = false;
    QPoint         m_rubber_origin;    // w układzie ThumbnailGrid
    QPoint         m_rubber_origin_c;  // w układzie kontenera (uwzględnia scroll)
    QPoint         m_rubber_cur;
    int            m_last_scroll_val = 0;
    QRubberBand*   m_rubber_band    = nullptr;  // nieużywany (kompatybilność)
};

} // namespace LapesEye
