#pragma once
#include <QWidget>
#include <QLabel>
#include <QFuture>
#include "LapesEye/ui/HistogramWidget.h"

namespace LapesEye {

class PreviewImageWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewImageWidget(QWidget* parent = nullptr);
    void set_image(const QImage& img);
    bool has_image() const { return !m_original.isNull(); }
    void reset_transform();

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    QPixmap compute_scaled() const;
    QImage  m_original;
    QPixmap m_scaled;
    float   m_zoom        = 1.0f;
    QPointF m_offset;
    QPoint  m_drag_start;
    QPointF m_offset_at_drag;
    bool    m_dragging    = false;
};

class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);
    void load(const QString& path);
    void invalidate(const QString& path);  // wymuś reload przy następnym load()
    void rename_path(const QString& old_path, const QString& new_path);
    QString current_path() const { return m_current_path; }
    void set_histogram_visible(bool visible);

private:
    PreviewImageWidget* m_view        = nullptr;
    QLabel*             m_info_label  = nullptr;
    HistogramWidget*    m_histogram   = nullptr;
    QFuture<void>       m_future;
    int                 m_load_gen    = 0;
    QString             m_current_path;
};

} // namespace LapesEye
