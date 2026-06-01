#pragma once
#include <QWidget>
#include <QListWidget>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QTimer>
#include <QSet>
#include <QSettings>

namespace LapesEye {

class FolderPanel : public QWidget {
    Q_OBJECT
public:
    explicit FolderPanel(QWidget* parent = nullptr);
    void add_recent(const QString& path);
    void refresh_devices();
    void set_current_path(const QString& path);
    void refresh() { build_list(); }  // odświeżenie po zmianie struktury folderów

signals:
    void folder_selected(const QString& path);
    void collection_selected(const QString& collection_id);
    void collection_edit_requested(const QString& collection_id);
    void collection_delete_requested(const QString& collection_id);
    void folder_open_in_tab(const QString& path);
    void folder_open_in_window(const QString& path);
    void folder_expand_requested(const QString& path);
    void files_moved_to(const QString& dest_dir, const QStringList& sources);

private slots:
    void on_item_clicked(QListWidgetItem* item);
    void on_item_double_clicked(QListWidgetItem* item);
    void on_context_menu_requested(const QPoint& pos);

    // Wywoływane przez DropListWidget
    void handle_drag_move(QDragMoveEvent* e, QListWidgetItem* item);
    void handle_drop(QDropEvent* e, QListWidgetItem* item);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void build_list();
    void add_header(const QString& text);
    void add_toggle_header(const QString& text, const QString& section_id, bool is_open);
    void add_place(const QString& icon, const QString& label,
                   const QString& path, int indent = 0, bool current = false);
    void add_expandable(const QString& icon, const QString& label,
                        const QString& path, int indent = 0, bool current = false);
    void add_children(const QString& path, int indent, const QStringList& skip_names);
    void highlight_current();

    QListWidget*  m_list        = nullptr;
    QString       m_current_path;
    QStringList   m_favorites;
    QStringList   m_recent_dirs;
    QSet<QString> m_expanded;
    bool          m_recent_open  = false;
    bool          m_setting_path = false;  // guard: blokuj folder_selected podczas set_current_path
    QTimer*       m_dev_timer    = nullptr;
    QStringList   m_last_mounts;
    static constexpr int MAX_RECENT = 15;
};

} // namespace LapesEye
