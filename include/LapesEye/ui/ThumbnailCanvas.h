#pragma once

// ── Baza: QOpenGLWidget gdy LEYE_HAS_GL=1, QWidget gdy =0 ───────────────────
// QOpenGLWidget jest child widgetem QScrollArea — brak osobnego okna systemowego.
// Qt zarządza FBO automatycznie. Cały event system (scroll/drag/events) bez zmian.
// Zawsze QWidget jako baza — brak FBO problemów z QScrollArea.
// OpenGL context tworzymy sami przez QOpenGLContext + QOffscreenSurface.
#include <QWidget>
#define TC_BASE QWidget
#if LEYE_HAS_GL
#  include <QOpenGLContext>
#  include <QOpenGLFunctions_4_5_Core>
#  include <QOpenGLVersionFunctionsFactory>
#  include <QOpenGLShaderProgram>
#  include <QOffscreenSurface>
#  include <QOpenGLFramebufferObject>
#endif

#include <QPixmap>
#include <QSet>
#include <QHash>
#include <QLineEdit>
#include <QTimer>
#include <QFileInfo>
#include <QMatrix4x4>
#include "LapesEye/core/FileScanner.h"
#include "LapesEye/core/MetaStore.h"
#include "LapesEye/ui/ColorLabelEditor.h"

namespace LapesEye {

static constexpr int TC_PAD    = 6;
static constexpr int TC_NAME_H = 16;
static constexpr int TC_STARS_H= 12;

struct ThumbnailCanvasItem {
    ScannedFile  file;
    QPixmap      thumb;
    FileMetadata meta;
    bool         meta_loaded = false;
};

class ThumbnailCanvas : public TC_BASE {
    Q_OBJECT
public:
    explicit ThumbnailCanvas(QWidget* parent = nullptr);
    ~ThumbnailCanvas() override;

    void set_items(QVector<ThumbnailCanvasItem> items);
    void set_thumb_size(int size);
    void set_selected(const QSet<QString>& selected);
    void set_cut_paths(const QSet<QString>& cut);
    void set_drag_active(bool active);
    void set_pixmap(const QString& path, const QPixmap& pix);
    void set_pixmap_no_update(const QString& path, const QPixmap& pix);
    void set_metadata(const QString& path, const FileMetadata& meta);
    void rename_item(const QString& old_path, const QString& new_path, const QString& new_name);
    void rename_pixmap_in_store(const QString& old_path, const QString& new_path);
    void clear_pixmap_store() { m_pixmap_store.clear(); }

    const QVector<ThumbnailCanvasItem>& items() const { return m_items; }

    int  cols() const;
    int  cell_size() const;
    int  cell_width() const;
    int  total_height() const;
    int  gap() const { return 4; }
    void notify_scroll_start() {
        m_is_scrolling = true;
        if (m_scroll_end_timer) m_scroll_end_timer->start();
    }
    QRect item_rect(int idx) const;
    int   index_at(const QPoint& pos) const;

    void start_rename(int idx);
    void cancel_rename();
    bool is_renaming() const { return m_rename_idx >= 0; }
    QLineEdit* rename_edit() const { return m_rename_edit; }

signals:
    void clicked(const QString& path, Qt::KeyboardModifiers mods);
    void press_in_empty(const QPoint& global_pos);
    void rubber_move(const QPoint& global_pos);
    void double_clicked(const QString& path);
    void context_menu_requested(const QString& path, const QPoint& global_pos);
    void drag_move(const QPoint& press_global, const QPoint& cur_global);
    void drag_released(const QPoint& global_pos);
    void rename_requested(const QString& old_path, const QString& new_name);
    void hovered(const QString& path);

protected:
    // QPainter fallback (LEYE_HAS_GL=0) lub nadpisany gdy GL aktywny
    void paintEvent(QPaintEvent* e) override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

#if LEYE_HAS_GL
    void gl_init();
    void gl_resize(int w, int h);
    void gl_paint();
    void gl_paint_region(QOpenGLFunctions_4_5_Core* f, const QRect& clip);
#endif

private:
    // ── QPainter rendering (fallback) ────────────────────────────────────────
    void draw_item(QPainter& p, int idx, const QRect& r);
    void draw_stars(QPainter& p, const QRect& r, int rating);
    void draw_badge(QPainter& p, const QRect& img_rect, const QString& text, const QColor& color);

    // ── Dane ─────────────────────────────────────────────────────────────────
    QVector<ThumbnailCanvasItem> m_items;
    QHash<QString, QPixmap>      m_pixmap_store;
    QSet<QString>  m_selected;
    QSet<QString>  m_cut;
    int            m_thumb_size  = 160;
    bool           m_drag_active = false;
    int            m_hovered_idx = -1;
    QPoint         m_press_global;
    int            m_press_idx   = -1;

    QLineEdit* m_rename_edit      = nullptr;
    int        m_rename_idx       = -1;
    bool       m_rename_committed = false;
    bool       m_is_scrolling     = false;
    QTimer*    m_scroll_end_timer = nullptr;

#if LEYE_HAS_GL
    // ── OpenGL 4.5 DSA GPU renderer ──────────────────────────────────────────
    using GL45 = QOpenGLFunctions_4_5_Core;
    GL45* gl() const;

    bool init_shader();
    void init_vao();
    void gl_draw_item(GL45* f, int idx);
    void gl_draw_quad_color(GL45* f, float x, float y, float w, float h,
                             float r, float g, float b, float a = 1.f);
    void gl_draw_quad_tex(GL45* f, float x, float y, float w, float h,
                           GLuint tex, float alpha = 1.f);

    struct GpuEntry {
        GLuint  thumb_id     = 0;   // tekstura miniatury w VRAM
        GLuint  overlay_id   = 0;   // tekstura overlaya (tekst/gwiazdki/badge)
        bool    thumb_dirty  = true;
        bool    overlay_dirty= true;
        QSize   thumb_src;          // rozmiar przy ostatnim upload
        int     ov_w = 0, ov_h = 0;// rozmiar overlaya przy ostatnim render
    };
    QHash<QString, GpuEntry> m_gpu;

    void gpu_upload_thumb(GpuEntry& e, const QPixmap& pix);
    void gpu_render_overlay(const QString& path, GpuEntry& e, int idx, int cw, int ch);
    void gpu_delete_entry(GpuEntry& e);
    void gpu_delete_all();

    QOpenGLContext*       m_gl_ctx  = nullptr;
    QOffscreenSurface*    m_gl_surf = nullptr;
    QSize                 m_last_size;
    QOpenGLShaderProgram* m_prog    = nullptr;
    GLuint                m_vao     = 0;
    GLuint                m_vbo     = 0;
    bool                  m_gl_ok   = false;
    int  m_u_mvp     = -1;
    int  m_u_color   = -1;
    int  m_u_use_tex = -1;
    int  m_u_alpha   = -1;
    QMatrix4x4 m_proj;
#endif
};

} // namespace LapesEye
