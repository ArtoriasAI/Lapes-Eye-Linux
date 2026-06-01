#pragma once
#include "LapesEye/core/FileScanner.h"
#include "LapesEye/core/MetaStore.h"

#include <QFrame>
#include <QPixmap>
#include <QLineEdit>

namespace LapesEye {

class ThumbnailItem : public QFrame {
    Q_OBJECT

public:
    explicit ThumbnailItem(const ScannedFile& file, QWidget* parent = nullptr);

    const QString&    path() const  { return m_file.path; }
    const ScannedFile& file() const { return m_file; }

    // Globalna flaga — gdy drag trwa, items nie rysują niebieskiego tła zaznaczenia
    static void set_drag_active(bool active);
    static bool s_drag_active;
    void set_file(const ScannedFile& f) { m_file = f; update(); }

    void set_pixmap(const QPixmap& pix);
    const QPixmap& pixmap() const { return m_thumb; }
    void set_metadata(const FileMetadata& meta);
    bool is_renaming() const { return m_rename_edit != nullptr; }
    void set_selected(bool selected);
    void set_cut_mode(bool cut);    // ściemnij przy wycinaniu
    void set_thumb_size(int size);
    int  thumb_size() const { return m_thumb_size; }
    bool is_selected() const { return m_selected; }

    // Uruchom tryb edycji nazwy inline
    void start_rename();

signals:
    void clicked(const QString& path, Qt::KeyboardModifiers mods);
    void double_clicked(const QString& path);
    void context_menu_requested(const QString& path, const QPoint& global_pos);
    void rename_requested(const QString& old_path, const QString& new_name);
    void drag_move(const QPoint& press_global, const QPoint& cur_global);
    void drag_released(const QPoint& global_pos);  // zakończenie drag/rubber band

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void draw_stars(QPainter& p, const QRect& rect, int rating);
    void draw_badge(QPainter& p, const QRect& rect,
                    const QString& text, const QColor& bg);
    void finish_rename(bool accept);
    void update_rename_geometry();
    static QColor label_color(ColorLabel c);

    ScannedFile    m_file;
    FileMetadata   m_meta;
    QPixmap        m_thumb;
    bool           m_selected   = false;
    bool           m_cut        = false;
    bool           m_hovered         = false;
    bool           m_rename_committed = false;
    QPoint         m_press_global;              // globalPos przy MousePress
    bool           m_meta_loaded = false;
    int            m_thumb_size  = 200;

    // Inline rename
    QLineEdit*     m_rename_edit = nullptr;
};

} // namespace LapesEye
