#include "LapesEye/workers/ThumbWorker.h"
#include "LapesEye/core/ThumbCache.h"
#include "LapesEye/core/FileScanner.h"

#include <QThreadPool>
#include <QPixmapCache>
#include <QImageReader>
#include <QImage>
#include <QBuffer>

// Unsharp Mask dla miniatur — wyostrzenie po przeskalowaniu
static QImage usm_thumb(const QImage& input, float amount = 0.5f) {
    if (input.width() < 4 || input.height() < 4) return input;
    QImage src = input.convertToFormat(QImage::Format_RGB32);
    QImage blurred = src.copy();
    const int w = src.width(), h = src.height();
    for (int y = 1; y < h-1; ++y) {
        const auto* r0 = reinterpret_cast<const QRgb*>(src.scanLine(y-1));
        const auto* r1 = reinterpret_cast<const QRgb*>(src.scanLine(y));
        const auto* r2 = reinterpret_cast<const QRgb*>(src.scanLine(y+1));
        auto* out = reinterpret_cast<QRgb*>(blurred.scanLine(y));
        for (int x = 1; x < w-1; ++x) {
            out[x] = qRgb(
                (qRed(r0[x-1])+qRed(r0[x])+qRed(r0[x+1])+qRed(r1[x-1])+qRed(r1[x])+qRed(r1[x+1])+qRed(r2[x-1])+qRed(r2[x])+qRed(r2[x+1]))/9,
                (qGreen(r0[x-1])+qGreen(r0[x])+qGreen(r0[x+1])+qGreen(r1[x-1])+qGreen(r1[x])+qGreen(r1[x+1])+qGreen(r2[x-1])+qGreen(r2[x])+qGreen(r2[x+1]))/9,
                (qBlue(r0[x-1])+qBlue(r0[x])+qBlue(r0[x+1])+qBlue(r1[x-1])+qBlue(r1[x])+qBlue(r1[x+1])+qBlue(r2[x-1])+qBlue(r2[x])+qBlue(r2[x+1]))/9
            );
        }
    }
    QImage result = src.copy();
    for (int y = 0; y < h; ++y) {
        const auto* s  = reinterpret_cast<const QRgb*>(src.scanLine(y));
        const auto* bl = reinterpret_cast<const QRgb*>(blurred.scanLine(y));
        auto* out = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < w; ++x)
            out[x] = qRgb(
                qBound(0,(int)(qRed(s[x])  +amount*(qRed(s[x])  -qRed(bl[x]))),  255),
                qBound(0,(int)(qGreen(s[x])+amount*(qGreen(s[x])-qGreen(bl[x]))),255),
                qBound(0,(int)(qBlue(s[x]) +amount*(qBlue(s[x]) -qBlue(bl[x]))), 255));
    }
    return result;
}
#include <QByteArray>
#include <QPainter>
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <libraw/libraw.h>

namespace LapesEye {

// ─── ThumbJob ─────────────────────────────────────────────────────────────────

ThumbJob::ThumbJob(const QString& path, int thumb_size,
                   ThumbCache* cache, ThumbQuality quality, int generation)
    : m_path(path), m_size(thumb_size), m_cache(cache), m_quality(quality), m_generation(generation)
{
    setAutoDelete(true);
}

void ThumbJob::run() {
    bool full = (m_quality == ThumbQuality::Full);
    QImage result;

    // Sprawdź SQLite cache w wątku tła — nie blokuje UI
    if (!full && m_cache) {
        auto cached = m_cache->get(m_path);
        if (cached.has_value()) {
            emit image_ready(m_path, *cached, ThumbQuality::CacheHit, m_generation);
            return;
        }
    }

    if (FileScanner::is_raw(m_path))
        result = generate_raw(m_path, m_size, full);
    else
        result = generate_raster(m_path, m_size, full);

    if (result.isNull())
        emit image_failed(m_path);
    else
        emit image_ready(m_path, result, m_quality, m_generation);
}

QImage ThumbJob::generate_raster(const QString& path, int size, bool full_quality) {
    QImageReader reader(path);
    reader.setAutoTransform(true);

    QSize orig = reader.size();
    if (!orig.isValid() || orig.width() <= 0) return {};

    // Fast: decode 2× rozmiar — szybko
    // Full: decode do 2400px max — wyraźna przy każdym powiększeniu suwaka
    int decode_max = full_quality ? qMin(size * 6, 2400) : size * 2;
    QSize scaled = orig.scaled(decode_max, decode_max, Qt::KeepAspectRatio);
    reader.setScaledSize(scaled);

    QImage img = reader.read();
    if (img.isNull()) return {};

    Qt::TransformationMode mode = full_quality
        ? Qt::SmoothTransformation : Qt::FastTransformation;

    if (img.width() > size || img.height() > size)
        img = img.scaled(size, size, Qt::KeepAspectRatio, mode);

    // Unsharp Mask dla pełnej jakości — widoczne przy dużym zoom suwaka
    if (full_quality)
        img = usm_thumb(img, 0.5f);

    return img;
}

QImage ThumbJob::generate_raw(const QString& path, int size, bool full_quality) {
    LibRaw raw;
    if (raw.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
        return {};

    // Zawsze najpierw próbuj embedded JPEG (szybko, niezależnie od jakości)
    if (raw.unpack_thumb() == LIBRAW_SUCCESS) {
        libraw_processed_image_t* thumb = raw.dcraw_make_mem_thumb();
        if (thumb && thumb->type == LIBRAW_IMAGE_JPEG) {
            QByteArray jpeg_data(reinterpret_cast<const char*>(thumb->data),
                                 static_cast<int>(thumb->data_size));
            LibRaw::dcraw_clear_mem(thumb);
            // Użyj QImageReader żeby setAutoTransform zastosowało rotację EXIF
            QBuffer buf(&jpeg_data);
            buf.open(QIODevice::ReadOnly);
            QImageReader reader(&buf, "JPEG");
            reader.setAutoTransform(true);
            QImage img = reader.read();
            if (!img.isNull()) {
                Qt::TransformationMode mode = full_quality
                    ? Qt::SmoothTransformation : Qt::FastTransformation;
                return img.scaled(size, size, Qt::KeepAspectRatio, mode);
            }
        } else if (thumb) {
            LibRaw::dcraw_clear_mem(thumb);
        }
    }

    // Fallback: pełny decode tylko dla full quality
    if (!full_quality) return {};

    raw.imgdata.params.half_size   = 1;
    raw.imgdata.params.use_auto_wb = 1;
    raw.imgdata.params.output_bps  = 8;
    if (raw.unpack()        != LIBRAW_SUCCESS) return {};
    if (raw.dcraw_process() != LIBRAW_SUCCESS) return {};

    libraw_processed_image_t* img_data = raw.dcraw_make_mem_image();
    if (!img_data) return {};

    QImage result(img_data->width, img_data->height, QImage::Format_RGB888);
    memcpy(result.bits(), img_data->data,
           static_cast<size_t>(img_data->width) * img_data->height * 3);
    LibRaw::dcraw_clear_mem(img_data);

    return result.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// ─── ThumbWorker ──────────────────────────────────────────────────────────────

ThumbWorker::ThumbWorker(ThumbCache* cache, QObject* parent)
    : QObject(parent), m_cache(cache)
{
    // Zostaw 1 wątek dla UI, resztę dla miniatur — dostosuj się do CPU
    int threads = qMax(1, QThread::idealThreadCount() - 1);
    QThreadPool::globalInstance()->setMaxThreadCount(threads);
    // Duży bufor zadań — nie ograniczaj kolejki
    QThreadPool::globalInstance()->setExpiryTimeout(-1);
}

ThumbWorker::~ThumbWorker() {
    cancel_all();
    QThreadPool::globalInstance()->waitForDone(3000);
}

void ThumbWorker::request(const QString& path, int priority) {
    if (m_pending_fast.contains(path)) return;

    // Obsługa miniatur folderów — kolaż pierwszych 4 zdjęć
    if (QFileInfo(path).isDir()) {
        QString cache_key = path + "@dir@" + QString::number(m_size);
        QPixmap ram_pix;
        if (QPixmapCache::find(cache_key, &ram_pix)) {
            emit thumb_ready(path, ram_pix, ThumbQuality::Full);
            return;
        }
        m_pending_fast.insert(path);
        int sz = m_size;
        auto fut = QtConcurrent::run([this, path, sz, cache_key]() {
            QStringList images;
            QDir dir(path);
            const QStringList filters = {"*.jpg","*.jpeg","*.png","*.tif","*.tiff","*.webp","*.bmp"};
            for (const QString& f : dir.entryList(filters, QDir::Files, QDir::Name)) {
                images << dir.filePath(f);
                if (images.size() >= 4) break;
            }
            QImage canvas(sz, sz, QImage::Format_RGB32);
            canvas.fill(QColor(0x2a, 0x2a, 0x2a));
            if (!images.isEmpty()) {
                QPainter p(&canvas);
                int n = qMin(images.size(), 4);
                int half = sz / 2;
                for (int i = 0; i < n; ++i) {
                    QImageReader reader(images[i]);
                    reader.setAutoTransform(true);
                    QSize s = reader.size().scaled(half, half, Qt::KeepAspectRatioByExpanding);
                    reader.setScaledSize(s);
                    QImage img = reader.read();
                    if (img.isNull()) continue;
                    img = img.scaled(half, half, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
                    // Przytnij do half×half
                    int ox = (img.width() - half) / 2;
                    int oy = (img.height() - half) / 2;
                    img = img.copy(ox, oy, half, half);
                    int cx = (i % 2) * half;
                    int cy = (i / 2) * half;
                    p.drawImage(cx, cy, img);
                }
            }
            QPixmap pix = QPixmap::fromImage(canvas);
            QPixmapCache::insert(cache_key, pix);
            QMetaObject::invokeMethod(this, [this, path, pix]() {
                m_pending_fast.remove(path);
                emit thumb_ready(path, pix, ThumbQuality::Full);
            }, Qt::QueuedConnection);
        });
        Q_UNUSED(fut);
        return;
    }

    // 1. Sprawdź RAM cache (QPixmapCache) — natychmiastowe, bez I/O
    QString cache_key = path + "@" + QString::number(m_size);
    QPixmap ram_pix;
    if (QPixmapCache::find(cache_key, &ram_pix)) {
        emit thumb_ready(path, ram_pix, ThumbQuality::Full);
        return;
    }

    // 2. SQLite + ewentualny decode z dysku — wszystko w wątku tła
    // NIE sprawdzaj SQLite na UI wątku — dla 20 miniatur × ~40ms = 800ms blokada
    m_pending_fast.insert(path);
    auto* job = new ThumbJob(path, m_size, m_cache, ThumbQuality::Fast, m_gen);
    QObject::connect(job, &ThumbJob::image_ready,
                     this, &ThumbWorker::on_image_ready, Qt::QueuedConnection);
    QObject::connect(job, &ThumbJob::image_failed,
                     this, &ThumbWorker::on_image_failed, Qt::QueuedConnection);
    QThreadPool::globalInstance()->start(job, priority);
}

void ThumbWorker::request_full_quality(const QString& path) {
    if (m_pending_full.contains(path)) return;
    m_pending_full.insert(path);
    auto* job = new ThumbJob(path, m_size, m_cache, ThumbQuality::Full, m_gen);
    QObject::connect(job, &ThumbJob::image_ready,
                     this, &ThumbWorker::on_image_ready, Qt::QueuedConnection);
    QObject::connect(job, &ThumbJob::image_failed,
                     this, &ThumbWorker::on_image_failed, Qt::QueuedConnection);
    QThreadPool::globalInstance()->start(job, 1);  // Full: niższy priorytet
}

void ThumbWorker::on_image_ready(const QString& path, const QImage& image,
                                   ThumbQuality quality, int generation)
{
    // Ignoruj wyniki starych generacji (po cancel_all/do_zoom_rebuild)
    if (generation != m_gen) return;

    QString cache_key = path + "@" + QString::number(m_size);
    QPixmap pix = QPixmap::fromImage(image);
    if (pix.isNull()) return;

    if (quality == ThumbQuality::CacheHit) {
        m_pending_fast.remove(path);
        QPixmapCache::insert(cache_key, pix);
        emit thumb_ready(path, pix, ThumbQuality::Fast);
        request_full_quality(path);

    } else if (quality == ThumbQuality::Fast) {
        m_pending_fast.remove(path);
        if (m_cache) {
            QImageReader r(path);
            auto orig = r.size();
            m_cache->put(path, image, orig.width(), orig.height());
        }
        QPixmapCache::insert(cache_key, pix);
        emit thumb_ready(path, pix, ThumbQuality::Fast);
        request_full_quality(path);

    } else {  // Full
        m_pending_full.remove(path);
        if (m_cache) {
            QImageReader r(path);
            auto orig = r.size();
            m_cache->put(path, image, orig.width(), orig.height());
        }
        QPixmapCache::insert(cache_key, pix);
        emit thumb_ready(path, pix, ThumbQuality::Full);
    }
}

void ThumbWorker::on_image_failed(const QString& path) {
    m_pending_fast.remove(path);
    m_pending_full.remove(path);
}

void ThumbWorker::cancel_all() {
    ++m_gen;  // stare joby będą ignorowane gdy wrócą
    QThreadPool::globalInstance()->clear();
    m_pending_fast.clear();
    m_pending_full.clear();
}

void ThumbWorker::rename_cache(const QString& old_path, const QString& new_path) {
    // Aktualizuj SQLite cache pod nową ścieżką
    if (m_cache) m_cache->rename_entry(old_path, new_path);
    // Aktualizuj QPixmapCache (RAM)
    QPixmap pix;
    QString old_key = "thumb:" + old_path;
    QString new_key = "thumb:" + new_path;
    if (QPixmapCache::find(old_key, &pix)) {
        QPixmapCache::remove(old_key);
        QPixmapCache::insert(new_key, pix);
    }
}

} // namespace LapesEye
