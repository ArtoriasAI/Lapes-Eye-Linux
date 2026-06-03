#include "LapesEye/ui/PreviewPanel.h"
#include "LapesEye/core/ColorManagement.h"
#include "LapesEye/core/MetaStore.h"
#include <QTransform>
#include "LapesEye/core/FileScanner.h"
#include <QVBoxLayout>
#include <QImageReader>
#include <QPixmapCache>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QBuffer>
#include <QtConcurrent/QtConcurrent>
#include <libraw/libraw.h>

namespace LapesEye {

// ─── Unsharp Mask ─────────────────────────────────────────────────────────────
// Algorytm: USM = original + amount * (original - blur(original))
// amount=0.6, radius=1px blur — subtelne wyostrzenie bez artefaktów
static QImage apply_unsharp_mask(const QImage& input, float amount = 0.6f) {
    if (input.width() < 4 || input.height() < 4) return input;
    QImage src = input.convertToFormat(QImage::Format_RGB32);

    // Rozmycie 3×3 box blur (aproksymacja Gaussa, szybkie)
    QImage blurred = src.copy();
    const int w = src.width(), h = src.height();
    for (int y = 1; y < h - 1; ++y) {
        const auto* r0 = reinterpret_cast<const QRgb*>(src.scanLine(y - 1));
        const auto* r1 = reinterpret_cast<const QRgb*>(src.scanLine(y));
        const auto* r2 = reinterpret_cast<const QRgb*>(src.scanLine(y + 1));
        auto* out = reinterpret_cast<QRgb*>(blurred.scanLine(y));
        for (int x = 1; x < w - 1; ++x) {
            // 3×3 box blur
            int r = (qRed(r0[x-1])+qRed(r0[x])+qRed(r0[x+1]) +
                     qRed(r1[x-1])+qRed(r1[x])+qRed(r1[x+1]) +
                     qRed(r2[x-1])+qRed(r2[x])+qRed(r2[x+1])) / 9;
            int g = (qGreen(r0[x-1])+qGreen(r0[x])+qGreen(r0[x+1]) +
                     qGreen(r1[x-1])+qGreen(r1[x])+qGreen(r1[x+1]) +
                     qGreen(r2[x-1])+qGreen(r2[x])+qGreen(r2[x+1])) / 9;
            int b = (qBlue(r0[x-1])+qBlue(r0[x])+qBlue(r0[x+1]) +
                     qBlue(r1[x-1])+qBlue(r1[x])+qBlue(r1[x+1]) +
                     qBlue(r2[x-1])+qBlue(r2[x])+qBlue(r2[x+1])) / 9;
            out[x] = qRgb(r, g, b);
        }
    }

    // USM: result = src + amount * (src - blur)
    QImage result = src.copy();
    for (int y = 0; y < h; ++y) {
        const auto* s = reinterpret_cast<const QRgb*>(src.scanLine(y));
        const auto* bl = reinterpret_cast<const QRgb*>(blurred.scanLine(y));
        auto* out = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = qBound(0, (int)(qRed(s[x])   + amount * (qRed(s[x])   - qRed(bl[x]))),   255);
            int g = qBound(0, (int)(qGreen(s[x]) + amount * (qGreen(s[x]) - qGreen(bl[x]))), 255);
            int b = qBound(0, (int)(qBlue(s[x])  + amount * (qBlue(s[x])  - qBlue(bl[x]))),  255);
            out[x] = qRgb(r, g, b);
        }
    }
    return result;
}

// ════════════════════════════════════════════════════════════════════════════
// PreviewImageWidget
// ════════════════════════════════════════════════════════════════════════════

PreviewImageWidget::PreviewImageWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(200, 180);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PreviewImageWidget::set_image(const QImage& img) {
    bool reset = (m_original.size() != img.size());
    m_original = img;
    if (reset || img.isNull()) { m_zoom = 1.0f; m_offset = {}; }
    m_scaled = compute_scaled();
    update();
}

QPixmap PreviewImageWidget::compute_scaled() const {
    if (m_original.isNull() || width() <= 0 || height() <= 0) return {};
    float fit = qMin((float)width()  / m_original.width(),
                     (float)height() / m_original.height());
    int sw = qBound(1, (int)(m_original.width()  * fit * m_zoom), 8192);
    int sh = qBound(1, (int)(m_original.height() * fit * m_zoom), 8192);
    QImage scaled = m_original.scaled(sw, sh, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // USM po przeskalowaniu — mocniejszy efekt na małym obrazie (amount=1.2)
    scaled = apply_unsharp_mask(scaled, 1.2f);
    return QPixmap::fromImage(scaled);
}

void PreviewImageWidget::reset_transform() {
    m_zoom = 1.0f; m_offset = {};
    m_scaled = compute_scaled();
    update();
}

void PreviewImageWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1e, 0x1e, 0x1e));
    if (m_scaled.isNull()) return;
    int x = (width()  - m_scaled.width())  / 2 + (int)m_offset.x();
    int y = (height() - m_scaled.height()) / 2 + (int)m_offset.y();
    p.drawPixmap(x, y, m_scaled);
}

void PreviewImageWidget::resizeEvent(QResizeEvent*) {
    if (!m_original.isNull()) m_scaled = compute_scaled();
}

void PreviewImageWidget::wheelEvent(QWheelEvent* e) {
    if (m_original.isNull()) return;
    float nz = qBound(0.1f, m_zoom * ((e->angleDelta().y() > 0) ? 1.25f : 0.8f), 32.0f);
    QPointF c(width()/2.0f + m_offset.x(), height()/2.0f + m_offset.y());
    m_offset += (e->position() - c) * (nz / m_zoom - 1.0f);
    m_zoom = nz; m_scaled = compute_scaled(); update(); e->accept();
}

void PreviewImageWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragging = true; m_drag_start = e->pos();
        m_offset_at_drag = m_offset; setCursor(Qt::ClosedHandCursor);
    }
}

void PreviewImageWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging) { m_offset = m_offset_at_drag + QPointF(e->pos() - m_drag_start); update(); }
}

void PreviewImageWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) { m_dragging = false; setCursor(Qt::ArrowCursor); }
}

void PreviewImageWidget::mouseDoubleClickEvent(QMouseEvent*) { reset_transform(); }

// ════════════════════════════════════════════════════════════════════════════
// PreviewPanel
// ════════════════════════════════════════════════════════════════════════════

PreviewPanel::PreviewPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(2);

    m_view = new PreviewImageWidget(this);

    m_info_label = new QLabel(this);
    m_info_label->setAlignment(Qt::AlignCenter);
    m_info_label->setWordWrap(true);
    m_info_label->setStyleSheet("color: #999; font-size: 10px; padding: 0 4px;");

    m_histogram = new HistogramWidget(this);

    layout->addWidget(m_view, 1);
    layout->addWidget(m_info_label);
    layout->addWidget(m_histogram);
}

void PreviewPanel::set_histogram_visible(bool visible) { m_histogram->setVisible(visible); }

void PreviewPanel::rename_path(const QString& old_path, const QString& new_path) {
    QPixmap pix;
    bool had = QPixmapCache::find("preview:" + old_path, &pix);
    if (had) { QPixmapCache::remove("preview:" + old_path); QPixmapCache::insert("preview:" + new_path, pix); }
    if (m_current_path == old_path) m_current_path = new_path;
}

// Ładuje embedded JPEG z RAW — szybkie (~50ms), dobra jakość
static QImage load_raw_preview(const QString& path, int target) {
    LibRaw raw;
    if (raw.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
        return {};

    // Embedded JPEG thumbnail — najszybsza metoda
    if (raw.unpack_thumb() == LIBRAW_SUCCESS) {
        libraw_processed_image_t* thumb = raw.dcraw_make_mem_thumb();
        if (thumb && thumb->type == LIBRAW_IMAGE_JPEG) {
            QByteArray data(reinterpret_cast<const char*>(thumb->data),
                            static_cast<int>(thumb->data_size));
            LibRaw::dcraw_clear_mem(thumb);
            QBuffer buf(&data);
            buf.open(QIODevice::ReadOnly);
            QImageReader reader(&buf, "JPEG");
            reader.setAutoTransform(true);
            // Skaluj przez reader jeśli duży
            QSize orig = reader.size();
            if (!orig.isEmpty() && (orig.width() > target || orig.height() > target))
                reader.setScaledSize(orig.scaled(target * 2, target * 2, Qt::KeepAspectRatio));
            QImage img = reader.read();
            if (!img.isNull() && (img.width() > target || img.height() > target))
                img = img.scaled(target, target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            return img;
        }
        if (thumb) LibRaw::dcraw_clear_mem(thumb);
    }

    // Fallback: half-size decode
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
    if (result.width() > target || result.height() > target)
        result = result.scaled(target, target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return result;
}

void PreviewPanel::invalidate(const QString& path) {
    // Wyczyść cache podglądu i reset current_path → następny load() odświeży
    QPixmapCache::remove("preview:" + path);
    if (m_current_path == path) {
        m_current_path.clear();
    }
}

void PreviewPanel::load(const QString& path) {
    if (path.isEmpty()) {
        m_view->set_image(QImage());
        m_info_label->clear();
        m_histogram->clear();
        return;
    }
    if (path == m_current_path && m_view->has_image()) return;

    QString key = "preview:" + path;
    QPixmap cached;
    if (QPixmapCache::find(key, &cached) && !cached.isNull()) {
        m_current_path = path;
        // Zastosuj rotation z metadanych
        int rot = MetaStore::load(path).rotation;
        QImage ci = cached.toImage();
        if (rot != 0) {
            QTransform t; t.rotate(rot);
            ci = ci.transformed(t, Qt::SmoothTransformation);
        }
        m_view->set_image(ci);
        return;
    }

    int gen = ++m_load_gen;
    m_current_path = path;
    int target = qMax(1200, qMax(width(), height()) * 2);

    m_future = QtConcurrent::run([this, path, gen, target]() {
        QImage img;
        QString info;

        if (FileScanner::is_raw(path)) {
            // RAW — embedded JPEG thumbnail przez libraw (~50ms)
            img = load_raw_preview(path, target);
            if (!img.isNull())
                info = QString("%1 × %2 px  [RAW]").arg(img.width()).arg(img.height());
        }

        if (img.isNull()) {
            // JPEG/PNG/TIFF — standardowy decode
            QImageReader reader(path);
            reader.setAutoTransform(true);
            QSize orig = reader.size();
            if (orig.isEmpty() || orig.width() <= 0 || orig.height() <= 0) return;

            int decode_sz = target * 2;
            if (orig.width() > decode_sz || orig.height() > decode_sz) {
                QSize sz = orig.scaled(decode_sz, decode_sz, Qt::KeepAspectRatio);
                if (sz.width() > 0 && sz.height() > 0)
                    reader.setScaledSize(sz);
            }
            img = reader.read();
            if (img.isNull() || img.width() <= 0 || img.height() <= 0) return;
            if (img.width() > target || img.height() > target)
                img = img.scaled(target, target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            info = QString("%1 × %2 px").arg(orig.width()).arg(orig.height());
        }

        if (img.isNull()) return;

        // Zarządzanie kolorem — konwersja profilu ICC
        img = apply_color_mode(img);

        // Zastosuj rotation z metadanych .leye
        int rotation = MetaStore::load(path).rotation;
        if (rotation != 0) {
            QTransform t; t.rotate(rotation);
            img = img.transformed(t, Qt::SmoothTransformation);
        }

        QPixmap pix = QPixmap::fromImage(img);
        QMetaObject::invokeMethod(this, [this, pix, img, info, path, gen]() {
            if (gen < m_load_gen && m_current_path != path) return;
            QPixmapCache::insert("preview:" + path, pix);
            m_view->set_image(img);
            m_info_label->setText(info);
            m_histogram->compute(img);
        }, Qt::QueuedConnection);
    });
}

} // namespace LapesEye
