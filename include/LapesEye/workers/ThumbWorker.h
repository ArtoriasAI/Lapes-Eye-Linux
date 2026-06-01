#pragma once
#include <QObject>
#include <QRunnable>
#include <QString>
#include <QPixmap>
#include <QImage>
#include <QSet>
#include <QThread>

namespace LapesEye {

class ThumbCache;

// Dwa poziomy jakości miniatury
enum class ThumbQuality { Fast, Full, CacheHit };

class ThumbJob : public QObject, public QRunnable {
    Q_OBJECT
public:
    ThumbJob(const QString& path, int thumb_size,
             ThumbCache* cache, ThumbQuality quality, int generation);
    void run() override;

signals:
    void image_ready(const QString& path, const QImage& image, ThumbQuality quality, int generation);
    void image_failed(const QString& path);

private:
    QImage generate_raster(const QString& path, int size, bool full_quality);
    QImage generate_raw(const QString& path, int size, bool full_quality);

    QString      m_path;
    int          m_size;
    ThumbCache*  m_cache;
    ThumbQuality m_quality;
    int          m_generation;
};

class ThumbWorker : public QObject {
    Q_OBJECT
public:
    explicit ThumbWorker(ThumbCache* cache, QObject* parent = nullptr);
    ThumbCache* cache() const { return m_cache; }
    ~ThumbWorker();

    // Żąda miniatury — najpierw szybka, potem pełna jakość w tle
    void request(const QString& path, int priority = 2);
    void request_full_quality(const QString& path);
    void cancel_all();
    void set_thumb_size(int size) { m_size = size; }
    void rename_cache(const QString& old_path, const QString& new_path);
    int  thumb_size() const { return m_size; }

signals:
    void thumb_ready(const QString& path, const QPixmap& pixmap, ThumbQuality quality);

private slots:
    void on_image_ready(const QString& path, const QImage& image, ThumbQuality quality, int generation);
    void on_image_failed(const QString& path);

private:
    ThumbCache*   m_cache;
    int           m_size = 256;
    QSet<QString> m_pending_fast;
    QSet<QString> m_pending_full;
    int           m_gen = 0;  // generation counter — rośnie przy cancel_all()
};

} // namespace LapesEye
