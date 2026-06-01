#pragma once
#include <QWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStringList>
#include <QPixmap>
#include <QTimer>
#include <QTime>
#include <QFuture>

namespace LapesEye {

class FullscreenViewer : public QWidget {
    Q_OBJECT
public:
    explicit FullscreenViewer(QWidget* parent = nullptr);
    void show_image(const QStringList& paths, int index);
    int              current_index() const { return m_index; }
    const QStringList& paths()        const { return m_paths;  }

signals:
    void closed();
    void index_changed(int index);  // emitowany przy nawigacji (punkt 5)

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void     navigate(int delta);
    void     load_current();
    void     draw_overlay(QPainter& p);
    QSizeF   base_image_size() const;
    QPointF  screen_to_image(const QPointF& screen_pt) const;
    void     zoom_at(double new_zoom, const QPointF& focus_screen);

    QStringList   m_paths;
    int           m_index  = 0;
    QPixmap       m_pixmap;
    QPixmap       m_loading_pixmap;
    bool          m_loading   = false;
    QFuture<void> m_future;
    int           m_load_gen  = 0;  // anuluj stare żądania

    // Zoom / pan — zachowywane między zdjęciami (punkt 4)
    double   m_zoom      = 1.0;
    QPointF  m_offset    = {0, 0};
    bool     m_is_zoomed = false;  // czy użytkownik powiększył (punkt 2)

    // Pan
    bool    m_panning      = false;
    QPoint  m_pan_start;
    QPointF m_offset_start;
    QPoint  m_press_pos;
    QTime   m_press_time;

    // Overlay
    QTimer* m_overlay_timer = nullptr;
    bool    m_show_overlay  = true;
};

} // namespace LapesEye
