#include "LapesEye/ui/FullscreenViewer.h"
#include <algorithm>
#include <vector>
#if LEYE_HAS_GL
#  include <cmath>
#endif
#include "LapesEye/core/ColorManagement.h"
#include "LapesEye/core/MetaStore.h"
#include "LapesEye/core/CameraProfiles.h"
#include "LapesEye/core/SonyILCE7M3Profile.h"
#include <QApplication>
#include <QScreen>
#include <QImageReader>
#include <QPixmapCache>
#include <QTransform>
#include <QBuffer>
#include <QtConcurrent/QtConcurrent>
#include <QPainter>
#include <QFileInfo>
#include <QResizeEvent>
#include <QWheelEvent>
#include <libraw/libraw.h>

namespace LapesEye {

// ─── Jaskrawość +12 (Vibrance) ───────────────────────────────────────────────
// Selektywne nasycenie — mocniej działa na mało nasycone piksele (jak Lape Vivid)
// Nie zmienia jasności, nie zmienia barwy
static QImage apply_vibrance(const QImage& src, float vibrance = 0.12f) {
    QImage result = src.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < result.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            QRgb px = line[x];
            float r = qRed(px)   / 255.f;
            float g = qGreen(px) / 255.f;
            float b = qBlue(px)  / 255.f;
            float mx = std::max({r, g, b});
            float mn = std::min({r, g, b});
            float sat = (mx > 0.f) ? (mx - mn) / mx : 0.f;
            // Jaskrawość: im mniejsze nasycenie tym silniejszy efekt
            float boost = vibrance * (1.f - sat);
            float avg = (r + g + b) / 3.f;
            r = std::clamp(avg + (r - avg) * (1.f + boost), 0.f, 1.f);
            g = std::clamp(avg + (g - avg) * (1.f + boost), 0.f, 1.f);
            b = std::clamp(avg + (b - avg) * (1.f + boost), 0.f, 1.f);
            line[x] = qRgb((int)(r*255+.5f),(int)(g*255+.5f),(int)(b*255+.5f));
        }
    }
    return result;
}

// ─── Unsharp mask (wyostrzanie po skalowaniu) ─────────────────────────────────
// Algorytm: wyostrzony = oryginal + (oryginal - rozmyty_gaussem) * siła
// Nie dotyka kolorów — operuje na istniejących pikselach.
// radius: promień rozmycia (1-3px), strength: siła efektu (0.3-0.8 = subtelne)
static QImage unsharp_mask(const QImage& src, int radius = 1, float strength = 0.45f) {
    // Pracujemy na RGBA8888 dla prostoty
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    QImage blurred = img;

    // Szybkie rozmycie box filter (aproksymacja gaussa) — 2 przebiegi
    const int w = img.width(), h = img.height();
    const int r = qBound(1, radius, 3);

    // Horizontal blur
    QImage tmp(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb* src_line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        QRgb* dst_line = reinterpret_cast<QRgb*>(tmp.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int rr = 0, gg = 0, bb = 0, cnt = 0;
            for (int dx = -r; dx <= r; ++dx) {
                int nx = qBound(0, x + dx, w - 1);
                QRgb px = src_line[nx];
                rr += qRed(px); gg += qGreen(px); bb += qBlue(px); ++cnt;
            }
            dst_line[x] = qRgb(rr/cnt, gg/cnt, bb/cnt);
        }
    }
    // Vertical blur
    for (int y = 0; y < h; ++y) {
        QRgb* dst_line = reinterpret_cast<QRgb*>(blurred.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int rr = 0, gg = 0, bb = 0, cnt = 0;
            for (int dy = -r; dy <= r; ++dy) {
                int ny = qBound(0, y + dy, h - 1);
                QRgb px = reinterpret_cast<const QRgb*>(tmp.constScanLine(ny))[x];
                rr += qRed(px); gg += qGreen(px); bb += qBlue(px); ++cnt;
            }
            dst_line[x] = qRgb(rr/cnt, gg/cnt, bb/cnt);
        }
    }

    // Unsharp: wynik = orig + (orig - blur) * strength
    QImage result(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb* orig_line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        const QRgb* blur_line = reinterpret_cast<const QRgb*>(blurred.constScanLine(y));
        QRgb* res_line = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb o = orig_line[x], b = blur_line[x];
            int rv = qBound(0, (int)(qRed(o)   + (qRed(o)   - qRed(b))   * strength), 255);
            int gv = qBound(0, (int)(qGreen(o) + (qGreen(o) - qGreen(b)) * strength), 255);
            int bv = qBound(0, (int)(qBlue(o)  + (qBlue(o)  - qBlue(b))  * strength), 255);
            res_line[x] = qRgb(rv, gv, bv);
        }
    }
    return result;
}

FullscreenViewer::~FullscreenViewer() {
#if LEYE_HAS_GL
    if (m_gl_ctx && m_gl_surf) {
        m_gl_ctx->makeCurrent(m_gl_surf);
        if (auto* f = gl()) {
            if (m_gl_vao) f->glDeleteVertexArrays(1, &m_gl_vao);
            if (m_gl_vbo) f->glDeleteBuffers(1, &m_gl_vbo);
            if (m_gl_tex) f->glDeleteTextures(1, &m_gl_tex);
        }
        delete m_gl_fbo;  m_gl_fbo  = nullptr;
        delete m_gl_prog; m_gl_prog = nullptr;
        m_gl_ctx->doneCurrent();
    }
#endif
}

FullscreenViewer::FullscreenViewer(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setStyleSheet("background: black;");
    setMouseTracking(true);

    m_overlay_timer = new QTimer(this);
    m_overlay_timer->setSingleShot(true);
    m_overlay_timer->setInterval(3000);
    QObject::connect(m_overlay_timer, &QTimer::timeout, this, [this]() {
        m_show_overlay = false;
        update();
    });
#if LEYE_HAS_GL
    gl_init();
#endif
}

// ─── Pomocnicza: oblicz base_size obrazu na ekranie (bez zoom) ───────────────
QSizeF FullscreenViewer::base_image_size() const {
    if (m_pixmap.isNull()) return {};
    // Przy zoom używaj rozmiaru aktywnego pixmap
    const QPixmap& active = (m_zoom > 1.1 && !m_pixmap_full.isNull())
                            ? m_pixmap_full : m_pixmap;
    return QSizeF(active.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio);
}

// ─── Przelicz punkt ekranu → punkt w pixmapie (0..1 normalizowany) ──────────
QPointF FullscreenViewer::screen_to_image(const QPointF& screen_pt) const {
    QSizeF bs = base_image_size();
    if (bs.isEmpty()) return {};
    QSizeF zoomed = bs * m_zoom;
    QPointF tl = QPointF(width() / 2.0 - zoomed.width()  / 2.0,
                         height()/ 2.0 - zoomed.height() / 2.0) + m_offset;
    // Znormalizowane współrzędne w obrazie [0..1]
    return QPointF((screen_pt.x() - tl.x()) / zoomed.width(),
                   (screen_pt.y() - tl.y()) / zoomed.height());
}

// ─── Zoom z focusem na punkt ekranu ─────────────────────────────────────────
void FullscreenViewer::zoom_at(double new_zoom, const QPointF& focus_screen) {
    new_zoom = qBound(0.1, new_zoom, 32.0);

    // Punkt w przestrzeni obrazu przed zmianą zoom
    QPointF img_pt = screen_to_image(focus_screen);

    QSizeF bs = base_image_size();
    QSizeF new_zoomed = bs * new_zoom;

    // Nowy offset taki żeby punkt img_pt był nadal pod focusem
    QPointF new_tl = focus_screen - QPointF(img_pt.x() * new_zoomed.width(),
                                            img_pt.y() * new_zoomed.height());
    m_offset = new_tl - QPointF(width() / 2.0 - new_zoomed.width()  / 2.0,
                                height()/ 2.0 - new_zoomed.height() / 2.0);
    m_zoom = new_zoom;
    update();
}

// ─── Otwórz viewer ───────────────────────────────────────────────────────────
void FullscreenViewer::show_image(const QStringList& paths, int index) {
    m_paths  = paths;
    m_index  = qBound(0, index, paths.size() - 1);
    m_zoom   = 1.0;
    m_offset = {0, 0};
    m_is_zoomed = false;
    m_show_overlay = true;
    m_overlay_timer->start();
    // Wyczyść poprzedni pixmap — nie pokazuj starego zdjęcia jako placeholder
    // przy pierwszym otwarciu fullscreen (spacja po raz drugi)
    m_pixmap         = QPixmap{};
    m_pixmap_full    = QPixmap{};
    m_loading_pixmap = QPixmap{};
    ++m_load_gen;  // anuluj ewentualne poprzednie żądania
    ++m_prefetch_gen;  // anuluj stare wątki prefetch
    m_prefetch_cache.clear();       // nowy folder — stary cache nieaktualny
    m_prefetch_in_flight.clear();
    load_current();
    prefetch_neighbors();
    showFullScreen();
    raise();
    activateWindow();
}

// ─── Nawigacja — zachowuje zoom i offset ────────────────────────────────────
void FullscreenViewer::navigate(int delta) {
    if (m_paths.isEmpty()) return;
    int new_idx = m_index + delta;
    if (new_idx < 0 || new_idx >= m_paths.size()) return;
    m_index = new_idx;
    m_pixmap_full = QPixmap{};  // stara pełna rozdzielczość nieaktualna
    ++m_load_gen_full;           // anuluj ewentualne ładowanie w tle
    m_show_overlay = true;
    m_overlay_timer->start();
    load_current();
    prefetch_neighbors();
    emit index_changed(m_index);
}

// ─── Ładowanie obrazu w tle ─────────────────────────────────────────────────
void FullscreenViewer::load_current() {
    if (m_paths.isEmpty()) return;

    QString path = m_paths[m_index];

    // 1. Prefetch cache — zdjęcie już gotowe w pełnej jakości
    if (m_prefetch_cache.contains(path)) {
        QPixmap pix = m_prefetch_cache.take(path);  // weź i usuń z cache
        m_prefetch_in_flight.remove(path);
        m_pixmap  = pix;
        m_loading = false;
        update();
        // Prefetch cache zawiera quarter_size RAW — doczytaj etap 2 i 3
        static const QSet<QString> raw_exts_ch = {
            "arw","cr2","cr3","nef","nrw","orf","raf","rw2","dng","pef","srw","x3f"
        };
        if (raw_exts_ch.contains(QFileInfo(path).suffix().toLower()))
            load_full_resolution();
        return;
    }

    // 2. Placeholder przed wczytaniem właściwego obrazu
    static const QSet<QString> raw_exts_ph = {
        "arw","cr2","cr3","nef","nrw","orf","raf","rw2","dng","pef","srw","x3f"
    };
    bool is_raw_ph = raw_exts_ph.contains(QFileInfo(path).suffix().toLower());

    if (is_raw_ph) {
        // RAW: wyczyść ekran — czarny do czasu wczytania quarter_size (~50ms)
        m_loading = true;
        m_pixmap  = QPixmap{};
        m_loading_pixmap = QPixmap{};
        update();
    } else {
        // Non-RAW: pokaż miniaturę JPEG z cache jako placeholder
        QPixmap cached_pix;
        for (int sz : {1200, 600, 400, 300, 250, 220, 200}) {
            QString cache_key = path + "@" + QString::number(sz);
            if (QPixmapCache::find(cache_key, &cached_pix) && !cached_pix.isNull())
                break;
        }
        if (cached_pix.isNull()) {
            QString preview_key = "preview:" + path;
            QPixmapCache::find(preview_key, &cached_pix);
        }
        if (!cached_pix.isNull()) {
            m_pixmap = cached_pix;
            m_loading = true;
            m_loading_pixmap = cached_pix;
        } else {
            m_loading = true;
            m_loading_pixmap = m_pixmap;
        }
        update();
    }

    // Generacja counter — ignoruj wyniki starych żądań
    int gen = ++m_load_gen;
    QSize screen_size = QGuiApplication::primaryScreen()->size() * 2;

    m_future = QtConcurrent::run([this, path, screen_size, gen]() {
        QImage img;

        // Sprawdź czy to RAW — użyj libraw dla szybkiego podglądu
        static const QSet<QString> raw_exts = {
            "arw","cr2","cr3","nef","nrw","orf","raf","rw2","dng","pef","srw","x3f"
        };
        QString ext = QFileInfo(path).suffix().toLower();

        if (raw_exts.contains(ext)) {
            // Etap 1: quarter_size = half_size + scale 0.5×
            // ~50ms, ~800×600 — natychmiastowy podgląd bez JPEG
            LibRaw raw;
            raw.imgdata.params.half_size        = 1;
            raw.imgdata.params.four_color_rgb   = 0;
            raw.imgdata.params.use_camera_wb    = 1;
            raw.imgdata.params.use_auto_wb      = 0;
            raw.imgdata.params.use_camera_matrix= 1;
            raw.imgdata.params.output_color     = 1;
            raw.imgdata.params.gamm[0]          = 1.0 / 2.222;
            raw.imgdata.params.gamm[1]          = 4.5;
            raw.imgdata.params.no_auto_bright   = 1;
            raw.imgdata.params.bright           = 1.0f;
            raw.imgdata.params.user_flip        = -1;
            if (raw.open_file(path.toLocal8Bit().constData()) == LIBRAW_SUCCESS &&
                raw.unpack()        == LIBRAW_SUCCESS &&
                raw.dcraw_process() == LIBRAW_SUCCESS) {
                libraw_processed_image_t* proc = raw.dcraw_make_mem_image();
                if (proc) {
                    QImage half(proc->data, proc->width, proc->height,
                                proc->width * 3, QImage::Format_RGB888);
                    // Skaluj do 50% → quarter rozdzielczość (~800×600 dla Sony A7III)
                    img = half.scaled(half.width() / 2, half.height() / 2,
                                      Qt::IgnoreAspectRatio, Qt::FastTransformation).copy();
                    LibRaw::dcraw_clear_mem(proc);
                }
            }
        }

        // Dla RAW — zmierz factor jasności (ten sam co etap 2)
        // Dzięki temu etap 1 i 2 mają identyczną jasność — bez skoku przy fade
        if (raw_exts.contains(ext) && !img.isNull()) {
            // Krok A: pobierz embedded JPEG i zmierz jego luminancję
            std::vector<uint8_t> jpeg_lumas_s1;
            {
                LibRaw rj;
                if (rj.open_file(path.toLocal8Bit().constData()) == LIBRAW_SUCCESS &&
                    rj.unpack_thumb() == LIBRAW_SUCCESS) {
                    libraw_processed_image_t* t = rj.dcraw_make_mem_thumb();
                    if (t && t->type == LIBRAW_IMAGE_JPEG) {
                        QByteArray jd(reinterpret_cast<const char*>(t->data), t->data_size);
                        LibRaw::dcraw_clear_mem(t);
                        QBuffer buf(&jd); buf.open(QIODevice::ReadOnly);
                        QImageReader rd(&buf, "JPEG"); rd.setAutoTransform(true);
                        QImage ji = rd.read();
                        if (!ji.isNull()) {
                            ji = ji.convertToFormat(QImage::Format_RGB32);
                            jpeg_lumas_s1.reserve(ji.width() * ji.height() / 4);
                            for (int y = 0; y < ji.height(); y += 2) {
                                const QRgb* ln = reinterpret_cast<const QRgb*>(ji.constScanLine(y));
                                for (int x = 0; x < ji.width(); x += 2)
                                    jpeg_lumas_s1.push_back((uint8_t)(0.299f*qRed(ln[x])
                                        + 0.587f*qGreen(ln[x]) + 0.114f*qBlue(ln[x])));
                            }
                            std::sort(jpeg_lumas_s1.begin(), jpeg_lumas_s1.end());
                        }
                    } else if (t) { LibRaw::dcraw_clear_mem(t); }
                }
            }
            auto jpeg_pct_s1 = [&](float p) -> float {
                if (jpeg_lumas_s1.empty()) return -1.f;
                return (float)jpeg_lumas_s1[(size_t)(jpeg_lumas_s1.size() * p)];
            };
            // Krok B: zmierz luminancję RAW etapu 1
            QImage tmp1 = img.convertToFormat(QImage::Format_RGB32);
            std::vector<uint8_t> lumas_raw1;
            lumas_raw1.reserve(tmp1.width() * tmp1.height() / 4);
            for (int y = 0; y < tmp1.height(); y += 2) {
                const QRgb* ln = reinterpret_cast<const QRgb*>(tmp1.constScanLine(y));
                for (int x = 0; x < tmp1.width(); x += 2)
                    lumas_raw1.push_back((uint8_t)(0.299f*qRed(ln[x])
                        + 0.587f*qGreen(ln[x]) + 0.114f*qBlue(ln[x])));
            }
            std::sort(lumas_raw1.begin(), lumas_raw1.end());
            int n1 = lumas_raw1.size();
            // Krok C: oblicz factor (ta sama logika co load_full_resolution)
            float raw_anchor1 = -1.f, jpeg_anchor1 = -1.f, factor1 = 1.f;
            for (float p = 0.90f; p >= 0.10f; p -= 0.05f) {
                float rv = (float)lumas_raw1[(size_t)(n1 * p)];
                if (rv < 128.f) {
                    raw_anchor1  = rv;
                    jpeg_anchor1 = jpeg_pct_s1(p);
                    break;
                }
            }
            if (raw_anchor1 > 1.f) {
                float ja = std::min(jpeg_anchor1, 253.f);
                factor1 = ja / raw_anchor1;
                float max_f = (jpeg_anchor1 > 180.f) ? 1.55f : 2.5f;
                factor1 = std::clamp(factor1, 0.70f, max_f);
            }
            // Zapisz factor dla etapu 2
            float factor_to_save = factor1;
            QMetaObject::invokeMethod(this, [this, factor_to_save]() {
                m_raw_factor = factor_to_save;
            }, Qt::QueuedConnection);
            // Krok D: zastosuj factor + vibrance na etapie 1
            if (std::abs(factor1 - 1.f) > 0.01f) {
                img = img.convertToFormat(QImage::Format_RGB32);
                for (int y = 0; y < img.height(); ++y) {
                    QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
                    for (int x = 0; x < img.width(); ++x) {
                        float r = qRed(line[x])   / 255.f;
                        float g = qGreen(line[x]) / 255.f;
                        float b = qBlue(line[x])  / 255.f;
                        float luma = 0.299f*r + 0.587f*g + 0.114f*b;
                        float t2 = std::clamp((luma - 0.75f) / (0.95f - 0.75f), 0.f, 1.f);
                        float eff = factor1 * (1.f - t2) + 1.f * t2;
                        float scale = (luma > 0.001f) ? std::min(eff, 1.f / luma) : 1.f;
                        line[x] = qRgb(
                            std::clamp((int)(r * scale * 255.f + .5f), 0, 255),
                            std::clamp((int)(g * scale * 255.f + .5f), 0, 255),
                            std::clamp((int)(b * scale * 255.f + .5f), 0, 255));
                    }
                }
            }
            img = apply_vibrance(img, 0.12f);
        }

        // Fallback dla JPEG/PNG/TIFF itp.
        if (img.isNull()) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            // Ładuj pełną rozdzielczość — bez setScaledSize
            // Dla Sony 6000×4000 to ~96MB RAM ale maksymalna jakość
            // Skalujemy przez SmoothTransformation dopiero jeśli przekracza 2× ekran
            img = reader.read();
        }

        if (img.isNull()) return;

        // Zarządzanie kolorem — konwersja profilu ICC
        img = apply_color_mode(img);

        // Ogranicz rozmiar tylko dla bardzo dużych obrazów (>2× ekran)
        QSize limit = screen_size;  // screen_size = primaryScreen * 2
        if (img.width() > limit.width() || img.height() > limit.height())
            img = img.scaled(limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        // Wyostrzanie po skalowaniu — dla wszystkich formatów
        img = unsharp_mask(img, 1, 0.6f);

        // Zastosuj rotation z metadanych .leye (obrót ustawiony przez użytkownika)
        int rotation = MetaStore::load(path).rotation;
        if (rotation != 0) {
            QTransform t;
            t.rotate(rotation);
            img = img.transformed(t, Qt::SmoothTransformation);
        }

        QPixmap pix = QPixmap::fromImage(img);
        qDebug() << "[FSV] załadowano:" << QFileInfo(path).fileName()
                 << "rozmiar:" << img.size();
        bool is_raw_cb = raw_exts.contains(ext);
        QMetaObject::invokeMethod(this, [this, pix, gen, is_raw_cb]() {
            if (gen != m_load_gen) return;
            m_loading = false;
            // Etap 1: cross-fade z poprzedniego zdjęcia do quarter_size RAW
            start_fade(pix);
            // RAW: od razu etap 2 (full quality) w tle
            if (is_raw_cb)
                load_full_resolution();
        }, Qt::QueuedConnection);
    });
}


// ─── Ładowanie pełnej rozdzielczości do zoom-in ──────────────────────────────
void FullscreenViewer::load_full_resolution() {
    if (m_paths.isEmpty()) return;
    QString path = m_paths[m_index];
    QString ext  = QFileInfo(path).suffix().toLower();

    static const QSet<QString> raw_exts = {
        "arw","cr2","cr3","nef","nrw","orf","raf","rw2","dng","pef","srw","x3f"
    };
    if (!raw_exts.contains(ext)) return;

    ++m_load_gen_full;
    int gen = m_load_gen_full;

    [[maybe_unused]] auto future = QtConcurrent::run([this, path, gen]() {

        // ── Krok 1: Zmierz jasność embedded JPEG (referencja = Sony JPEG) ────
        float jpeg_p90 = -1.f;
        {
            LibRaw rj;
            if (rj.open_file(path.toLocal8Bit().constData()) == LIBRAW_SUCCESS &&
                rj.unpack_thumb() == LIBRAW_SUCCESS) {
                libraw_processed_image_t* t = rj.dcraw_make_mem_thumb();
                if (t && t->type == LIBRAW_IMAGE_JPEG) {
                    QByteArray jd(reinterpret_cast<const char*>(t->data), t->data_size);
                    LibRaw::dcraw_clear_mem(t);
                    QBuffer buf(&jd); buf.open(QIODevice::ReadOnly);
                    QImageReader rd(&buf, "JPEG");
                    rd.setAutoTransform(true);
                    QImage ji = rd.read();
                    if (!ji.isNull()) {
                        ji = ji.convertToFormat(QImage::Format_RGB32);
                        std::vector<uint8_t> lumas;
                        lumas.reserve(ji.width() * ji.height() / 4);
                        for (int y = 0; y < ji.height(); y += 2) {
                            const QRgb* line = reinterpret_cast<const QRgb*>(ji.constScanLine(y));
                            for (int x = 0; x < ji.width(); x += 2)
                                lumas.push_back((uint8_t)(0.299f*qRed(line[x])
                                    + 0.587f*qGreen(line[x]) + 0.114f*qBlue(line[x])));
                        }
                        std::sort(lumas.begin(), lumas.end());
                        jpeg_p90 = lumas[(int)(lumas.size() * 0.90f)];
                    }
                } else if (t) { LibRaw::dcraw_clear_mem(t); }
            }
        }

        // ── Krok 2: Decode RAW z bright=1.0, neutralny ───────────────────────
        LibRaw raw;
        raw.imgdata.params.half_size        = 0;
        raw.imgdata.params.use_camera_wb    = 1;
        raw.imgdata.params.use_auto_wb      = 0;
        raw.imgdata.params.use_camera_matrix= 1;
        raw.imgdata.params.output_color     = 1;
        raw.imgdata.params.gamm[0]          = 1.0 / 2.222;
        raw.imgdata.params.gamm[1]          = 4.5;
        raw.imgdata.params.no_auto_bright   = 1;  // neutralny, bez auto
        raw.imgdata.params.bright           = 1.0f;
        raw.imgdata.params.user_flip        = -1;

        QImage img;
        if (raw.open_file(path.toLocal8Bit().constData()) == LIBRAW_SUCCESS &&
            raw.unpack()        == LIBRAW_SUCCESS &&
            raw.dcraw_process() == LIBRAW_SUCCESS) {
            libraw_processed_image_t* proc = raw.dcraw_make_mem_image();
            if (proc) {
                img = QImage(proc->data, proc->width, proc->height,
                             proc->width * 3, QImage::Format_RGB888).copy();
                LibRaw::dcraw_clear_mem(proc);
            }
        }
        if (img.isNull()) return;

        // ── Krok 3: Dopasuj jasność RAW do JPEG ─────────────────────────────
        // Używamy wielu percentyli żeby obsłużyć przepalone zdjęcia (p90=255)
        // LUT aplikowana na luminancji — kolory nie są zmieniane
        if (jpeg_p90 > 0.f) {
            // Zbierz luminancje RAW (próbkowanie co 2. piksel)
            QImage tmp = img.convertToFormat(QImage::Format_RGB32);
            std::vector<uint8_t> lumas_raw;
            lumas_raw.reserve(tmp.width() * tmp.height() / 4);
            for (int y = 0; y < tmp.height(); y += 2) {
                const QRgb* line = reinterpret_cast<const QRgb*>(tmp.constScanLine(y));
                for (int x = 0; x < tmp.width(); x += 2)
                    lumas_raw.push_back((uint8_t)(0.299f*qRed(line[x])
                        + 0.587f*qGreen(line[x]) + 0.114f*qBlue(line[x])));
            }
            std::sort(lumas_raw.begin(), lumas_raw.end());
            int n = lumas_raw.size();

            // Wybierz najwyższy percentyl poniżej 255 jako punkt kotwicy
            float anchor_pct = 0.90f;
            float raw_anchor = lumas_raw[(int)(n * anchor_pct)];
            float jpeg_anchor = jpeg_p90;
            bool used_fallback = false;

            // Jeśli p90=255 (przepalone okna/lampy) — zejdź do niższego percentyla
            // ale tylko dla RAW; jpeg_anchor zostaje proporcjonalnie zmniejszony
            if (raw_anchor >= 254.f && jpeg_anchor >= 254.f) {
                // Oba przepalone — nie ma jak dopasować, użyj bright=0.90
                used_fallback = true;
            } else {
                while (raw_anchor >= 254.f && anchor_pct > 0.50f) {
                    anchor_pct -= 0.10f;
                    raw_anchor  = lumas_raw[(int)(n * anchor_pct)];
                    // jpeg_anchor: jeśli jpeg_p90=255 nie możemy go skalować
                    // używamy stałego współczynnika 0.90 jako bazowego
                    jpeg_anchor = (jpeg_p90 < 255.f)
                        ? jpeg_p90 * (anchor_pct / 0.90f)
                        : 230.f * (anchor_pct / 0.90f);  // 230 = empiryczna baza dla przepalonych
                }
            }

            float factor = 1.0f;
            if (used_fallback) {
                factor = 0.90f;  // oba przepalone — stały bright
            } else {
                factor = (raw_anchor > 1.f) ? (jpeg_anchor / raw_anchor) : 1.f;
                factor = std::max(0.5f, std::min(2.5f, factor));
            }

            qDebug() << "[FSV] jpeg_p90=" << jpeg_p90 << "raw_anchor=" << raw_anchor
                     << "jpeg_anchor=" << jpeg_anchor << "factor=" << factor;

            if (std::abs(factor - 1.0f) > 0.01f) {
                // Zastosuj factor na luminancji — zachowuje kolory (H, S)
                img = img.convertToFormat(QImage::Format_RGB32);
                for (int y = 0; y < img.height(); ++y) {
                    QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
                    for (int x = 0; x < img.width(); ++x) {
                        float r = qRed(line[x])   / 255.f;
                        float g = qGreen(line[x]) / 255.f;
                        float b = qBlue(line[x])  / 255.f;
                        float luma = 0.299f*r + 0.587f*g + 0.114f*b;
                        float new_luma = std::min(luma * factor, 1.f);
                        // Skaluj R,G,B proporcjonalnie zachowując stosunek kolorów
                        float scale = (luma > 0.001f) ? (new_luma / luma) : 1.f;
                        int ri = std::clamp((int)(r * scale * 255.f + .5f), 0, 255);
                        int gi = std::clamp((int)(g * scale * 255.f + .5f), 0, 255);
                        int bi = std::clamp((int)(b * scale * 255.f + .5f), 0, 255);
                        line[x] = qRgb(ri, gi, bi);
                    }
                }
            }
        }


        // ── Krok 4: Jaskrawość +12% ───────────────────────────────────────────
        img = apply_vibrance(img, 0.12f);

        img = apply_color_mode(img);

        int rotation = MetaStore::load(path).rotation;
        if (rotation != 0) {
            QTransform t; t.rotate(rotation);
            img = img.transformed(t, Qt::SmoothTransformation);
        }

        QPixmap pix_full = QPixmap::fromImage(img);

        QSize screen = QGuiApplication::primaryScreen()->size();
        QSizeF bs = QSizeF(img.size()).scaled(QSizeF(screen), Qt::KeepAspectRatio);
        QImage scaled = img.scaled(bs.toSize(), Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);
        scaled = unsharp_mask(scaled, 1, 0.6f);
        QPixmap pix = QPixmap::fromImage(scaled);

        QMetaObject::invokeMethod(this, [this, pix, pix_full, gen]() {
            if (gen != m_load_gen_full) return;
            m_pixmap_full  = pix_full;
            m_loading_full = false;
            start_fade(pix);  // płynne cross-fade quarter_size → full quality
        }, Qt::QueuedConnection);
    });
}


// ─── Prefetch sąsiednich zdjęć ───────────────────────────────────────────────
void FullscreenViewer::prefetch_neighbors() {
    if (m_paths.size() <= 1) return;

    // Zbierz indeksy do prefetch (PREFETCH_RANGE w każdą stronę, bez bieżącego)
    for (int d = 1; d <= PREFETCH_RANGE; ++d) {
        for (int delta : {+d, -d}) {
            int idx = m_index + delta;
            if (idx < 0 || idx >= m_paths.size()) continue;
            const QString& path = m_paths[idx];
            // Pomiń jeśli już w cache lub w trakcie ładowania
            if (m_prefetch_cache.contains(path)) continue;
            if (m_prefetch_in_flight.contains(path)) continue;

            m_prefetch_in_flight.insert(path);
            QSize screen_size = QGuiApplication::primaryScreen()->size() * 2;
            int pgen = m_prefetch_gen;

            [[maybe_unused]] auto f = QtConcurrent::run([this, path, screen_size, pgen]() {
                QImage img;
                static const QSet<QString> raw_exts = {
                    "arw","cr2","cr3","nef","nrw","orf","raf","rw2","dng","pef","srw","x3f"
                };
                QString ext = QFileInfo(path).suffix().toLower();

                if (raw_exts.contains(ext)) {
                    // Prefetch: quarter_size (etap 1) — szybki placeholder bez JPEG
                    LibRaw raw;
                    raw.imgdata.params.half_size        = 1;
                    raw.imgdata.params.four_color_rgb   = 0;
                    raw.imgdata.params.use_camera_wb    = 1;
                    raw.imgdata.params.use_auto_wb      = 0;
                    raw.imgdata.params.use_camera_matrix= 1;
                    raw.imgdata.params.output_color     = 1;
                    raw.imgdata.params.gamm[0]          = 1.0 / 2.222;
                    raw.imgdata.params.gamm[1]          = 4.5;
                    raw.imgdata.params.no_auto_bright   = 1;
                    raw.imgdata.params.bright           = 1.0f;
                    raw.imgdata.params.user_flip        = -1;
                    if (raw.open_file(path.toLocal8Bit().constData()) == LIBRAW_SUCCESS &&
                        raw.unpack()        == LIBRAW_SUCCESS &&
                        raw.dcraw_process() == LIBRAW_SUCCESS) {
                        libraw_processed_image_t* proc = raw.dcraw_make_mem_image();
                        if (proc) {
                            QImage half(proc->data, proc->width, proc->height,
                                        proc->width * 3, QImage::Format_RGB888);
                            img = half.scaled(half.width() / 2, half.height() / 2,
                                              Qt::IgnoreAspectRatio, Qt::FastTransformation).copy();
                            LibRaw::dcraw_clear_mem(proc);
                        }
                    }
                }
                if (img.isNull()) {
                    QImageReader reader(path);
                    reader.setAutoTransform(true);
                    img = reader.read();
                }
                if (img.isNull()) {
                    QMetaObject::invokeMethod(this, [this, path, pgen]() {
                        if (pgen == m_prefetch_gen)
                            m_prefetch_in_flight.remove(path);
                    }, Qt::QueuedConnection);
                    return;
                }

                img = apply_color_mode(img);
                QSize limit = screen_size;
                if (img.width() > limit.width() || img.height() > limit.height())
                    img = img.scaled(limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                if (raw_exts.contains(ext))
                    img = unsharp_mask(img, 1, 0.6f);

                int rotation = MetaStore::load(path).rotation;
                if (rotation != 0) {
                    QTransform t; t.rotate(rotation);
                    img = img.transformed(t, Qt::SmoothTransformation);
                }

                QPixmap pix = QPixmap::fromImage(img);
                QMetaObject::invokeMethod(this, [this, path, pix, pgen]() {
                    m_prefetch_in_flight.remove(path);
                    // Odrzuć jeśli show_image() zostało wywołane po starcie wątku
                    if (pgen != m_prefetch_gen) return;
                    // Zapisz tylko jeśli nadal prawdopodobnie potrzebne
                    int idx = m_paths.indexOf(path);
                    if (idx >= 0 && qAbs(idx - m_index) <= PREFETCH_RANGE)
                        m_prefetch_cache[path] = pix;
                }, Qt::QueuedConnection);
            });
        }
    }
}

// ─── Rysowanie ───────────────────────────────────────────────────────────────
// ─── Cross-fade między etapem 1 (quarter) a etapem 2 (full quality) ─────────
void FullscreenViewer::start_fade(const QPixmap& next) {
    m_fade_from  = m_pixmap;   // zachowaj stary (quarter_size) jako tło
    m_pixmap     = next;        // nowy obraz gotowy
    m_fade_alpha = 0.f;

    if (!m_fade_timer) {
        m_fade_timer = new QTimer(this);
        m_fade_timer->setInterval(16);  // ~60fps
        connect(m_fade_timer, &QTimer::timeout, this, [this]() {
            m_fade_alpha += 0.07f;  // ~250ms całość (16ms × 16 kroków)
            if (m_fade_alpha >= 1.f) {
                m_fade_alpha = 1.f;
                m_fade_from  = QPixmap{};
                m_fade_timer->stop();
            }
            update();
        });
    }
    m_fade_timer->start();
    update();
}

void FullscreenViewer::paintEvent(QPaintEvent*) {
    const QPixmap& pix = m_loading      ? m_loading_pixmap
                       : (m_zoom > 1.1 && !m_pixmap_full.isNull()) ? m_pixmap_full
                       : m_pixmap;

#if LEYE_HAS_GL
    if (m_gl_ok && m_gl_tex && !pix.isNull()) {
        gl_paint();
        return;
    }
#endif

    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (pix.isNull()) {
        p.setPen(QColor("#666"));
        p.setFont(QFont("sans", 14));
        p.drawText(rect(), Qt::AlignCenter, "Ładowanie...");
        if (m_show_overlay) draw_overlay(p);
        return;
    }
    QSizeF bs = base_image_size();
    QSizeF zoomed = bs * m_zoom;
    QPointF tl = QPointF(width()/2.0 - zoomed.width()/2.0,
                         height()/2.0 - zoomed.height()/2.0) + m_offset;
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Cross-fade między etapami
    if (m_fade_alpha < 1.f) {
        if (!m_fade_from.isNull()) {
            // Fade między dwoma obrazami (etap1→etap2)
            p.setOpacity(1.0);
            p.drawPixmap(QRectF(tl, zoomed), m_fade_from, QRectF(m_fade_from.rect()));
        }
        // Nowy obraz narasta (działa też przy fade z czarnego — tło już czarne)
        p.setOpacity(m_fade_alpha);
        p.drawPixmap(QRectF(tl, zoomed), pix, QRectF(pix.rect()));
        p.setOpacity(1.0);
    } else {
        p.drawPixmap(QRectF(tl, zoomed), pix, QRectF(pix.rect()));
    }
    if (m_show_overlay) draw_overlay(p);
}

void FullscreenViewer::draw_overlay(QPainter& p) {
    // Pasek dolny
    QRect bar(0, height() - 50, width(), 50);
    p.fillRect(bar, QColor(0, 0, 0, 160));
    QString name = QFileInfo(m_paths.value(m_index)).fileName();
    QString pos  = QString("%1 / %2").arg(m_index + 1).arg(m_paths.size());
    p.setPen(Qt::white);
    QFont f = p.font(); f.setPixelSize(16); p.setFont(f);
    p.drawText(bar.adjusted(20, 0, -20, 0), Qt::AlignVCenter | Qt::AlignLeft,  name);
    p.drawText(bar.adjusted(20, 0, -20, 0), Qt::AlignVCenter | Qt::AlignRight, pos);
    // Pasek górny — podpowiedź
    QRect hint(0, 0, width(), 34);
    p.fillRect(hint, QColor(0, 0, 0, 140));
    f.setPixelSize(12); p.setFont(f); p.setPen(QColor("#aaa"));
    p.drawText(hint.adjusted(14, 0, -14, 0), Qt::AlignVCenter,
        "←/→  nawigacja   LPM  zoom x2 / reset   Scroll  zoom   0  reset   Spacja/Esc  zamknij");
}

// ─── Klawiatura ──────────────────────────────────────────────────────────────
void FullscreenViewer::keyPressEvent(QKeyEvent* e) {
    m_show_overlay = true;
    m_overlay_timer->start();
    switch (e->key()) {
        case Qt::Key_Left:  case Qt::Key_Up:   navigate(-1); return;
        case Qt::Key_Right: case Qt::Key_Down: navigate(+1); return;
        case Qt::Key_Space: case Qt::Key_Escape:
            hide(); emit closed(); return;
        case Qt::Key_0:
            m_zoom = 1.0; m_offset = {0,0}; m_is_zoomed = false; break;
        case Qt::Key_Plus: case Qt::Key_Equal:
            zoom_at(m_zoom * 1.25, QPointF(width()/2.0, height()/2.0)); break;
        case Qt::Key_Minus:
            zoom_at(m_zoom / 1.25, QPointF(width()/2.0, height()/2.0)); break;
        default: QWidget::keyPressEvent(e); return;
    }
    update();
}

// ─── Mysz ────────────────────────────────────────────────────────────────────
void FullscreenViewer::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_press_pos  = e->pos();
    m_press_time = QTime::currentTime();
    m_panning      = false;
    m_pan_start    = e->pos();
    m_offset_start = m_offset;
}

void FullscreenViewer::mouseMoveEvent(QMouseEvent* e) {
    m_show_overlay = true;
    m_overlay_timer->start();

    if (e->buttons() & Qt::LeftButton) {
        QPoint delta = e->pos() - m_pan_start;
        if (!m_panning && delta.manhattanLength() > 4) m_panning = true;
        if (m_panning) {
            m_offset = m_offset_start + QPointF(delta);
            setCursor(Qt::ClosedHandCursor);
        }
    } else {
        setCursor(Qt::ArrowCursor);
    }
    update();
}

void FullscreenViewer::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    setCursor(Qt::ArrowCursor);

    if (m_panning) {
        // Był drag — tylko kończ pan
        m_panning = false;
        return;
    }

    // Był klik (nie drag) — punkt 1 i 2
    QPointF click = QPointF(e->pos());
    if (!m_is_zoomed) {
        // Zoom x4 - kliknięty punkt ląduje na środku ekranu
        // Oblicz gdzie jest click w przestrzeni obrazu
        QPointF img_pt = screen_to_image(click);
        double new_zoom = 4.0;
        new_zoom = qBound(0.1, new_zoom, 32.0);
        QSizeF bs = base_image_size();
        QSizeF new_zoomed = bs * new_zoom;
        // Środek ekranu jako focus_screen - punkt img_pt będzie na środku
        QPointF screen_center(width() / 2.0, height() / 2.0);
        QPointF new_tl = screen_center - QPointF(img_pt.x() * new_zoomed.width(),
                                                  img_pt.y() * new_zoomed.height());
        m_offset = new_tl - QPointF(width() / 2.0 - new_zoomed.width()  / 2.0,
                                    height()/ 2.0 - new_zoomed.height() / 2.0);
        m_zoom = new_zoom;
        m_is_zoomed = true;
    } else {
        // LPM gdy powiększone → powrót do fit
        m_zoom = 1.0;
        m_offset = {0, 0};
        m_is_zoomed = false;
    }
    update();
}

void FullscreenViewer::mouseDoubleClickEvent(QMouseEvent*) {
    // Podwójny klik zamyka (obsługiwany jako dwa single-clicki — reset zoom przy pierwszym)
    // Dla pewności: jeśli jest powiększone — zamknij, jeśli nie — zamknij
    hide();
    emit closed();
}

void FullscreenViewer::wheelEvent(QWheelEvent* e) {
    // Punkt 3: zoom z focusem na pozycji kursora
    double factor = e->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
    double new_zoom = qBound(0.1, m_zoom * factor, 32.0);
    zoom_at(new_zoom, e->position());
    m_is_zoomed = (new_zoom > 1.001);
    m_show_overlay = true;
    m_overlay_timer->start();
}

void FullscreenViewer::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    update();
}


#if LEYE_HAS_GL
// ════════════════════════════════════════════════════════════════════════════
// GPU RENDERING — FullscreenViewer
// Obraz jako tekstura w VRAM, zoom/pan = zmiana MVP matrix → zero CPU per frame
// ════════════════════════════════════════════════════════════════════════════

static const char* FS_VERT = R"GLSL(
#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
uniform mat4 u_mvp;
out vec2 v_uv;
void main() {
    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
)GLSL";

static const char* FS_FRAG = R"GLSL(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 frag;
void main() { frag = texture(u_tex, v_uv); }
)GLSL";

FullscreenViewer::GL45* FullscreenViewer::gl() const {
    if (!m_gl_ctx) return nullptr;
    return QOpenGLVersionFunctionsFactory::get<GL45>(m_gl_ctx);
}

void FullscreenViewer::gl_init() {
    QSurfaceFormat fmt;
    fmt.setVersion(4, 5);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    m_gl_surf = new QOffscreenSurface(nullptr, this);
    m_gl_surf->setFormat(fmt);
    m_gl_surf->create();
    if (!m_gl_surf->isValid()) return;
    m_gl_ctx = new QOpenGLContext(this);
    m_gl_ctx->setFormat(fmt);
    if (!m_gl_ctx->create()) return;
    m_gl_ctx->makeCurrent(m_gl_surf);
    auto* f = gl();
    if (!f) { m_gl_ctx->doneCurrent(); return; }
    f->glDisable(GL_DEPTH_TEST);
    f->glEnable(GL_BLEND);
    f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Shader
    m_gl_prog = new QOpenGLShaderProgram(this);
    if (!m_gl_prog->addShaderFromSourceCode(QOpenGLShader::Vertex,   FS_VERT) ||
        !m_gl_prog->addShaderFromSourceCode(QOpenGLShader::Fragment, FS_FRAG) ||
        !m_gl_prog->link()) {
        delete m_gl_prog; m_gl_prog = nullptr;
        m_gl_ctx->doneCurrent(); return;
    }
    m_gl_u_mvp = m_gl_prog->uniformLocation("u_mvp");

    // Quad jednostkowy [0,0]→[1,1]
    static const float Q[] = {
        0.f,0.f, 0.f,1.f,  1.f,0.f, 1.f,1.f,
        1.f,1.f, 1.f,0.f,  0.f,0.f, 0.f,1.f,
        1.f,1.f, 1.f,0.f,  0.f,1.f, 0.f,0.f,
    };
    f->glCreateVertexArrays(1, &m_gl_vao);
    f->glCreateBuffers(1, &m_gl_vbo);
    f->glNamedBufferStorage(m_gl_vbo, sizeof(Q), Q, 0);
    f->glVertexArrayVertexBuffer(m_gl_vao, 0, m_gl_vbo, 0, 4*sizeof(float));
    f->glEnableVertexArrayAttrib(m_gl_vao, 0);
    f->glVertexArrayAttribFormat(m_gl_vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    f->glVertexArrayAttribBinding(m_gl_vao, 0, 0);
    f->glEnableVertexArrayAttrib(m_gl_vao, 1);
    f->glVertexArrayAttribFormat(m_gl_vao, 1, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float));
    f->glVertexArrayAttribBinding(m_gl_vao, 1, 0);

    m_gl_ok = true;
    m_gl_ctx->doneCurrent();
}

void FullscreenViewer::gl_upload_pixmap(const QPixmap& pix) {
    if (!m_gl_ok || !m_gl_ctx || !m_gl_surf) return;
    m_gl_ctx->makeCurrent(m_gl_surf);
    auto* f = gl(); if (!f) { m_gl_ctx->doneCurrent(); return; }

    QImage img = pix.toImage().convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width(), h = img.height();

    // Usuń starą teksturę jeśli rozmiar się zmienił
    if (m_gl_tex && m_gl_tex_size != QSize(w, h)) {
        f->glDeleteTextures(1, &m_gl_tex);
        m_gl_tex = 0;
    }
    if (!m_gl_tex) {
        f->glCreateTextures(GL_TEXTURE_2D, 1, &m_gl_tex);
        int mips = 1 + (int)std::log2((double)qMax(w, h));
        f->glTextureStorage2D(m_gl_tex, mips, GL_RGBA8, w, h);
        f->glTextureParameteri(m_gl_tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        f->glTextureParameteri(m_gl_tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTextureParameteri(m_gl_tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTextureParameteri(m_gl_tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    f->glTextureSubImage2D(m_gl_tex, 0, 0, 0, w, h,
                           GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
    f->glGenerateTextureMipmap(m_gl_tex);
    m_gl_tex_size = QSize(w, h);
    m_gl_ctx->doneCurrent();
}

void FullscreenViewer::gl_paint() {
    if (!m_gl_ok || !m_gl_tex || !m_gl_ctx || !m_gl_surf) return;
    const int W = width(), H = height();
    m_gl_ctx->makeCurrent(m_gl_surf);
    auto* f = gl(); if (!f) { m_gl_ctx->doneCurrent(); return; }

    // Cache FBO
    if (!m_gl_fbo || m_gl_fbo->size() != QSize(W, H)) {
        delete m_gl_fbo;
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setSamples(0);
        m_gl_fbo = new QOpenGLFramebufferObject(W, H, fmt);
    }
    if (!m_gl_fbo->isValid()) { m_gl_ctx->doneCurrent(); return; }

    m_gl_fbo->bind();
    f->glViewport(0, 0, W, H);
    f->glClearColor(0.f, 0.f, 0.f, 1.f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    // Projekcja ortho — piksel = jednostka, Y w dół
    m_gl_proj.setToIdentity();
    m_gl_proj.ortho(0.f, W, H, 0.f, -1.f, 1.f);

    // Oblicz pozycję i rozmiar — używaj rozmiaru tekstury (nie pixmapy)
    QSizeF bs = QSizeF(m_gl_tex_size).scaled(QSizeF(W, H), Qt::KeepAspectRatio);
    QSizeF zoomed = bs * m_zoom;
    QPointF tl(W/2.0 - zoomed.width()/2.0,
               H/2.0 - zoomed.height()/2.0);
    tl += m_offset;

    QMatrix4x4 model;
    model.translate(tl.x(), tl.y());
    model.scale(zoomed.width(), zoomed.height());

    m_gl_prog->bind();
    m_gl_prog->setUniformValue(m_gl_u_mvp, m_gl_proj * model);
    m_gl_prog->setUniformValue("u_tex", 0);
    f->glBindVertexArray(m_gl_vao);
    f->glBindTextureUnit(0, m_gl_tex);
    f->glDrawArrays(GL_TRIANGLES, 0, 6);
    f->glBindTextureUnit(0, 0);
    f->glBindVertexArray(0);
    m_gl_prog->release();
    m_gl_fbo->release();

    QImage result = m_gl_fbo->toImage(true);
    m_gl_ctx->doneCurrent();

    if (!result.isNull()) {
        QPainter p(this);
        p.drawImage(0, 0, result);
        draw_overlay(p);
    }
}

#endif // LEYE_HAS_GL
} // namespace LapesEye
