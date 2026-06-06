#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QFuture>
#include "LapesEye/core/MetaStore.h"
#include "LapesEye/ui/BreadcrumbBar.h"
#include <QSlider>
#include <QTabWidget>
#include <QAction>
#include <QDockWidget>
#include <memory>
#include "LapesEye/core/MetaStore.h"
#include "LapesEye/ui/CompareView.h"
#include "LapesEye/ui/FullscreenViewer.h"

namespace LapesEye {

class FolderPanel;
class ThumbnailGrid;
class PreviewPanel;
class MetaPanel;
class FilterBar;
class ThumbWorker;
class ThumbCache;
class LapeIPC;

struct TabHistory {
    QStringList back;
    QStringList forward;
    QString     current;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void open_folder(const QString& path);
    void select_file(const QString& path);  // zaznacz plik w bieżącej siatce
    void open_folder_in_new_tab(const QString& path, bool switch_to = true);
    static void open_folder_in_new_window(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void on_folder_selected(const QString& path);
    void on_folder_in_new_tab(const QString& path);
    void on_folder_in_new_window(const QString& path);
    void on_primary_changed(const QString& path);
    void on_selection_changed(const QStringList& paths);
    void on_open_in_lape(const QString& path);
    void open_in_external_editor(const QStringList& paths, bool as_layer = false);
    void on_open_as_layer(const QString& path);
    void on_context_menu(const QStringList& paths, const QPoint& pos);
    void on_context_menu_background(const QPoint& pos);
    void on_ipc_status_changed();
    void on_tab_changed(int index);
    void on_tab_close_requested(int index);
    void on_view_mode_changed(CompareMode mode);

    void action_open_folder();
    void action_new_tab();
    void action_close_tab();
    void action_select_all();
    void action_batch_rename();
    void action_purge_cache();
    void action_about();
    void action_file_info(const QString& path);
    void action_advanced_search();
    void action_export();
    void action_print();
    bool eventFilter(QObject* obj, QEvent* event) override;
    void action_rate(int stars);
    void action_label(ColorLabel label);
    void action_new_folder();
    void action_go_back();
    void action_go_forward();

private:
    void setup_panels();
    void setup_tabs();
    void setup_menu();
    void setup_toolbar();
    void setup_statusbar();
    void save_settings();
    void load_settings();
    void update_ipc_indicator();
    void update_tab_title(int index, const QString& path);
    void load_meta_async(const QString& path);  // ładuje metadane w wątku tła
    void update_nav_buttons();
    void update_compare_view();

    void navigate_to(const QString& path, bool add_to_history = true);

    ThumbnailGrid* current_grid() const;
    ThumbnailGrid* add_tab(const QString& path = {});
    TabHistory&    current_history();

    // ── Panele ───────────────────────────────────────────────────────────────
    QTabWidget*     m_tabs          = nullptr;
    FolderPanel*    m_folder_panel  = nullptr;
    PreviewPanel*       m_preview_panel     = nullptr;
    FullscreenViewer*   m_fullscreen_viewer = nullptr;
    CompareView*    m_compare_view  = nullptr;   // tryb porównania
    QDockWidget*    m_preview_dock  = nullptr;
    QDockWidget*    m_left_dock     = nullptr;
    QDockWidget*    m_meta_dock     = nullptr;
    MetaPanel*      m_meta_panel    = nullptr;
    QFuture<FileMetadata> m_meta_future;
    int             m_meta_gen      = 0;   // generation counter dla async meta
    FilterBar*      m_filter_bar    = nullptr;

    CompareMode     m_view_mode     = CompareMode::Single;

    // ── Serwisy ───────────────────────────────────────────────────────────────
    std::unique_ptr<ThumbCache>  m_thumb_cache;
    std::unique_ptr<ThumbWorker> m_thumb_worker;
    std::unique_ptr<LapeIPC>     m_ipc;

    // ── Toolbar ───────────────────────────────────────────────────────────────
    QAction* m_act_back    = nullptr;
    QAction* m_act_forward = nullptr;
    QSlider* m_zoom_slider = nullptr;

    // ── Statusbar ─────────────────────────────────────────────────────────────
    QLabel*     m_status_count  = nullptr;
    QLineEdit*    m_address_bar   = nullptr;  // legacy
    // m_open_action usunięty — Enter obsługuje ThumbnailGrid::keyPressEvent
    BreadcrumbBar* m_breadcrumb    = nullptr;
    QLabel*  m_status_path   = nullptr;
    QLabel*  m_ipc_indicator = nullptr;

    // ── Historia nawigacji ────────────────────────────────────────────────────
    QMap<int, TabHistory> m_history;
    int  m_tab_id_counter = 0;

    QString m_current_dir;
    bool    m_navigating           = false;
    bool    m_rename_just_finished = false;  // blokuj nawigację tuż po rename
    QString m_current_collection_id;  // pusty gdy nie jesteśmy w kolekcji
};

} // namespace LapesEye
