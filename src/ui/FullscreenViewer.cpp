#include "LapesEye/ui/FullscreenViewer.h"
#include "LapesEye/core/MetaStore.h"
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
}

// ─── Pomocnicza: oblicz base_size obrazu na ekranie (bez zoom) ───────────────
QSizeF FullscreenViewer::base_image_size() const {
    if (m_pixmap.isNull()) return {};
    return QSizeF(m_pixmap.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio);
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
    m_loading_pixmap = QPixmap{};
    ++m_load_gen;  // anuluj ewentualne poprzednie żądania
    load_current();
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
    // Zoom i offset zachowane — tak jak w punkcie 4
    m_show_overlay = true;
    m_overlay_timer->start();
    load_current();
    // Emituj żeby MainWindow mógł zaznaczyć właściwe zdjęcie (punkt 5)
    emit index_changed(m_index);
}

// ─── Ładowanie obrazu w tle ─────────────────────────────────────────────────
void FullscreenViewer::load_current() {
    if (m_paths.isEmpty()) return;

    QString path = m_paths[m_index];

    // Sprawdź cache miniatur — pokaż natychmiast
    QPixmap cached_pix;
    for (int sz : {1200, 600, 400, 300, 250, 220, 200}) {
        QString cache_key = path + "@" + QString::number(sz);
        if (QPixmapCache::find(cache_key, &cached_pix) && !cached_pix.isNull())
            break;
    }
    // Sprawdź cache pełnego podglądu
    if (cached_pix.isNull()) {
        QString preview_key = "preview:" + path;
        QPixmapCache::find(preview_key, &cached_pix);
    }

    if (!cached_pix.isNull()) {
        // Jest w cache — pokaż natychmiast bez mrugania
        m_pixmap = cached_pix;
        m_loading = true;  // nadal ładujemy full quality w tle
        m_loading_pixmap = cached_pix;
        update();
    } else {
        // Nie ma w cache — zachowaj poprzedni obraz (nie czyść m_pixmap)
        // żeby nie było czarnego ekranu, ustaw tylko m_loading = true
        m_loading = true;
        m_loading_pixmap = m_pixmap;  // poprzedni obraz jako placeholder
        update();
    }

    // Generacja counter — ignoruj wyniki starych żądań
    int gen = ++m_load_gen;
    // *2 dla HiDPI i ostrości przy zoom
    QSize screen_size = QGuiApplication::primaryScreen()->size() * 2;

    m_future = QtConcurrent::run([this, path, screen_size, gen]() {
        QImage img;

        // Sprawdź czy to RAW — użyj libraw dla szybkiego podglądu
        static const QSet<QString> raw_exts = {
            "arw","cr2","cr3","nef","nrw","orf","raf","rw2","dng","pef","srw","x3f"
        };
        QString ext = QFileInfo(path).suffix().toLower();

        if (raw_exts.contains(ext)) {
            LibRaw raw;
            if (raw.open_file(path.toLocal8Bit().constData()) == LIBRAW_SUCCESS) {
                if (raw.unpack_thumb() == LIBRAW_SUCCESS) {
                    libraw_processed_image_t* thumb = raw.dcraw_make_mem_thumb();
                    if (thumb && thumb->type == LIBRAW_IMAGE_JPEG) {
                        QByteArray jpeg_data(reinterpret_cast<const char*>(thumb->data),
                                             static_cast<int>(thumb->data_size));
                        LibRaw::dcraw_clear_mem(thumb);
                        QBuffer buf(&jpeg_data);
                        buf.open(QIODevice::ReadOnly);
                        QImageReader reader(&buf, "JPEG");
                        reader.setAutoTransform(true);
                        img = reader.read();
                    } else if (thumb) {
                        LibRaw::dcraw_clear_mem(thumb);
                    }
                }
                // Fallback: pełne dekodowanie
                if (img.isNull()) {
                    if (raw.unpack() == LIBRAW_SUCCESS &&
                        raw.dcraw_process() == LIBRAW_SUCCESS) {
                        libraw_processed_image_t* proc = raw.dcraw_make_mem_image();
                        if (proc) {
                            img = QImage(proc->data,
                                         proc->width, proc->height,
                                         proc->width * 3, QImage::Format_RGB888).copy();
                            LibRaw::dcraw_clear_mem(proc);
                        }
                    }
                }
            }
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

        // Skaluj tylko gdy obraz jest większy niż 2× ekran (np. medium format 100MP)
        QSize limit = screen_size * 2;
        if (img.width() > limit.width() || img.height() > limit.height())
            img = img.scaled(limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        // Zastosuj rotation z metadanych .leye (obrót ustawiony przez użytkownika)
        int rotation = MetaStore::load(path).rotation;
        if (rotation != 0) {
            QTransform t;
            t.rotate(rotation);
            img = img.transformed(t, Qt::SmoothTransformation);
        }

        QPixmap pix = QPixmap::fromImage(img);
        QMetaObject::invokeMethod(this, [this, pix, gen]() {
            if (gen != m_load_gen) return;  // stare żądanie — ignoruj
            m_pixmap  = pix;
            m_loading = false;
            update();
        }, Qt::QueuedConnection);
    });
}

// ─── Rysowanie ───────────────────────────────────────────────────────────────
void FullscreenViewer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    const QPixmap& pix = m_loading ? m_loading_pixmap : m_pixmap;
    if (pix.isNull()) {
        // Tylko gdy naprawdę nie ma nic do pokazania
        p.setPen(QColor("#666"));
        p.setFont(QFont("sans", 14));
        p.drawText(rect(), Qt::AlignCenter, "Ładowanie...");
        if (m_show_overlay) draw_overlay(p);
        return;
    }

    QSizeF bs = base_image_size();
    QSizeF zoomed = bs * m_zoom;
    QPointF tl = QPointF(width() / 2.0 - zoomed.width()  / 2.0,
                         height()/ 2.0 - zoomed.height() / 2.0) + m_offset;

    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawPixmap(QRectF(tl, zoomed), pix, QRectF(pix.rect()));

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

} // namespace LapesEye
