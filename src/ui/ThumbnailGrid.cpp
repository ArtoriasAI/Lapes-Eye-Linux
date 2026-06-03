#include <QDateTime>
#include <QPointer>
#include <QPixmapCache>
#include <QJsonDocument>
#include <QJsonObject>
#include "LapesEye/ui/ThumbnailGrid.h"
#include "LapesEye/core/PerfTimer.h"
#include "LapesEye/ui/ThumbnailCanvas.h"
#include "LapesEye/ui/RubberOverlay.h"
#include "LapesEye/ui/ThumbnailItem.h"
#include "LapesEye/workers/ThumbWorker.h"
#include "LapesEye/core/ThumbCache.h"
#include <exiv2/exiv2.hpp>
#include "LapesEye/core/FileScanner.h"
#include "LapesEye/core/MetaStore.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QDir>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QShortcut>
#include <QLineEdit>
#include <QPalette>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QResizeEvent>
#include <QMimeData>
#include <QDrag>
#include <QTimer>
#include <QMessageBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QFuture>
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <execinfo.h>
#include <QWindow>
#include <QDebug>
#include <QCursor>
#include <QCollator>
#include <algorithm>

namespace LapesEye {

// ═══════════════════════════════════════════════════════════════════════════════
// WIRTUALNA SIATKA — ZASADA DZIAŁANIA
//
// Zamiast tworzyć N widgetów ThumbnailItem dla N plików (co blokuje UI przy
// dużych folderach), utrzymujemy:
//
//  m_items   — QMap<QString,ThumbnailItem*> tylko dla widocznych + bufor
//              (typowo 2-4 rzędy powyżej i poniżej viewport)
//  m_pool    — QList<ThumbnailItem*> zwolnionych widgetów do reużycia
//
// m_container ma stały rozmiar = całkowita wysokość siatki (wszystkich rzędów),
// dzięki czemu pasek przewijania działa poprawnie bez tworzenia wszystkich widgetów.
//
// Podczas scrollowania:
//   valueChanged → virt_update_visible_rows()
//     → recykluje rzędy które wyszły poza viewport (release_to_pool)
//     → tworzy/recykluje widgety dla nowych rzędów (acquire_from_pool/new)
//     → ładuje miniatury tylko dla nowo widocznych
//
// Zmiana rozmiaru (suwak zoom):
//   set_thumb_size → debounce 200ms → do_zoom_rebuild
//     → virt_full_rebuild() — przebudowuje geometrię wszystkich aktywnych
//     → virt_update_visible_rows()
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Konstruktor ──────────────────────────────────────────────────────────────

ThumbnailGrid::ThumbnailGrid(ThumbWorker* worker, QWidget* parent)
    : QWidget(parent), m_worker(worker)
{
    // rename_completed jest emitowany z wątku tła — Qt AutoConnection kolejkuje do głównego wątku
    connect(this, &ThumbnailGrid::rename_completed,
            this, &ThumbnailGrid::on_rename_completed,
            Qt::QueuedConnection);
    connect(this, &ThumbnailGrid::watcher_unblock, this, [this]() {
        if (m_fs_watcher) m_fs_watcher->blockSignals(false);
    }, Qt::QueuedConnection);
    // Synchronizuj worker size z m_thumb_size natychmiast przy starcie
    if (m_worker) m_worker->set_thumb_size(m_thumb_size);
    // ThumbnailCanvas — jeden widget rysujący wszystko sam, bez child widgetów
    // Eliminuje problem Wayland z buforami child widgetów (niebieski highlight)
    m_canvas = new ThumbnailCanvas(this);
    m_canvas->setObjectName("thumbCanvas");

    // m_container nadal istnieje jako wrapper dla zgodności ze starym kodem
    // ale nie jest używany do rysowania — m_canvas to robi
    m_container = m_canvas;  // aliasujemy

    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_canvas);
    m_scroll->setWidgetResizable(false);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setFrameShadow(QFrame::Plain);
    m_scroll->setLineWidth(0);
    m_scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    m_scroll->setAcceptDrops(true);   // ThumbnailGrid obsługuje drop przez event filter
    m_scroll->viewport()->setAcceptDrops(true);
    m_canvas->installEventFilter(this);  // przechwytuj drag eventy z canvas
    m_scroll->installEventFilter(this);
    m_scroll->viewport()->installEventFilter(this);
    m_scroll->viewport()->setAttribute(Qt::WA_OpaquePaintEvent, false);
    m_scroll->viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->viewport()->setFocusPolicy(Qt::NoFocus);

    // Połącz sygnały canvas z ThumbnailGrid
    connect(m_canvas, &ThumbnailCanvas::clicked,
            this, &ThumbnailGrid::on_item_clicked);
    connect(m_canvas, &ThumbnailCanvas::press_in_empty, this, [this](const QPoint& global_pos) {
        QPoint local = mapFromGlobal(global_pos);
        m_drag_start_pos  = local;
        m_drag_active     = false;
        m_rubber_active   = false;
        m_rubber_origin   = local;
        QPoint vp_in_grid = m_scroll->viewport()->mapTo(this, QPoint(0,0));
        int sy = m_scroll->verticalScrollBar()->value();
        int sx = m_scroll->horizontalScrollBar()->value();
        m_rubber_origin_c = local - vp_in_grid + QPoint(sx, sy);
        m_rubber_rect     = QRect();
    });
    connect(m_canvas, &ThumbnailCanvas::rubber_move, this, [this](const QPoint& global_pos) {
        // Symuluj mouseMoveEvent dla rubber band
        QPoint local = mapFromGlobal(global_pos);
        m_rubber_cur = local;
        if ((local - m_drag_start_pos).manhattanLength() >= QApplication::startDragDistance()) {
            m_rubber_active = true;
            update_rubber_selection(QRect());
            // Rysuj overlay
            if (m_overlay) {
                m_overlay->setGeometry(m_scroll->viewport()->rect());
                m_overlay->show_rect(QRect(
                    m_scroll->viewport()->mapFrom(this, m_rubber_origin),
                    m_scroll->viewport()->mapFrom(this, local)).normalized());
            }
        }
    });
    connect(m_canvas, &ThumbnailCanvas::double_clicked,
            this, &ThumbnailGrid::on_item_double_clicked);
    connect(m_canvas, &ThumbnailCanvas::context_menu_requested,
            this, &ThumbnailGrid::on_context_menu);
    connect(m_canvas, &ThumbnailCanvas::drag_move,
            this, &ThumbnailGrid::on_item_drag_move);
    connect(m_canvas, &ThumbnailCanvas::drag_released,
            this, &ThumbnailGrid::on_item_drag_released);
    connect(m_canvas, &ThumbnailCanvas::rename_requested,
            this, &ThumbnailGrid::on_rename_requested);
    // Prefetch przy hover — ładuj miniaturę zanim user kliknie
    connect(m_canvas, &ThumbnailCanvas::hovered,
            this, [this](const QString& path) {
                if (!path.isEmpty() && m_worker)
                    m_worker->request(path, 20);
            });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_scroll);

    m_loading_label = new QLabel("⏳ Ładowanie...", this);
    m_loading_label->setAlignment(Qt::AlignCenter);
    m_loading_label->setStyleSheet("color: #666; font-size: 14px; background: transparent;");
    m_loading_label->hide();
    layout->addWidget(m_loading_label);

    // ── Timery ────────────────────────────────────────────────────────────────
    // Debounce zoom — 200ms po puszczeniu suwaka
    m_zoom_timer = new QTimer(this);
    m_zoom_timer->setSingleShot(true);
    m_zoom_timer->setInterval(200);
    QObject::connect(m_zoom_timer, &QTimer::timeout,
                     this, &ThumbnailGrid::do_zoom_rebuild);

    // Debounce scroll — ładuj miniatury 100ms po zatrzymaniu scrollowania
    m_visible_timer = new QTimer(this);
    m_visible_timer->setSingleShot(true);
    m_visible_timer->setInterval(100);
    QObject::connect(m_visible_timer, &QTimer::timeout,
                     this, &ThumbnailGrid::request_visible_thumbs);

    // Debounce resize — przebuduj layout 200ms po zatrzymaniu zmiany rozmiaru okna
    m_resize_timer = new QTimer(this);
    m_resize_timer->setSingleShot(true);
    m_resize_timer->setInterval(200);
    QObject::connect(m_resize_timer, &QTimer::timeout, this, [this]() {
        virt_full_rebuild();
    });

    // Rubber band auto-scroll — 60fps
    m_rubber_timer = new QTimer(this);
    m_rubber_timer->setSingleShot(true);
    m_rubber_timer->setInterval(16);
    QObject::connect(m_rubber_timer, &QTimer::timeout, this, [this]() {
        if (!m_rubber_active || !m_overlay) return;
        QPoint global_cur = QCursor::pos();
        QPoint vp_cur = m_scroll->viewport()->mapFromGlobal(global_cur);
        QRect  vp_rect = m_scroll->viewport()->rect();
        const int ZONE = 40;
        int dy = 0;
        if      (vp_cur.y() < ZONE)                    dy = -(ZONE - vp_cur.y()) / 2;
        else if (vp_cur.y() > vp_rect.height() - ZONE) dy =  (vp_cur.y() - vp_rect.height() + ZONE) / 2;
        if (dy != 0) {
            auto* sb = m_scroll->verticalScrollBar();
            sb->setValue(qBound(sb->minimum(), sb->value() + dy, sb->maximum()));
            QPoint vp_tl = m_scroll->viewport()->mapTo(this, QPoint(0,0));
            QPoint new_cur = mapFromGlobal(global_cur);
            if (dy > 0) new_cur.setY(vp_tl.y() + vp_rect.height());
            else        new_cur.setY(vp_tl.y());
            QPoint vp_tl2 = m_scroll->viewport()->mapTo(this, QPoint(0,0));
            int sv2 = m_scroll->verticalScrollBar()->value();
            int orig_sy2 = m_rubber_origin_c.y() - sv2 + vp_tl2.y();
            int top2    = (orig_sy2 < vp_tl2.y()) ? vp_tl2.y() : qMin(orig_sy2, new_cur.y());
            int bottom2 = (orig_sy2 > vp_tl2.y() + vp_rect.height())
                          ? vp_tl2.y() + vp_rect.height()
                          : qMax(orig_sy2, new_cur.y());
            int left2  = qMin(m_rubber_origin.x(), new_cur.x());
            int right2 = qMax(m_rubber_origin.x(), new_cur.x());
            QRect sel(QPoint(left2, top2), QPoint(right2, bottom2));
            QPoint vp_o2 = m_scroll->viewport()->mapFrom(this, QPoint(0, 0));
            m_overlay->setGeometry(m_scroll->viewport()->rect());
            m_overlay->show_rect(sel.translated(vp_o2));
            m_rubber_cur = new_cur;
            update_rubber_selection(QRect());
            m_rubber_timer->start(16);
        } else {
            update_rubber_selection(QRect());
        }
    });

    // ── Scroll → aktualizacja wirtualnych rzędów ──────────────────────────────
    QObject::connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
                     this, [this](int scroll_val) {
                         if (m_canvas) m_canvas->notify_scroll_start();
                         m_visible_timer->start();

                         // Rubber band podczas scrollowania
                         if (m_rubber_active && m_overlay) {
                             m_last_scroll_val = scroll_val;
                             QPoint vp_tl  = m_scroll->viewport()->mapTo(this, QPoint(0,0));
                             int vp_top    = vp_tl.y();
                             int vp_bottom = vp_tl.y() + m_scroll->viewport()->height();
                             QPoint cur_in_grid = mapFromGlobal(QCursor::pos());
                             int origin_screen_y2 = m_rubber_origin_c.y() - scroll_val + vp_top;
                             bool origin_above = origin_screen_y2 < vp_top;
                             bool origin_below = origin_screen_y2 > vp_bottom;
                             int top, bottom;
                             if (origin_above) {
                                 top    = vp_top;
                                 bottom = qMax(cur_in_grid.y(), vp_top);
                             } else if (origin_below) {
                                 top    = qMin(cur_in_grid.y(), vp_bottom);
                                 bottom = vp_bottom;
                             } else {
                                 top    = qMin(origin_screen_y2, cur_in_grid.y());
                                 bottom = qMax(origin_screen_y2, cur_in_grid.y());
                             }
                             int left  = qMin(m_rubber_origin.x(), cur_in_grid.x());
                             int right = qMax(m_rubber_origin.x(), cur_in_grid.x());
                             QRect sel(QPoint(left, top), QPoint(right, bottom));
                             QPoint vp_os = m_scroll->viewport()->mapFrom(this, QPoint(0, 0));
                             m_overlay->setGeometry(m_scroll->viewport()->rect());
                             m_overlay->show_rect(sel.translated(vp_os));
                             m_rubber_cur = cur_in_grid;
                             update_rubber_selection(QRect());
                         }
                     });

    QObject::connect(m_worker, &ThumbWorker::thumb_ready,
                     this, &ThumbnailGrid::on_thumb_ready);

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);        // my malujemy tło sami
    setAttribute(Qt::WA_NoSystemBackground);       // Qt nie maluje tła za nas (brak focus rect od systemu)
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->viewport()->setFocusPolicy(Qt::NoFocus);
    m_container->setFocusPolicy(Qt::NoFocus);
    setAcceptDrops(true);
    // Overlay jest dzieckiem m_scroll->viewport() — renderowany NAD kontenerem z thumbnailami,
    // a po hide() viewport automatycznie odmalowuje swoje tło w miejscu overlay.
    m_overlay = new RubberOverlay(m_scroll->viewport());
    m_overlay->raise();

    // Cover przykrywa viewport gdy folder jest pusty — zapobiega "ghost" items z Wayland
    m_empty_cover = new QWidget(m_scroll->viewport());
    m_empty_cover->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_empty_cover->setAutoFillBackground(true);
    {
        QPalette p = m_empty_cover->palette();
        p.setColor(QPalette::Window, QColor("#1e1e1e"));
        m_empty_cover->setPalette(p);
    }
    m_empty_cover->hide();

    // Enter obsługiwany przez keyPressEvent (gdy ThumbnailGrid ma fokus)
    // i przez eventFilter na scroll/viewport

    // QFileSystemWatcher — obserwuje aktualny folder i wykrywa zewnętrzne zmiany
    // (np. przeniesienie pliku przez Dolphin menu "Przenieś" bez drag)
    m_fs_watcher = new QFileSystemWatcher(this);
    QObject::connect(m_fs_watcher, &QFileSystemWatcher::directoryChanged,
                     this, [this](const QString& dir) {
        if (dir != m_current_dir) return;
        // Folder się zmienił — sprawdź które pliki zniknęły
        // Uwaga: pomiń jeśli to rename (m_all_files już ma nową ścieżkę dla tego pliku)
        QSet<QString> current_on_disk;
        QDir d(m_current_dir);
        for (const QString& f : d.entryList(QDir::Files))
            current_on_disk.insert(d.filePath(f));

        QStringList missing;
        for (const auto& f : m_all_files) {
            // Sprawdź przez m_all_files (już zaktualizowane przez rename)
            // a nie przez exists — żeby nie usunąć przy rename
            if (!f.is_dir && !current_on_disk.contains(f.path))
                missing << f.path;
        }
        if (!missing.isEmpty()) {
            qDebug() << "[Lape] fs_watcher: zniknęło" << missing.size() << "plików";
            remove_items_in_place(missing);
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// WIRTUALNA SIATKA — IMPLEMENTACJA
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Pomocnicze obliczenia geometrii ─────────────────────────────────────────

// ─── Geometria wirtualnej siatki ─────────────────────────────────────────────
// ThumbnailItem wymiary (PAD=4, NAME_H=15, STARS_H=11):
//   item_w = thumb + 2*PAD             = thumb + 8
//   item_h = thumb + 2*PAD+NAME_H+STARS_H+4 = thumb + 34
// Komórka:
//   cell_w = item_w + GAP = thumb + 14
//   cell_h = item_h + GAP = thumb + 40
// Pozycja:
//   x = col * cell_w + (cell_w - item_w)/2 = col*cell_w + GAP/2
//   y = VIRT_TOP + row * cell_h + (cell_h - item_h)/2

static constexpr int VIRT_ITEM_W_EXTRA = 8;   // PAD=4, item_w = thumb + 8
static constexpr int VIRT_ITEM_H_EXTRA = 34;  // PAD=4, NAME_H=15, STARS_H=11, +4
static constexpr int VIRT_GAP = 6;
static constexpr int VIRT_TOP = 4;

// Efektywny rozmiar miniatury — przy dużym zoom dopasowuje do viewportu
int ThumbnailGrid::effective_thumb_size() const {
    int avail = m_scroll->viewport()->width();
    if (avail < 10) avail = m_scroll->width() - 20;
    if (avail < 10) avail = 400;
    int cell_w = m_thumb_size + VIRT_ITEM_W_EXTRA + VIRT_GAP;
    if (cell_w >= avail) {
        // 1 kolumna: miniatura dopasowana do viewportu z marginesem GAP z każdej strony
        int eth = avail - VIRT_ITEM_W_EXTRA - VIRT_GAP;
        return qMax(10, eth);
    }
    return m_thumb_size;
}

int ThumbnailGrid::virt_cell_size() const {
    // cell_h zawsze spójne z effective_thumb_size
    return effective_thumb_size() + VIRT_ITEM_H_EXTRA + VIRT_GAP;
}

int ThumbnailGrid::virt_cols() const {
    int avail = m_scroll->viewport()->width();
    if (avail < 10) avail = m_scroll->width() - 20;
    if (avail < 10) avail = 400;
    int cell_w = m_thumb_size + VIRT_ITEM_W_EXTRA + VIRT_GAP;
    if (cell_w >= avail) return 1;
    return avail / cell_w;
}

int ThumbnailGrid::virt_total_height() const {
    if (m_visible.isEmpty()) return 0;
    int rows = (m_visible.size() + virt_cols() - 1) / virt_cols();
    return VIRT_TOP + rows * virt_cell_size() + VIRT_GAP;
}

QRect ThumbnailGrid::virt_item_rect(int idx) const {
    int eth    = effective_thumb_size();
    int item_w = eth + VIRT_ITEM_W_EXTRA;
    int item_h = eth + VIRT_ITEM_H_EXTRA;
    int cell_h = eth + VIRT_ITEM_H_EXTRA + VIRT_GAP;  // == virt_cell_size()

    int cols  = virt_cols();
    int avail = m_scroll->viewport()->width();
    if (avail < 10) avail = m_scroll->width() - 20;
    if (avail < 10) avail = 400;

    // cell_w dla pozycji X — stały rozmiar komórki niezależnie od liczby kolumn
    int cell_w = m_thumb_size + VIRT_ITEM_W_EXTRA + VIRT_GAP;
    // Przy 1 kolumnie NIE rozciągamy do całego viewportu — item ma swój naturalny rozmiar

    int row = idx / cols;
    int col = idx % cols;

    // Wyśrodkuj siatkę poziomo gdy jest reszta miejsca
    int grid_w   = cols * cell_w;
    int offset_x = (avail > grid_w) ? (avail - grid_w) / 2 : 0;

    int x = offset_x + col * cell_w + (cell_w - item_w) / 2;
    int y = VIRT_TOP + row * cell_h + (cell_h - item_h) / 2;
    return QRect(x, y, item_w, item_h);
}

// ─── Pełna przebudowa (przy zmianie cols lub thumb_size) ─────────────────────

void ThumbnailGrid::virt_full_rebuild() {
    PERF_SCOPE("virt_full_rebuild");
    int saved_sv = m_scroll->verticalScrollBar()->value();

    int w = m_scroll->viewport()->width();
    if (w < 10) w = m_scroll->width() - m_scroll->verticalScrollBar()->sizeHint().width();
    if (w < 10) w = width();
    if (w < 10 && parentWidget()) w = parentWidget()->width() - 20;
    if (w < 10) w = 800;

    m_scroll->setStyleSheet("QScrollArea { border: none; }");
    m_scroll->viewport()->setStyleSheet("");
    m_scroll->show();

    // Zaktualizuj canvas — przekazuje listę plików do rysowania
    m_canvas->setFixedWidth(w);
    m_canvas->set_thumb_size(m_thumb_size);
    sync_canvas();

    // Zwolnij stare widgety z puli (kompatybilność — pool nie jest już używany)
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        it.value()->hide();
        m_pool << it.value();
    }
    m_items.clear();

    m_virt_first_visible_row = -1;
    m_virt_last_visible_row  = -1;

    m_scroll->verticalScrollBar()->setValue(saved_sv);

    if (m_empty_cover) m_empty_cover->hide();
    QTimer::singleShot(30, this, &ThumbnailGrid::request_visible_thumbs);
}

// ─── Aktualizacja widocznych rzędów (wywoływana przy scrollowaniu) ────────────

void ThumbnailGrid::virt_update_visible_rows() {
    if (m_visible.isEmpty()) {
        m_canvas->set_items({});
        m_scroll->viewport()->setStyleSheet("background: #1e1e1e;");
        m_scroll->setStyleSheet("QScrollArea { border: none; }");
        if (m_overlay) { m_overlay->hide_rect(); m_overlay->hide(); }
        return;
    }
    m_scroll->viewport()->setStyleSheet("");
    m_scroll->setStyleSheet("QScrollArea { border: none; }");
    if (m_empty_cover && m_empty_cover->isVisible()) m_empty_cover->hide();
    sync_canvas();
}

// ─── Stara nazwa dla kompatybilności ─────────────────────────────────────────

void ThumbnailGrid::rebuild_layout() {
    virt_full_rebuild();
}

// ─── sync_canvas — synchronizuje stan ThumbnailGrid → ThumbnailCanvas ────────

void ThumbnailGrid::sync_canvas() {
    PERF_SCOPE("sync_canvas");
    if (!m_canvas) return;

    // Ustaw szerokość PRZED set_items — żeby cols() było poprawne przy obliczaniu wysokości
    int w = m_scroll->viewport()->width();
    if (w < 10) w = m_scroll->width() - m_scroll->verticalScrollBar()->sizeHint().width();
    if (w < 10) w = width();
    if (w < 10) w = 800;
    m_canvas->setFixedWidth(w);

    QVector<ThumbnailCanvasItem> items;
    items.reserve(m_visible.size());

    for (const auto& f : m_visible) {
        ThumbnailCanvasItem ci;
        ci.file = f;
        // Metadane — tylko z cache
        auto meta_it = m_meta_cache.find(f.path);
        if (meta_it != m_meta_cache.end()) {
            ci.meta = meta_it.value();
            ci.meta_loaded = true;
        }
        items << ci;
    }
    m_canvas->set_thumb_size(m_thumb_size);
    m_canvas->set_items(std::move(items));
    m_canvas->set_selected(m_selected);
    QSet<QString> cut_set;
    if (m_cut_mode) cut_set = QSet<QString>(m_clipboard_paths.begin(), m_clipboard_paths.end());
    m_canvas->set_cut_paths(cut_set);

    // Załaduj metadane folderów asynchronicznie — kolory etykiet widoczne bez klikania
    // Tylko foldery których nie ma w cache (nie blokuje UI)
    // Zbierz pliki bez metadanych w cache
    QStringList files_to_load;
    for (const auto& f : m_visible)
        if (!m_meta_cache.contains(f.path))
            files_to_load << f.path;

    if (!files_to_load.isEmpty()) {
        auto fut = QtConcurrent::run([this, files_to_load]() {
            // Grupuj po folderze — czytaj katalog JSON raz per folder
            QMap<QString, QStringList> by_dir;
            for (const QString& path : files_to_load)
                by_dir[QFileInfo(path).dir().absolutePath()] << path;

            for (auto it = by_dir.begin(); it != by_dir.end(); ++it) {
                for (const QString& path : it.value()) {
                    FileMetadata meta = MetaStore::load(path);
                    if (meta.color_label != ColorLabel::None || meta.rating > 0) {
                        QMetaObject::invokeMethod(this, [this, path, meta]() {
                            m_meta_cache[path] = meta;
                            if (m_canvas) m_canvas->set_metadata(path, meta);
                        }, Qt::QueuedConnection);
                    } else {
                        QMetaObject::invokeMethod(this, [this, path, meta]() {
                            m_meta_cache[path] = meta;
                        }, Qt::QueuedConnection);
                    }
                }
            }
        });
        Q_UNUSED(fut);
    }
}

// ─── dir_at — ścieżka folderu pod pozycją (w koordinatach ThumbnailGrid) ─────

QString ThumbnailGrid::dir_at(const QPoint& pos_in_grid) const {
    if (!m_canvas) return {};
    QPoint in_canvas = m_canvas->mapFrom(const_cast<ThumbnailGrid*>(this), pos_in_grid);
    int idx = m_canvas->index_at(in_canvas);
    if (idx >= 0 && idx < m_visible.size() && m_visible[idx].is_dir)
        return m_visible[idx].path;
    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// WIDOCZNOŚĆ I MINIATURY
// ═══════════════════════════════════════════════════════════════════════════════

bool ThumbnailGrid::is_item_visible(ThumbnailItem* item) const {
    if (!item || !m_scroll) return false;
    QRect viewport_rect = m_scroll->viewport()->rect();
    QPoint item_pos = item->mapTo(m_scroll->viewport(), QPoint(0, 0));
    QRect item_rect(item_pos, item->size());
    viewport_rect.adjust(0, -(m_thumb_size + 20), 0, m_thumb_size + 20);
    return viewport_rect.intersects(item_rect);
}

// ═══════════════════════════════════════════════════════════════════════════════
// WIDOCZNOŚĆ I MINIATURY
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::request_visible_thumbs() {
    PERF_SCOPE("request_visible_thumbs");
    if (!m_canvas || m_visible.isEmpty()) return;

    int sv   = m_scroll->verticalScrollBar()->value();
    int vp_h = m_scroll->viewport()->height();
    if (vp_h < 10) vp_h = height();
    int ch   = m_canvas->cell_size() + m_canvas->gap();
    int c    = m_canvas->cols();
    if (ch <= 0 || c <= 0) return;

    // Tylko widoczne + 3 wiersze buforu — wysoki priorytet
    int first_row = qMax(0, sv / ch - 3);
    int last_row  = (sv + vp_h) / ch + 3;
    int first_idx = first_row * c;
    int last_idx  = qMin((last_row + 1) * c - 1, (int)m_visible.size() - 1);

    for (int i = first_idx; i <= last_idx; ++i)
        m_worker->request(m_visible[i].path, 10);

    emit thumb_progress(m_thumbs_loaded, (int)m_all_files.size());
}

// ═══════════════════════════════════════════════════════════════════════════════
// ŁADOWANIE FOLDERU
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::load_collection(const QStringList& paths, const QString& title) {
    m_current_dir.clear();
    m_collection_mode = true;
    m_all_files.clear();
    m_visible.clear();
    m_selected.clear();
    m_primary.clear();
    m_shift_anchor.clear();  // reset anchor — nowy folder, nowa selekcja
    m_thumbs_loaded = 0;
    m_meta_cache.clear();

    // Wyczyść widgety
    for (auto* item : m_items) { item->set_selected(false); item->set_pixmap(QPixmap{}); item->hide(); m_pool << item; }
    m_items.clear();
    m_virt_first_visible_row = -1;
    m_virt_last_visible_row  = -1;

    for (const QString& path : paths) {
        QFileInfo fi(path);
        if (!fi.exists()) continue;
        ScannedFile sf;
        sf.path   = path;
        sf.name   = fi.fileName();
        sf.is_dir = fi.isDir();
        sf.is_raw = FileScanner::is_raw(path);
        sf.is_psd = FileScanner::is_psd(path);
        sf.size   = fi.size();
        sf.mtime  = fi.lastModified().toSecsSinceEpoch();
        m_all_files << sf;
    }

    apply_filter_and_rebuild();
    setFocus();

    int total = 0;
    for (const auto& f : m_visible) if (!f.is_dir) ++total;
    emit thumb_progress(0, total);
    emit scan_finished();
    (void)title;
}

void ThumbnailGrid::load_folder(const QString& dir_path) {
    m_collection_mode = false;
    if (dir_path == m_current_dir) return;

    // Aktualizuj QFileSystemWatcher — obserwuj nowy folder
    if (m_fs_watcher) {
        if (!m_fs_watcher->directories().isEmpty())
            m_fs_watcher->removePaths(m_fs_watcher->directories());
        m_fs_watcher->addPath(dir_path);
    }

    // Ukryj cover — nowy folder ma własną zawartość
    if (m_empty_cover) m_empty_cover->hide();

    m_current_dir = dir_path;

    // Wyczyść store pixmap TYLKO gdy to faktycznie nowy folder
    // (guard dir_path == m_current_dir powyżej gwarantuje że tu trafiamy tylko przy zmianie)
    if (m_canvas) m_canvas->clear_pixmap_store();

    m_zoom_timer->stop();
    m_visible_timer->stop();
    if (m_rubber_timer) m_rubber_timer->stop();
    m_rubber_active  = false;
    m_rubber_rect    = QRect();
    m_rubber_origin  = QPoint();
    m_rubber_origin_c = QPoint();
    m_rubber_cur     = QPoint();
    if (m_overlay) { m_overlay->hide_rect(); m_overlay->hide(); }
    m_worker->cancel_all();

    // Wyczyść widgety do puli (nie delete — reużywamy)
    for (auto* item : m_items) {
        item->set_selected(false);
        item->set_cut_mode(false);
        item->set_pixmap(QPixmap{});
        item->hide();
        m_pool << item;
    }
    m_items.clear();
    // Wyczyść WSZYSTKIE widgety w puli — stare pixmapy z poprzedniego folderu
    for (auto* item : m_pool) {
        item->set_pixmap(QPixmap{});
        item->hide();
    }
    m_virt_first_visible_row = -1;
    m_virt_last_visible_row  = -1;

    m_selected.clear();
    m_primary.clear();
    m_shift_anchor.clear();  // reset anchor — nowy folder
    m_meta_cache.clear();
    m_thumbs_loaded = 0;
    // Wyczyść cut mode — przecięte pliki są już nieaktualne po zmianie folderu
    // (clipboard systemowy nadal je trzyma, ale efekt wizualny czyścimy)
    if (m_cut_mode) {
        m_cut_mode = false;
        m_clipboard_paths.clear();
    }

    if (dir_path.isEmpty()) return;

    QString scan_dir = dir_path;
    // Pokaż "Ładowanie..." natychmiast — zamiast pustej siatki
    m_scroll->setStyleSheet("QScrollArea { border: none; }");
    m_scroll->viewport()->setStyleSheet(
        QString("background: %1;").arg(palette().color(QPalette::Window).name()));
    if (m_loading_label) { m_loading_label->show(); m_loading_label->raise(); }

    auto* watcher = new QFutureWatcher<QList<ScannedFile>>(this);

    QObject::connect(watcher, &QFutureWatcher<QList<ScannedFile>>::finished,
                     this, [this, watcher, scan_dir]() {
        if (m_current_dir == scan_dir) {
            if (m_loading_label) m_loading_label->hide();
            m_all_files = watcher->result();
            m_thumbs_loaded = 0;
            apply_filter_and_rebuild();
            setFocus();
            QTimer::singleShot(0, this, [this, scan_dir]() {
                if (m_current_dir == scan_dir) {
                    virt_full_rebuild();
                    request_visible_thumbs();
                    setFocus();
                }
            });
            int total = 0;
            for (const auto& f : m_visible) if (!f.is_dir) ++total;
            emit thumb_progress(0, total);
            emit scan_finished();
            if (m_filter.pick_flag != PickFlag::None ||
                m_filter.min_rating > 0 ||
                m_filter.color_label != ColorLabel::None) {
                QTimer::singleShot(300, this, [this]() {
                    apply_filter_and_rebuild();
                });
            }
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([scan_dir]() {
        FileScanner scanner;
        return scanner.scan_sync(scan_dir, false);
    }));
}

// ═══════════════════════════════════════════════════════════════════════════════
// FILTROWANIE I SORTOWANIE
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::set_filter(const GridFilter& f) {
    m_filter = f;
    apply_filter_and_rebuild();
}

bool ThumbnailGrid::passes_filter(const ScannedFile& f) const {
    if (f.is_dir) return true;
    if (m_filter.only_raw && !f.is_raw) return false;
    if (m_filter.only_psd && !f.is_psd) return false;
    if (m_filter.only_jpg) {
        QString ext = QFileInfo(f.name).suffix().toLower();
        if (ext != "jpg" && ext != "jpeg") return false;
    }
    if (!m_filter.name_contains.isEmpty() &&
        !f.name.contains(m_filter.name_contains, Qt::CaseInsensitive)) return false;

    if (m_filter.use_date_from || m_filter.use_date_to) {
        QDate file_date = QDateTime::fromSecsSinceEpoch(f.mtime).date();
        if (m_filter.use_date_from && file_date < m_filter.date_from) return false;
        if (m_filter.use_date_to   && file_date > m_filter.date_to)   return false;
    }

    if (m_filter.use_size_min && f.size < m_filter.size_min_mb * 1024 * 1024) return false;
    if (m_filter.use_size_max && f.size > m_filter.size_max_mb * 1024 * 1024) return false;

    bool needs_meta = m_filter.min_rating > 0
                   || m_filter.color_label != ColorLabel::None
                   || m_filter.pick_flag   != PickFlag::None
                   || m_filter.use_dim
                   || !m_filter.camera_contains.isEmpty()
                   || !m_filter.lens_contains.isEmpty()
                   || m_filter.iso_min > 0 || m_filter.iso_max > 0
                   || m_filter.focal_min > 0 || m_filter.focal_max > 0
                   || m_filter.fnumber_min > 0 || m_filter.fnumber_max > 0
                   || m_filter.exposure_denom_min > 0
                   || m_filter.exposure_denom_max > 0;

    if (needs_meta) {
        const FileMetadata& meta = cached_meta(f.path);
        if (m_filter.min_rating > 0 && meta.rating < m_filter.min_rating) return false;
        if (m_filter.color_label != ColorLabel::None &&
            meta.color_label != m_filter.color_label) return false;
        if (m_filter.pick_flag != PickFlag::None &&
            meta.pick_flag != m_filter.pick_flag) return false;
        const auto& ex = meta.exif;
        if (!m_filter.camera_contains.isEmpty()) {
            QString cam = ex.camera_make + " " + ex.camera_model;
            if (!cam.contains(m_filter.camera_contains, Qt::CaseInsensitive)) return false;
        }
        if (!m_filter.lens_contains.isEmpty() &&
            !ex.lens.contains(m_filter.lens_contains, Qt::CaseInsensitive)) return false;
        if (m_filter.iso_min > 0 && ex.iso < m_filter.iso_min) return false;
        if (m_filter.iso_max > 0 && ex.iso > m_filter.iso_max) return false;
        if (m_filter.focal_min > 0 && ex.focal_length < m_filter.focal_min) return false;
        if (m_filter.focal_max > 0 && ex.focal_length > m_filter.focal_max) return false;
        if (m_filter.fnumber_min > 0 && ex.fnumber < m_filter.fnumber_min) return false;
        if (m_filter.fnumber_max > 0 && ex.fnumber > m_filter.fnumber_max) return false;
        if (m_filter.exposure_denom_min > 0 && ex.exposure_time > 0) {
            if (ex.exposure_time > 1.0 / m_filter.exposure_denom_min) return false;
        }
        if (m_filter.exposure_denom_max > 0 && ex.exposure_time > 0) {
            if (ex.exposure_time < 1.0 / m_filter.exposure_denom_max) return false;
        }
        if (m_filter.use_dim &&
            (ex.width < m_filter.dim_w_min || ex.height < m_filter.dim_h_min)) return false;
    }
    return true;
}

const FileMetadata& ThumbnailGrid::cached_meta(const QString& path) const {
    auto it = m_meta_cache.find(path);
    if (it != m_meta_cache.end()) {
        bool needs_exif = m_filter.focal_min > 0 || m_filter.focal_max > 0
                       || m_filter.fnumber_min > 0 || m_filter.fnumber_max > 0
                       || m_filter.exposure_denom_min > 0 || m_filter.exposure_denom_max > 0
                       || m_filter.iso_min > 0 || m_filter.iso_max > 0
                       || m_filter.use_dim
                       || !m_filter.camera_contains.isEmpty()
                       || !m_filter.lens_contains.isEmpty();
        if (!needs_exif || it.value().loaded_exif)
            return it.value();
    }
    FileMetadata meta = MetaStore::load(path);
    // Ogranicz cache — usuń najstarsze wpisy gdy przekroczy limit
    if (m_meta_cache.size() >= META_CACHE_MAX) {
        auto first = m_meta_cache.begin();
        m_meta_cache.erase(first);
    }
    return m_meta_cache.insert(path, meta).value();
}

void ThumbnailGrid::invalidate_meta_cache(const QString& path) {
    m_meta_cache.remove(path);
}

void ThumbnailGrid::apply_filter_and_rebuild() {
    PERF_SCOPE("apply_filter_and_rebuild");
    m_visible.clear();
    for (const auto& f : m_all_files)
        if (passes_filter(f)) m_visible.append(f);

    QCollator col;
    col.setNumericMode(true);
    col.setCaseSensitivity(Qt::CaseInsensitive);

    auto cmp = [&](const ScannedFile& a, const ScannedFile& b) -> bool {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        switch (m_filter.sort_mode) {
            case SortMode::NameAsc:  return col.compare(a.name, b.name) < 0;
            case SortMode::NameDesc: return col.compare(a.name, b.name) > 0;
            case SortMode::DateAsc:  return a.mtime < b.mtime;
            case SortMode::DateDesc: return a.mtime > b.mtime;
            case SortMode::SizeAsc:  return a.size  < b.size;
            case SortMode::SizeDesc: return a.size  > b.size;
            case SortMode::TypeAsc: {
                QString ea = QFileInfo(a.name).suffix().toLower();
                QString eb = QFileInfo(b.name).suffix().toLower();
                int ct = ea.compare(eb, Qt::CaseInsensitive);
                if (ct != 0) return ct < 0;
                return col.compare(a.name, b.name) < 0;
            }
            default: return col.compare(a.name, b.name) < 0;
        }
    };
    std::stable_sort(m_visible.begin(), m_visible.end(), cmp);

    // Resetuj widgety z puli (kompatybilność)
    for (auto* item : m_items) { item->hide(); m_pool << item; }
    m_items.clear();
    m_virt_first_visible_row = -1;
    m_virt_last_visible_row  = -1;

    m_scroll->show();
    m_scroll->viewport()->setStyleSheet("");
    m_scroll->setStyleSheet("QScrollArea { border: none; }");
    m_pending_rebuild = false;

    // Zaktualizuj canvas
    int w = m_scroll->viewport()->width();
    if (w <= 10) w = m_scroll->width() - m_scroll->verticalScrollBar()->sizeHint().width();
    if (w <= 10) w = width();
    if (w <= 10 && parentWidget()) w = parentWidget()->width() - 20;
    if (w <= 10) w = 800;
    if (m_canvas) m_canvas->setFixedWidth(w);

    virt_update_visible_rows();
    QTimer::singleShot(30, this, &ThumbnailGrid::request_visible_thumbs);

    if (m_scroll->viewport()->width() < 10) {
        m_pending_rebuild = true;
        QTimer::singleShot(200, this, [this]() {
            if (!m_pending_rebuild) return;
            m_pending_rebuild = false;
            virt_full_rebuild();
            request_visible_thumbs();
        });
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ZOOM
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::set_thumb_size(int size) {
    if (size == m_pending_size) return;
    m_pending_size = size;
    m_zoom_timer->start();
}

void ThumbnailGrid::apply_thumb_size_now(int size) {
    // Natychmiastowe zastosowanie rozmiaru — bez debounce timera
    // Używane przy starcie programu żeby suwak działał od razu
    m_zoom_timer->stop();
    m_pending_size = size;
    if (size == m_thumb_size) return;
    m_thumb_size = size;
    m_worker->set_thumb_size(size);
    m_worker->cancel_all();
    if (m_canvas) {
        m_canvas->set_thumb_size(size);
        int w = m_scroll->viewport()->width();
        if (w < 10) w = 800;
        m_canvas->setFixedWidth(w);
    }
    virt_full_rebuild();
    QTimer::singleShot(30, this, &ThumbnailGrid::request_visible_thumbs);
}

void ThumbnailGrid::do_zoom_rebuild() {
    QString anchor = m_primary;
    if (anchor.isEmpty() && !m_selected.isEmpty())
        anchor = *m_selected.begin();
    if (anchor.isEmpty() && !m_visible.isEmpty())
        anchor = m_visible.first().path;

    m_thumb_size = m_pending_size;
    m_worker->set_thumb_size(m_thumb_size);
    m_worker->cancel_all();

    for (auto* item : m_items) item->set_thumb_size(m_thumb_size);
    for (auto* item : m_pool)  item->set_thumb_size(m_thumb_size);

    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        it.value()->hide(); m_pool << it.value();
    }
    m_items.clear();
    for (auto* item : m_pool) item->hide();
    m_virt_first_visible_row = -1;
    m_virt_last_visible_row  = -1;

    int w = qMax(m_scroll->viewport()->width(), 1);
    int _th = virt_total_height();
    int h = _th > 0 ? _th : 1;
    m_container->resize(w, h);

    // Najpierw przebuduj widok, potem przewiń do zaznaczonego
    virt_update_visible_rows();

    if (!anchor.isEmpty()) {
        int target_idx = -1;
        for (int i = 0; i < m_visible.size(); ++i)
            if (m_visible[i].path == anchor) { target_idx = i; break; }
        if (target_idx >= 0)
            navigate_to_index(target_idx);
    }

    QTimer::singleShot(30, this, &ThumbnailGrid::request_visible_thumbs);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ZDARZENIA OKNA
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::paintEvent(QPaintEvent* e) {
    QPainter p(this);
    p.fillRect(e->rect(), palette().color(QPalette::Window));
    // Rubber-band jest rysowany przez RubberOverlay (dziecko viewport) — nie tutaj
}

void ThumbnailGrid::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (m_empty_cover && m_empty_cover->isVisible())
        m_empty_cover->setGeometry(m_scroll->viewport()->rect());
    if (m_pending_rebuild && !m_all_files.isEmpty()) {
        // Pending rebuild — teraz mamy rozmiar, przebuduj od razu
        m_resize_timer->stop();
        apply_filter_and_rebuild();
        return;
    }
    if (e->size().width() == e->oldSize().width()) return;
    m_resize_timer->start();
}

void ThumbnailGrid::focusOutEvent(QFocusEvent* e) {
    QWidget::focusOutEvent(e);
    // Wyczyść rubber band gdy tracimy fokus (np. przy otwarciu menu)
    if (m_rubber_active) {
        m_rubber_timer->stop();
        m_rubber_active = false;
        m_rubber_rect   = QRect();
        if (m_overlay) m_overlay->hide_rect();
    }
}

void ThumbnailGrid::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    QTimer::singleShot(0, this, [this]() {
        if (m_pending_rebuild && !m_all_files.isEmpty()) {
            apply_filter_and_rebuild();
        } else if (!m_all_files.isEmpty() && m_items.isEmpty()) {
            apply_filter_and_rebuild();
        } else if (!m_all_files.isEmpty()) {
            virt_full_rebuild();
        }
        request_visible_thumbs();
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// SLOTY
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::on_thumb_ready(const QString& path, const QPixmap& pix,
                                    ThumbQuality /*quality*/)
{
    // Aktualizuj pixmapę w canvas — canvas odmaluje tylko ten item
    if (m_canvas) m_canvas->set_pixmap(path, pix);
    // Też w starym m_items dla kompatybilności
    auto it = m_items.find(path);
    if (it != m_items.end()) it.value()->set_pixmap(pix);

    ++m_thumbs_loaded;
    emit thumb_progress(m_thumbs_loaded, (int)m_all_files.size());
}

void ThumbnailGrid::on_item_clicked(const QString& path, Qt::KeyboardModifiers mods) {
    setFocus();
    m_drag_start_pos = mapFromGlobal(QCursor::pos());
    m_drag_active    = false;
    m_rubber_active  = false;

    // Klik w pustą przestrzeń (path="") — odznacz wszystko (problem 7)
    if (path.isEmpty()) {
        if (!m_selected.isEmpty()) {
            m_selected.clear();
            m_primary.clear();
            m_shift_anchor.clear();
            if (m_canvas) m_canvas->set_selected(m_selected);
            emit selection_changed({});
        }
        return;
    }

    // Znajdź indeks klikniętego elementu i przewiń minimalnie żeby był widoczny
    // Identyczne zachowanie jak strzałki — NIE centruj, tylko minimalne przewinięcie
    if (m_canvas) {
        for (int i = 0; i < m_visible.size(); ++i) {
            if (m_visible[i].path == path) {
                QRect r    = m_canvas->item_rect(i);
                int sv     = m_scroll->verticalScrollBar()->value();
                int vp_h   = m_scroll->viewport()->height();
                if (r.top() < sv) {
                    // Element wychodzi poza górę — przewiń do góry
                    m_scroll->verticalScrollBar()->setValue(r.top());
                } else if (r.bottom() > sv + vp_h) {
                    // Element wychodzi poza dół — przewiń minimalnie w dół
                    m_scroll->verticalScrollBar()->setValue(r.bottom() - vp_h);
                }
                // W pełni widoczny — nie ruszaj scrolla
                break;
            }
        }
    }

    m_click_path = path;
    m_click_mods = mods;

    if (mods & Qt::ControlModifier) {
        if (m_selected.contains(path)) m_selected.remove(path);
        else                            m_selected.insert(path);
        m_primary = path;
        m_shift_anchor = path;  // Ctrl+klik też ustawia anchor
        if (m_canvas) m_canvas->set_selected(m_selected);
        if (m_canvas) m_canvas->set_selected(m_selected);
        emit primary_changed(path); // L__LINE__
        emit selection_changed(selected_paths());
    } else if (mods & Qt::ShiftModifier) {
        // Shift+klik: zaznacz zakres od anchor do klikniętego
        // ZASTĄP całe zaznaczenie nowym zakresem (jak w Dolphin)
        // Anchor = m_shift_anchor (zapamiętany przy pierwszym kliknięciu bez Shift)
        QString anchor = m_shift_anchor.isEmpty() ? m_primary : m_shift_anchor;
        if (anchor.isEmpty()) anchor = path;
        int from = -1, to = -1, cur = 0;
        for (const auto& f : m_visible) {
            if (f.path == anchor) from = cur;
            if (f.path == path)   to   = cur;
            ++cur;
        }
        m_selected.clear();  // WYCZYŚĆ stare zaznaczenie - zastąp nowym zakresem
        if (from >= 0 && to >= 0) {
            if (from > to) std::swap(from, to);
            for (int i = from; i <= to; ++i)
                m_selected.insert(m_visible[i].path);
        } else {
            m_selected.insert(path);
        }
        // Nie zmieniaj m_primary - to jest punkt nawigacji (ostatni kliknięty)
        // m_shift_anchor pozostaje niezmieniony
        if (m_canvas) m_canvas->set_selected(m_selected);
        if (m_canvas) m_canvas->set_selected(m_selected);
        emit primary_changed(m_primary.isEmpty() ? path : m_primary); // L__LINE__
        emit selection_changed(selected_paths());
    } else {
        if (m_selected.contains(path) && m_selected.size() > 1) {
            m_primary = path;
            m_shift_anchor = path;  // KLUCZOWE: anchor też musi się zaktualizować
            if (m_canvas) m_canvas->set_selected(m_selected);
            if (m_canvas) m_canvas->set_selected(m_selected);
            emit primary_changed(path); // L__LINE__
        } else {
            m_selected.clear();
            m_selected.insert(path);
            m_primary = path;
            m_shift_anchor = path;
            if (m_canvas) m_canvas->set_selected(m_selected);
            emit primary_changed(path); // L__LINE__
            emit selection_changed(selected_paths());
        }
    }
}

void ThumbnailGrid::on_item_double_clicked(const QString& path) {
    bool is_dir = false;
    bool found = false;
    // Sprawdź w m_visible (aktywne) ORAZ m_all_files (pula wirtualna) — wirtualna
    // siatka trzyma w m_visible tylko aktualnie widoczne wiersze; folder może
    // być poza widokiem ale nadal istnieć.
    for (const auto& f : m_visible) {
        if (f.path == path) { is_dir = f.is_dir; found = true; break; }
    }
    if (!found) {
        for (const auto& f : m_all_files) {
            if (f.path == path) { is_dir = f.is_dir; found = true; break; }
        }
    }
    // Fallback — sprawdź bezpośrednio na dysku jeśli path nie został znaleziony
    // w żadnej z list (np. po zewnętrznym ruchu pliku, lub stan niespójny).
    if (!found) {
        QFileInfo fi(path);
        if (fi.exists()) {
            is_dir = fi.isDir();
            found = true;
        }
    }
    // Ostateczny guard — QFileInfo::isDir() jest wiarygodne nawet gdy nie ma w listach
    if (!found && !path.isEmpty()) {
        is_dir = QFileInfo(path).isDir();
        found = true;
    }
    if (is_dir)
        emit folder_navigate(path);
    else if (found)  // tylko gdy faktycznie znaleziony plik
        emit open_in_lape(path);
    // Jeśli !found — nic nie rób, NIE wywołuj open_in_lape z nieprawidłową ścieżką
}

void ThumbnailGrid::on_context_menu(const QString& path, const QPoint& pos) {
    if (!m_selected.contains(path)) {
        m_selected.clear();
        m_selected.insert(path);
        m_primary = path;
        if (m_canvas) m_canvas->set_selected(m_selected);
    }
    emit context_menu(selected_paths(), pos);
}


void ThumbnailGrid::rotate_selected(int degrees) {
    if (m_selected.isEmpty()) return;

    const QStringList paths(m_selected.begin(), m_selected.end());
    const QStringList raw_exts = {"arw","cr2","cr3","nef","nrw","orf",
                                  "raf","rw2","dng","pef","srw","x3f"};

    for (const QString& path : paths) {
        // 1. Zaktualizuj metadane (rotation)
        FileMetadata meta = MetaStore::load(path);
        meta.path     = path;
        meta.rotation = ((meta.rotation + degrees) % 360 + 360) % 360;
        MetaStore::save(meta);
        m_meta_cache[path] = meta;
        if (m_canvas) m_canvas->set_metadata(path, meta);

        QString ext = QFileInfo(path).suffix().toLower();
        bool is_raw = raw_exts.contains(ext);

        if (!is_raw) {
            // Natychmiast obróć miniaturę w canvas — nie czekaj na zapis pliku
            QString cache_key = path + "@" + QString::number(m_thumb_size);
            QPixmap pix;
            if (QPixmapCache::find(cache_key, &pix) && !pix.isNull()) {
                QTransform t; t.rotate(degrees);
                QPixmap rotated = pix.transformed(t, Qt::SmoothTransformation);
                QPixmapCache::insert(cache_key, rotated);
                if (m_canvas) m_canvas->set_pixmap(path, rotated);
            }
            QPixmapCache::remove("preview:" + path);

            // Obrót fizyczny i zapis metadanych asynchronicznie w tle
            int deg = degrees;
            int thumb_sz = m_thumb_size;
            ThumbCache* cache = m_worker ? m_worker->cache() : nullptr;
            (void)QtConcurrent::run([path, deg, cache, thumb_sz]() {
                QImage img(path);
                if (img.isNull()) return;

                QTransform t; t.rotate(deg);
                QImage rotated = img.transformed(t, Qt::SmoothTransformation);

                QString tmp_path = path + ".leye_rot_tmp";
                if (!rotated.save(tmp_path)) {
                    QFile::remove(tmp_path);
                    return;
                }

                // Kopiuj EXIF/IPTC/XMP z oryginału, ustaw Orientation=1
                try {
                    auto src = Exiv2::ImageFactory::open(path.toStdString());
                    src->readMetadata();
                    auto dst = Exiv2::ImageFactory::open(tmp_path.toStdString());
                    dst->readMetadata();
                    dst->setExifData(src->exifData());
                    dst->setIptcData(src->iptcData());
                    dst->setXmpData(src->xmpData());
                    dst->exifData()["Exif.Image.Orientation"] = uint16_t(1);
                    dst->writeMetadata();
                } catch (...) {}

                QFile::remove(path);
                QFile::rename(tmp_path, path);

                // Wyczyść SQLite cache — przy kolejnym request ThumbWorker
                // wygeneruje miniaturę z już obróconego pliku
                if (cache) cache->remove(path);
            });
        } else {
            // RAW (ARW itp.): nie modyfikujemy pliku.
            // Pobierz aktualną miniaturę z QPixmapCache, obróć i zapisz z powrotem.
            QString cache_key = path + "@" + QString::number(m_thumb_size);
            QPixmap pix;
            if (QPixmapCache::find(cache_key, &pix) && !pix.isNull()) {
                QTransform t; t.rotate(degrees);
                QPixmap rotated = pix.transformed(t, Qt::SmoothTransformation);
                // Zaktualizuj RAM cache i canvas od razu
                QPixmapCache::insert(cache_key, rotated);
                if (m_canvas) m_canvas->set_pixmap(path, rotated);
            } else {
                // Miniatury nie ma w RAM — usuń SQLite cache żeby ThumbWorker
                // wygenerował nową (uwzględni rotation przy następnym odczycie)
                if (m_worker && m_worker->cache()) m_worker->cache()->remove(path);
            }
        }
    }

    // Przeładuj miniatury które nie były w RAM cache
    for (const QString& path : paths)
        if (m_worker) m_worker->request(path, 10);
}

void ThumbnailGrid::on_rename_completed(const QString& old_path, const QString& new_path)
{
    // Wykonuje się w głównym wątku — bezpieczne do emisji sygnałów UI
    emit preview_rename(old_path, new_path);
    emit primary_changed(new_path);
}

void ThumbnailGrid::on_rename_requested(const QString& old_path,
                                         const QString& new_name)
{
    QFileInfo fi(old_path);
    QString new_path = fi.dir().filePath(new_name);
    if (new_path == old_path) return;

    // ── 1. Aktualizuj UI natychmiast (optimistic update) ─────────────────────
    // Nie czekaj na I/O — dysk zewnętrzny może blokować Wayland timeout

    for (auto& sf : m_all_files)
        if (sf.path == old_path) { sf.path = new_path; sf.name = new_name; break; }
    for (auto& sf : m_visible)
        if (sf.path == old_path) { sf.path = new_path; sf.name = new_name; break; }

    if (m_selected.remove(old_path)) m_selected.insert(new_path);
    if (m_primary == old_path) m_primary = new_path;
    invalidate_meta_cache(old_path);

    if (m_items.contains(old_path)) {
        auto* item = m_items.take(old_path);
        ScannedFile new_sf = item->file();
        new_sf.path = new_path; new_sf.name = new_name;
        item->set_file(new_sf);
        m_items[new_path] = item;
    }

    if (m_canvas && m_canvas->rename_edit())
        disconnect(m_canvas->rename_edit(), nullptr, m_canvas, nullptr);
    if (m_canvas) m_canvas->cancel_rename();
    if (m_canvas) m_canvas->rename_item(old_path, new_path, new_name);
    // Przenoś cache miniatur TERAZ (UI thread) — zanim canvas poprosi o nową miniaturę
    // SQLite rename_entry jest szybkie, nie blokuje UI
    if (m_worker) m_worker->rename_cache(old_path, new_path);
    if (m_canvas) m_canvas->rename_pixmap_in_store(old_path, new_path);
    // Przebuduj z sortowaniem — nowa nazwa może zmienić pozycję w liście
    apply_filter_and_rebuild();

    m_selected.clear();
    m_selected.insert(new_path);
    m_primary = new_path;
    if (m_canvas) m_canvas->set_selected(m_selected);
    // NIE emituj primary_changed tutaj — plik jeszcze nie istnieje pod new_path!
    // primary_changed zostanie wysłane z wątku tła po ukończeniu rename na dysku.
    emit selection_changed(selected_paths());
    emit rename_done();
    // Blokuj nawigację klawiaturową przez 300ms — buforowane eventy (strzałki)
    // nie powinny przesuwać selekcji po rename
    m_rename_nav_block = true;
    QTimer::singleShot(300, this, [this]() {
        m_rename_nav_block = false;
        setFocus();
    });

    // ── 2. I/O w wątku tła — nie blokuj UI ───────────────────────────────────
    bool is_dir = fi.isDir();
    QString old_filename = QFileInfo(old_path).fileName();
    QString new_filename = new_name;
    QString cat_path     = MetaStore::catalog_path(old_path);

    if (m_fs_watcher) m_fs_watcher->blockSignals(true);

    auto rename_future = QtConcurrent::run([old_path, new_path, new_name, is_dir,
                       old_filename, new_filename, cat_path,
                       this]() {
        // Rename na dysku (może być wolny na USB/NAS)
        bool ok = false;
        if (is_dir) {
            QFileInfo fi2(old_path);
            ok = fi2.dir().rename(fi2.fileName(), new_name);
        } else {
            ok = QFile::rename(old_path, new_path);
        }

        if (!ok) {
            qDebug() << "[Grid] RENAME FAILED:" << old_path << "->" << new_path
                     << "writable=" << QFileInfo(QFileInfo(old_path).dir().path()).isWritable();
            // Rollback UI — plik się nie zmienił
            // Rollback — cofnij UI z powrotem do starej ścieżki
            emit rename_completed(new_path, old_path);  // odwrotna kolejność = rollback
            return;
        }

        // Emituj sygnał — Qt AutoConnection kolejkuje do głównego wątku grida
        emit rename_completed(old_path, new_path);

        // Przenieś metadane w tle
        QFile cat_f(cat_path);
        if (cat_f.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(cat_f.readAll());
            cat_f.close();
            if (doc.isObject()) {
                QJsonObject catalog = doc.object();
                if (catalog.contains(old_filename)) {
                    catalog[new_filename] = catalog.take(old_filename);
                    QFile out(cat_path);
                    if (out.open(QIODevice::WriteOnly))
                        out.write(QJsonDocument(catalog).toJson());
                }
            }
        }
        // Fallback: stary .leye
        QString old_leye = old_path + ".leye";
        if (QFileInfo::exists(old_leye))
            QFile::rename(old_leye, new_path + ".leye");

        // Odblokuj watcher i wyślij folder_renamed jeśli potrzeba
        // rename_completed już obsłuży primary_changed i preview_rename
        if (is_dir) emit folder_renamed(old_path, new_path);
        // Watcher zostanie odblokowany w on_rename_completed
        emit watcher_unblock();
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// MYSZ I RUBBER BAND
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        QPoint in_cont = m_container->mapFrom(this, e->pos());
        QWidget* child = m_container->childAt(in_cont);
        if (!child) {
            m_selected.clear();
            for (auto* item : m_items) item->set_selected(false);
            emit selection_changed({});
            emit context_menu_background(e->globalPosition().toPoint());
            return;
        }
    }

    if (e->button() == Qt::LeftButton) {
        m_drag_start_pos = e->pos();
        m_drag_active    = false;
        m_rubber_active  = false;
        m_rubber_origin  = e->pos();
        QPoint vp_in_grid = m_scroll->viewport()->mapTo(this, QPoint(0,0));
        int sy = m_scroll->verticalScrollBar()->value();
        int sx = m_scroll->horizontalScrollBar()->value();
        m_rubber_origin_c = e->pos() - vp_in_grid + QPoint(sx, sy);
        m_rubber_rect = QRect();
        m_scroll->viewport()->update();

        QPoint in_cont = m_container->mapFrom(this, e->pos());
        QWidget* child = m_container->childAt(in_cont);
        if (!child && !m_selected.isEmpty()) {
            m_selected.clear();
            for (auto* item : m_items) item->set_selected(false);
            emit selection_changed({});
        }
    }
    QWidget::mousePressEvent(e);
}

void ThumbnailGrid::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    if (m_drag_active) return;

    QPoint cur = e->pos();
    if ((cur - m_drag_start_pos).manhattanLength() < QApplication::startDragDistance()) return;

    QPoint in_container_start = m_container->mapFrom(this, m_drag_start_pos);
    if (!m_rubber_active && m_container->childAt(in_container_start)) return;

    m_rubber_active = true;
    m_rubber_cur = cur;

    QRect vp_rect = m_scroll->viewport()->rect();
    QPoint vp_cur = m_scroll->viewport()->mapFromGlobal(mapToGlobal(cur));
    const int ZONE = 40;
    int scroll_dy = 0;
    if (vp_cur.y() < ZONE) scroll_dy = -(ZONE - vp_cur.y()) / 2;
    else if (vp_cur.y() > vp_rect.height() - ZONE)
        scroll_dy = (vp_cur.y() - vp_rect.height() + ZONE) / 2;

    QPoint vp_tl  = m_scroll->viewport()->mapTo(this, QPoint(0, 0));
    int vp_top    = vp_tl.y();
    int vp_bottom = vp_tl.y() + vp_rect.height();
    QPoint display_cur = cur;
    if (scroll_dy != 0) {
        if (scroll_dy > 0) display_cur.setY(vp_bottom);
        else               display_cur.setY(vp_top);
        auto* sb = m_scroll->verticalScrollBar();
        sb->setValue(qBound(sb->minimum(), sb->value() + scroll_dy, sb->maximum()));
    }

    int scroll_val   = m_scroll->verticalScrollBar()->value();
    int origin_sy    = m_rubber_origin_c.y() - scroll_val + vp_top;
    bool orig_above  = origin_sy < vp_top;
    bool orig_below  = origin_sy > vp_bottom;

    int top, bottom, left, right;
    left  = qMin(m_rubber_origin.x(), display_cur.x());
    right = qMax(m_rubber_origin.x(), display_cur.x());
    if (orig_above) {
        top    = vp_top;
        bottom = qMax(display_cur.y(), vp_top);
    } else if (orig_below) {
        top    = qMin(display_cur.y(), vp_bottom);
        bottom = vp_bottom;
    } else {
        top    = qMin(origin_sy, display_cur.y());
        bottom = qMax(origin_sy, display_cur.y());
    }

    QRect sel_rect(QPoint(left, top), QPoint(right, bottom));
    // Overlay jest dzieckiem viewport — przemapuj koordynaty z ThumbnailGrid do viewport
    QPoint vp_origin = m_scroll->viewport()->mapFrom(this, QPoint(0, 0));
    QRect sel_vp = sel_rect.translated(vp_origin);
    m_overlay->setGeometry(m_scroll->viewport()->rect());
    m_overlay->show_rect(sel_vp);
    m_overlay->raise();

    update_rubber_selection(QRect());
    m_rubber_timer->start(16);
}

void ThumbnailGrid::on_item_drag_move(const QPoint& press_global, const QPoint& cur_global) {
    if (m_drag_active || m_rubber_active) return;
    if ((cur_global - press_global).manhattanLength() < QApplication::startDragDistance()) return;
    m_drag_active = true;

    // Canvas rysuje bez niebieskiego highlight gdy drag_active=true
    // Nie ma child widgetów z buforami Wayland — brak problemu
    if (m_canvas) m_canvas->set_drag_active(true);

    start_drag_selected();
}

void ThumbnailGrid::on_item_drag_released(const QPoint& /*global_pos*/) {
    // Zawsze ukryj overlay i wyczyść rubber band
    if (m_overlay) m_overlay->hide_rect();
    m_rubber_active = false;
    m_rubber_rect   = QRect();

    bool plain_click = !(m_click_mods & Qt::ControlModifier)
                    && !(m_click_mods & Qt::ShiftModifier);
    if (!m_drag_active && plain_click && !m_click_path.isEmpty()
        && m_selected.contains(m_click_path) && m_selected.size() > 1) {
        m_selected.clear();
        m_selected.insert(m_click_path);
        m_primary = m_click_path;
        if (m_canvas) m_canvas->set_selected(m_selected);
        emit primary_changed(m_click_path); // L__LINE__
        emit selection_changed(selected_paths());
    }
    m_drag_active = false;
    m_click_path.clear();
    m_click_mods = Qt::NoModifier;
    setFocus();
}

void ThumbnailGrid::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_rubber_active) {
        m_rubber_timer->stop();
        m_rubber_active = false;
        m_rubber_rect = QRect();
        m_overlay->hide_rect();
        update_rubber_selection(QRect());
        if (!m_selected.isEmpty()) {
            for (const auto& f : m_visible) {
                if (m_selected.contains(f.path)) { m_primary = f.path; break; }
            }
            emit primary_changed(m_primary); // L__LINE__
        }
        emit selection_changed(selected_paths());
    }
    m_drag_active = false;
    QWidget::mouseReleaseEvent(e);
}

// ═══════════════════════════════════════════════════════════════════════════════
// DRAG & DROP
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::dragEnterEvent(QDragEnterEvent* e) {
    if (!e->mimeData()->hasUrls()) return;
    e->acceptProposedAction();
}

void ThumbnailGrid::dragLeaveEvent(QDragLeaveEvent*) {
    // Wymuś repaint po opuszczeniu przez drag — KDE może zostawić drop highlight
    m_scroll->viewport()->repaint();
    repaint();
}

void ThumbnailGrid::dragMoveEvent(QDragMoveEvent* e) {
    if (!e->mimeData()->hasUrls()) { e->ignore(); return; }

    // Sprawdź czy drag jest nad folderem w canvas
    QString dir_under = dir_at(e->position().toPoint());

    if (e->source() == this) {
        // Wewnętrzny drag — akceptuj tylko gdy jest nad folderem który nie jest zaznaczony
        if (!dir_under.isEmpty() && !m_selected.contains(dir_under))
            e->acceptProposedAction();
        else
            e->ignore();
    } else {
        e->acceptProposedAction();
    }
}

void ThumbnailGrid::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls() || m_current_dir.isEmpty()) return;

    QString dest_dir = dir_at(e->position().toPoint());
    if (e->source() == this) {
        if (dest_dir.isEmpty()) { e->ignore(); return; }
    }
    if (dest_dir.isEmpty()) dest_dir = m_current_dir;

    QStringList moved_src, moved_dst;
    for (const QUrl& url : e->mimeData()->urls()) {
        QString src = url.toLocalFile();
        if (src.isEmpty() || !QFileInfo::exists(src)) continue;
        QString dst = dest_dir + "/" + QFileInfo(src).fileName();
        if (src == dst) continue;
        if (QFileInfo::exists(dst)) {
            dst = dest_dir + "/" + QFileInfo(src).completeBaseName()
                  + "_copy." + QFileInfo(src).suffix();
        }
        if (QFile::rename(src, dst)) { moved_src << src; moved_dst << dst; }
    }

    if (!moved_src.isEmpty()) {
        e->acceptProposedAction();
        if (e->source() == this) {
            remove_items_in_place(moved_src);
        } else {
            if (dest_dir == m_current_dir) {
                for (const QString& dst : moved_dst) add_item_in_place(dst);
                QCollator col; col.setNumericMode(true);
                col.setCaseSensitivity(Qt::CaseInsensitive);
                auto cmp = [&col](const ScannedFile& a, const ScannedFile& b) {
                    if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
                    return col.compare(a.name, b.name) < 0;
                };
                std::sort(m_all_files.begin(), m_all_files.end(), cmp);
                std::sort(m_visible.begin(),   m_visible.end(),   cmp);
                virt_full_rebuild();
            }
            // Powiadom źródłowe okno (i wszystkie inne) że pliki zostały przeniesione
            const auto topWidgets = QApplication::topLevelWidgets();
            for (QWidget* top : topWidgets) {
                const auto grids = top->findChildren<ThumbnailGrid*>();
                for (ThumbnailGrid* g : grids) {
                    if (g == this) continue;
                    QStringList foreign;
                    for (const QString& p : moved_src)
                        for (const auto& f : g->m_all_files)
                            if (f.path == p) { foreign << p; break; }
                    if (!foreign.isEmpty())
                        g->remove_items_in_place(foreign);
                }
            }
        }
    }
    // Wymuś repaint po dropie — KDE może zostawić drop highlight
    m_scroll->viewport()->repaint();
    repaint();
}

// ═══════════════════════════════════════════════════════════════════════════════
// USUWANIE
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::delete_selected() {
    QStringList paths = selected_paths();
    if (paths.isEmpty()) return;

    // Przenieś do kosza systemowego (Linux: ~/.local/share/Trash, Windows: Kosz)
    // QFile::moveToTrash() — bezpieczne, można odzyskać
    QStringList failed;
    for (const auto& path : paths) {
        bool ok = false;
        if (QFileInfo(path).isDir()) {
            // Foldery: moveToTrash działa też dla katalogów od Qt 5.15
            ok = QFile::moveToTrash(path);
            if (!ok) ok = !QDir(path).removeRecursively();
        } else {
            ok = QFile::moveToTrash(path);
        }
        if (!ok) failed << QFileInfo(path).fileName();

        // Plik .leye usuwamy na stałe (metadane nieistotne bez pliku)
        QString leye = path + ".leye";
        if (QFileInfo::exists(leye)) QFile::remove(leye);
    }

    if (!failed.isEmpty()) {
        QMessageBox::warning(this, "Usuwanie",
            "Nie udało się przenieść do kosza:\n" + failed.join("\n"));
    }
    remove_items_in_place(paths);
}

// ═══════════════════════════════════════════════════════════════════════════════
// EVENT FILTER I KLAWIATURA
// ═══════════════════════════════════════════════════════════════════════════════

bool ThumbnailGrid::eventFilter(QObject* obj, QEvent* event) {
    // Przechwytuj drag eventy z canvas i viewport
    if (obj == m_canvas || obj == m_scroll->viewport() || obj == m_scroll) {
        if (event->type() == QEvent::DragEnter) {
            dragEnterEvent(static_cast<QDragEnterEvent*>(event));
            return true;
        }
        if (event->type() == QEvent::DragMove) {
            auto* de = static_cast<QDragMoveEvent*>(event);
            QPoint pos = (obj == m_canvas) ?
                m_canvas->mapTo(this, de->position().toPoint()) :
                m_scroll->viewport()->mapTo(this, de->position().toPoint());
            QDragMoveEvent local(pos, de->possibleActions(), de->mimeData(),
                                 de->buttons(), de->modifiers());
            dragMoveEvent(&local);
            if (local.isAccepted()) de->acceptProposedAction(); else de->ignore();
            return true;
        }
        if (event->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(event);
            QPoint pos = (obj == m_canvas) ?
                m_canvas->mapTo(this, de->position().toPoint()) :
                m_scroll->viewport()->mapTo(this, de->position().toPoint());
            QDropEvent local(pos, de->possibleActions(), de->mimeData(),
                             de->buttons(), de->modifiers());
            dropEvent(&local);
            if (local.isAccepted()) de->acceptProposedAction(); else de->ignore();
            return true;
        }
        if (event->type() == QEvent::DragLeave) {
            dragLeaveEvent(static_cast<QDragLeaveEvent*>(event));
            return true;
        }
    }

    // Kliknięcie LPM w viewport poza canvas — odznacz wszystko
    if (obj == m_scroll->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            QPoint in_canvas = m_canvas ? m_canvas->mapFrom(m_scroll->viewport(), me->pos()) : QPoint(-1,-1);
            bool in_canvas_bounds = m_canvas && m_canvas->rect().contains(in_canvas);
            if (!in_canvas_bounds || (m_canvas && m_canvas->index_at(in_canvas) < 0)) {
                on_item_clicked("", Qt::NoModifier);
            }
        }
    }

    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(obj, event);
    if (obj != m_scroll && obj != m_scroll->viewport() && obj != m_canvas) return QWidget::eventFilter(obj, event);

    auto* ke = static_cast<QKeyEvent*>(event);

    // Rename aktywny — nie przechwytuj
    for (auto* item : m_items)
        if (item->is_renaming()) return false;

    // Fokus na QLineEdit — nie przechwytuj
    if (qobject_cast<QLineEdit*>(QApplication::focusWidget())) return false;

    switch (ke->key()) {
        case Qt::Key_Up: case Qt::Key_Down:
        case Qt::Key_Left: case Qt::Key_Right:
        case Qt::Key_Home: case Qt::Key_End:
        case Qt::Key_Delete: case Qt::Key_F2:
            // Enter/Return CELOWO pominięte — obsługuje MainWindow::eventFilter
            setFocus();
            keyPressEvent(ke);
            return true;
        default:
            return QWidget::eventFilter(obj, event);
    }
}

void ThumbnailGrid::navigate_to_index(int idx) {
    const int n = m_visible.size();
    if (n == 0) return;
    idx = qBound(0, idx, n - 1);
    const QString& path = m_visible[idx].path;

    m_selected.clear();
    m_selected.insert(path);
    m_primary = path;
    if (m_canvas) m_canvas->set_selected(m_selected);

    // Przewijanie: zależnie od rozmiaru kafelka
    // - Mały zoom (wiele wierszy w viewport): minimalne przewinięcie o 1 wiersz
    // - Duży zoom (1-2 wiersze w viewport): centruj gdy poza widokiem
    if (m_canvas) {
        QRect r    = m_canvas->item_rect(idx);
        int sv     = m_scroll->verticalScrollBar()->value();
        int vp_h   = m_scroll->viewport()->height();
        int cell_h = virt_cell_size();

        // Ile wierszy mieści się w viewport
        int rows_in_view = (cell_h > 0) ? (vp_h / cell_h) : 10;

        if (r.top() >= sv && r.bottom() <= sv + vp_h) {
            // W pełni widoczny — nic nie rób
        } else if (rows_in_view <= 2) {
            // Duży zoom — centruj element
            int new_sv = r.top() - (vp_h - r.height()) / 2;
            m_scroll->verticalScrollBar()->setValue(qMax(0, new_sv));
        } else if (r.top() < sv) {
            // Wychodzi poza górę — przewiń żeby górna krawędź była widoczna
            m_scroll->verticalScrollBar()->setValue(r.top());
        } else {
            // Wychodzi poza dół — przewiń żeby dolna krawędź była widoczna
            m_scroll->verticalScrollBar()->setValue(r.bottom() - vp_h);
        }
    }

    // Prefetch: załaduj bieżące + sąsiednie zdjęcia z wysokim priorytetem
    // żeby nawigacja strzałkami była płynna
    static constexpr int PREFETCH = 3;
    for (int i = qMax(0, idx - PREFETCH); i <= qMin(n - 1, idx + PREFETCH); ++i) {
        if (!m_visible[i].is_dir)
            m_worker->request(m_visible[i].path, 20);  // priorytet 20 = najwyższy
    }

    emit primary_changed(path); // L__LINE__
    emit selection_changed(selected_paths());
}

void ThumbnailGrid::keyPressEvent(QKeyEvent* e) {
    const int n = m_visible.size();
    if (n == 0) { QWidget::keyPressEvent(e); return; }

    // Blokuj nawigację strzałkami tuż po rename — buforowane eventy nie powinny
    // przesuwać selekcji gdy user zatwierdził rename Enterem
    if (m_rename_nav_block) {
        // Pochłoń event — nie nawiguj
        e->accept();
        return;
    }

    int cur = 0;
    for (int i = 0; i < n; ++i)
        if (m_visible[i].path == m_primary) { cur = i; break; }

    int cols = m_canvas ? m_canvas->cols() : virt_cols();

    switch (e->key()) {
        case Qt::Key_Right:  navigate_to_index(cur + 1);    break;
        case Qt::Key_Left:   navigate_to_index(cur - 1);    break;
        case Qt::Key_Down:   navigate_to_index(cur + cols); break;
        case Qt::Key_Up:     navigate_to_index(cur - cols); break;
        case Qt::Key_Home:   navigate_to_index(0);          break;
        case Qt::Key_End:    navigate_to_index(n - 1);      break;
        // Enter/Return — obsługuje MainWindow::eventFilter (globalny qApp filter)
        // Wcześniej był podwójny handler: jeden tutaj + jeden w MainWindow eventFilter,
        // co powodowało 2-3 wywołania on_item_double_clicked dla jednego naciśnięcia.
        case Qt::Key_F2:    start_rename_selected(); break;
        case Qt::Key_Delete: delete_selected();      break;
        case Qt::Key_C:
            if (e->modifiers() & Qt::ControlModifier) copy_selected(false);
            break;
        case Qt::Key_X:
            if (e->modifiers() & Qt::ControlModifier) copy_selected(true);
            break;
        case Qt::Key_V:
            if (e->modifiers() & Qt::ControlModifier) paste_here();
            break;
        case Qt::Key_Space: {
            if (!m_primary.isEmpty()) {
                QStringList files;
                for (const auto& f : m_visible)
                    if (!f.is_dir) files << f.path;
                int idx = files.indexOf(m_primary);
                if (idx >= 0) emit fullscreen_requested(files, idx);
            }
            break;
        }
        default: QWidget::keyPressEvent(e); return;
    }
    e->accept();
}

// ═══════════════════════════════════════════════════════════════════════════════
// RUBBER BAND SELECTION
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::update_rubber_band_visual() {}

void ThumbnailGrid::update_rubber_selection(const QRect& /*unused*/) {
    if (!m_canvas) return;

    int sy = m_scroll->verticalScrollBar()->value();
    int sx = m_scroll->horizontalScrollBar()->value();

    // m_rubber_origin_c jest w koordynatach kontenera (canvas)
    // m_rubber_cur jest w koordynatach viewport — przelicz do canvas
    QPoint vp_in_grid = m_scroll->viewport()->mapTo(this, QPoint(0, 0));
    QPoint cur_canvas = m_rubber_cur - vp_in_grid + QPoint(sx, sy);

    // sel_rect w koordynatach canvas
    QRect sel_rect = QRect(m_rubber_origin_c, cur_canvas).normalized();

    bool changed = false;
    for (int idx = 0; idx < m_visible.size(); ++idx) {
        QRect r = m_canvas->item_rect(idx);
        bool intersects = r.intersects(sel_rect);
        bool was = m_selected.contains(m_visible[idx].path);
        if (intersects == was) continue;
        changed = true;
        if (intersects) m_selected.insert(m_visible[idx].path);
        else            m_selected.remove(m_visible[idx].path);
    }
    if (changed) {
        m_canvas->set_selected(m_selected);
        emit selection_changed(selected_paths());
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// POZOSTAŁE OPERACJE
// ═══════════════════════════════════════════════════════════════════════════════

void ThumbnailGrid::refresh_metadata(const QString& path) {
    invalidate_meta_cache(path);
    auto meta = MetaStore::load(path);
    m_meta_cache[path] = meta;
    if (m_canvas) m_canvas->set_metadata(path, meta);
}

void ThumbnailGrid::start_rename_selected() {
    if (m_primary.isEmpty() || !m_canvas) return;
    for (int i = 0; i < m_visible.size(); ++i) {
        if (m_visible[i].path == m_primary) {
            m_canvas->start_rename(i);
            return;
        }
    }
}

void ThumbnailGrid::select_and_rename(const QString& path) {
    m_selected.clear();
    m_selected.insert(path);
    m_primary = path;
    if (m_canvas) {
        m_canvas->set_selected(m_selected);
        for (int i = 0; i < m_visible.size(); ++i) {
            if (m_visible[i].path == path) {
                m_canvas->start_rename(i);
                break;
            }
        }
    }
    emit primary_changed(path); // L__LINE__
    emit selection_changed(selected_paths());
}

QStringList ThumbnailGrid::selected_paths() const {
    return QStringList(m_selected.begin(), m_selected.end());
}

QStringList ThumbnailGrid::selected_paths_ordered() const {
    QStringList result;
    for (const auto& f : m_visible)
        if (m_selected.contains(f.path)) result << f.path;
    return result;
}

void ThumbnailGrid::rename_item(const QString& old_path, const QString& new_name) {
    QFileInfo fi(old_path);
    QString new_path = fi.dir().filePath(new_name);
    if (new_path == old_path) return;
    if (m_fs_watcher) m_fs_watcher->blockSignals(true);
    bool ok = fi.isDir()
        ? fi.dir().rename(fi.fileName(), new_name)
        : QFile::rename(old_path, new_path);
    if (m_fs_watcher) m_fs_watcher->blockSignals(false);
    if (!ok) return;
    if (fi.isDir()) emit folder_renamed(old_path, new_path);

    auto update_sf = [&](QList<ScannedFile>& list) {
        for (auto& sf : list)
            if (sf.path == old_path) { sf.path = new_path; sf.name = new_name; break; }
    };
    update_sf(m_all_files);
    update_sf(m_visible);

    // Stara struktura m_items (child widgety)
    if (m_items.contains(old_path)) {
        auto* item = m_items.take(old_path);
        ScannedFile sf = item->file();
        sf.path = new_path; sf.name = new_name;
        item->set_file(sf);
        m_items[new_path] = item;
    }

    // Aktualizuj selekcję i primary
    if (m_selected.remove(old_path)) m_selected.insert(new_path);
    if (m_primary == old_path) m_primary = new_path;
    invalidate_meta_cache(old_path);

    // Przesuń pixmapę w store na nową ścieżkę
    if (m_canvas) {
        m_canvas->rename_pixmap_in_store(old_path, new_path);
        m_canvas->rename_item(old_path, new_path, new_name);
    }

    // Przenieś metadane w katalogu zbiorczym
    {
        QString old_filename = QFileInfo(old_path).fileName();
        QString new_filename = QFileInfo(new_path).fileName();
        QString cat = MetaStore::catalog_path(old_path);
        QFile cat_f(cat);
        if (cat_f.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(cat_f.readAll());
            cat_f.close();
            if (doc.isObject()) {
                QJsonObject catalog = doc.object();
                if (catalog.contains(old_filename)) {
                    catalog[new_filename] = catalog.take(old_filename);
                    QFile out(cat);
                    if (out.open(QIODevice::WriteOnly))
                        out.write(QJsonDocument(catalog).toJson());
                }
            }
        }
        // Fallback: stary .leye per plik
        QString old_leye = old_path + ".leye", new_leye = new_path + ".leye";
        if (QFileInfo::exists(old_leye)) QFile::rename(old_leye, new_leye);
    }

    QCollator col; col.setNumericMode(true); col.setCaseSensitivity(Qt::CaseInsensitive);
    auto cmp = [&col](const ScannedFile& a, const ScannedFile& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return col.compare(a.name, b.name) < 0;
    };
    std::sort(m_all_files.begin(), m_all_files.end(), cmp);
    std::sort(m_visible.begin(),   m_visible.end(),   cmp);

    sync_canvas();  // odśwież canvas z nową nazwą i zachowaną pixmapą
    if (m_canvas) m_canvas->set_selected(m_selected);

    // Emituj primary_changed żeby MetaPanel zaktualizował nazwę
    emit primary_changed(new_path); // L__LINE__
    emit selection_changed(selected_paths());
    emit rename_done();
}

void ThumbnailGrid::copy_selected(bool cut) {
    QStringList paths = selected_paths_ordered();
    if (paths.isEmpty()) return;
    m_clipboard_paths = paths;
    m_cut_mode = cut;

    QList<QUrl> urls;
    for (const QString& p : paths) urls << QUrl::fromLocalFile(p);
    auto* mime = new QMimeData();
    mime->setUrls(urls);
    if (cut) {
        QByteArray kde_cut = "cut\n";
        for (const QUrl& u : urls) kde_cut += u.toString().toUtf8() + "\n";
        mime->setData("application/x-kde-cutselection", "1");
        mime->setData("x-special/gnome-copied-files", kde_cut);
    } else {
        QByteArray gnome = "copy\n";
        for (const QUrl& u : urls) gnome += u.toString().toUtf8() + "\n";
        mime->setData("x-special/gnome-copied-files", gnome);
    }
    QApplication::clipboard()->setMimeData(mime);

    if (cut) {
        for (auto* item : m_items)
            item->set_cut_mode(m_clipboard_paths.contains(item->path()));
        for (auto* item : m_pool)
            item->set_cut_mode(false);
    } else {
        // Ctrl+C po Ctrl+X — wyczyść efekt cut ze wszystkich widgetów
        for (auto* item : m_items) item->set_cut_mode(false);
        for (auto* item : m_pool)  item->set_cut_mode(false);
    }
    statusBar_message(QString("%1 %2 plik(ów)").arg(cut ? "Wycięto" : "Skopiowano").arg(paths.size()));
}

void ThumbnailGrid::paste_here() {
    if (m_current_dir.isEmpty()) return;
    QStringList sources = m_clipboard_paths;
    bool is_cut = m_cut_mode;
    if (sources.isEmpty()) {
        const QMimeData* mime = QApplication::clipboard()->mimeData();
        if (!mime || !mime->hasUrls()) return;
        for (const QUrl& u : mime->urls()) {
            QString local = u.toLocalFile();
            if (!local.isEmpty()) sources << local;
        }
        QByteArray gnome = mime->data("x-special/gnome-copied-files");
        is_cut = gnome.startsWith("cut");
        if (!is_cut) is_cut = (mime->data("application/x-kde-cutselection") == "1");
    }
    if (sources.isEmpty()) return;

    QStringList added;
    for (const QString& src : sources) {
        if (!QFileInfo::exists(src)) continue;
        QString dst = m_current_dir + "/" + QFileInfo(src).fileName();
        if (QFileInfo::exists(dst) && src != dst) {
            QString base = QFileInfo(src).completeBaseName();
            QString ext  = QFileInfo(src).suffix();
            int n = 2;
            do { dst = m_current_dir + "/" + base + QString(" (%1)").arg(n++)
                       + (ext.isEmpty() ? "" : "." + ext); }
            while (QFileInfo::exists(dst));
        }
        if ((is_cut ? QFile::rename(src, dst) : QFile::copy(src, dst)))
            added << dst;
    }
    if (added.isEmpty()) return;

    if (is_cut) {
        // Usuń z WŁASNEGO widoku jeśli te pliki tu są
        QStringList to_remove;
        for (const QString& src : sources) {
            for (const auto& f : m_all_files)
                if (f.path == src) { to_remove << src; break; }
        }
        if (!to_remove.isEmpty()) remove_items_in_place(to_remove);
        m_clipboard_paths.clear();
        m_cut_mode = false;
        for (auto* item : m_items) item->set_cut_mode(false);
        for (auto* item : m_pool)  item->set_cut_mode(false);

        // Powiadom WSZYSTKIE inne siatki we wszystkich oknach —
        // nawet gdy my sami nie mamy tych plików (paste z innego okna).
        // sources zawiera pełne ścieżki plików które zostały przeniesione.
        const auto topWidgets = QApplication::topLevelWidgets();
        for (QWidget* top : topWidgets) {
            const auto grids = top->findChildren<ThumbnailGrid*>();
            for (ThumbnailGrid* g : grids) {
                if (g == this) continue;
                QStringList foreign;
                for (const QString& p : sources)
                    for (const auto& f : g->m_all_files)
                        if (f.path == p) { foreign << p; break; }
                if (!foreign.isEmpty())
                    g->remove_items_in_place(foreign);
            }
        }
    }
    for (const QString& dst : added) add_item_in_place(dst);
    QCollator col; col.setNumericMode(true); col.setCaseSensitivity(Qt::CaseInsensitive);
    auto cmp = [&col](const ScannedFile& a, const ScannedFile& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return col.compare(a.name, b.name) < 0;
    };
    std::sort(m_all_files.begin(), m_all_files.end(), cmp);
    std::sort(m_visible.begin(),   m_visible.end(),   cmp);
    virt_full_rebuild();
    statusBar_message(QString("Wklejono %1 plik(ów)").arg(added.size()));
}

void ThumbnailGrid::duplicate_selected() {
    QStringList paths = selected_paths_ordered();
    if (paths.isEmpty() || m_current_dir.isEmpty()) return;
    QStringList added;
    for (const QString& src : paths) {
        if (!QFileInfo::exists(src)) continue;
        QString base = QFileInfo(src).completeBaseName();
        QString ext  = QFileInfo(src).suffix();
        QString dst;
        int n = 2;
        do { dst = m_current_dir + "/" + base + QString(" (%1)").arg(n++)
                   + (ext.isEmpty() ? "" : "." + ext); }
        while (QFileInfo::exists(dst));
        if (QFile::copy(src, dst)) added << dst;
    }
    if (added.isEmpty()) return;
    for (const QString& dst : added) add_item_in_place(dst);
    QCollator col; col.setNumericMode(true); col.setCaseSensitivity(Qt::CaseInsensitive);
    auto cmp = [&col](const ScannedFile& a, const ScannedFile& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return col.compare(a.name, b.name) < 0;
    };
    std::sort(m_all_files.begin(), m_all_files.end(), cmp);
    std::sort(m_visible.begin(),   m_visible.end(),   cmp);
    virt_full_rebuild();
    statusBar_message(QString("Zduplikowano %1 plik(ów)").arg(added.size()));
}

void ThumbnailGrid::statusBar_message(const QString& msg) {
    emit status_message(msg);
}

void ThumbnailGrid::select_path(const QString& path) {
    // Sprawdź czy plik jest w m_visible
    int target_idx = -1;
    for (int i = 0; i < m_visible.size(); ++i)
        if (m_visible[i].path == path) { target_idx = i; break; }
    if (target_idx < 0) return;

    m_selected.clear();
    m_selected.insert(path);
    m_primary = path;
    if (m_canvas) m_canvas->set_selected(m_selected);
    emit primary_changed(path); // L__LINE__
    emit selection_changed(selected_paths());

    // Wycentruj kafelek w viewporcie (jak po fullscreen)
    if (m_canvas) {
        QRect r = m_canvas->item_rect(target_idx);
        int vp_h = m_scroll->viewport()->height();
        int center_sv = r.top() + r.height() / 2 - vp_h / 2;
        m_scroll->verticalScrollBar()->setValue(qMax(0, center_sv));
    } else {
        // Fallback — stara logika
        int cell = virt_cell_size();
        int cols = virt_cols();
        int row  = target_idx / cols;
        int item_top = VIRT_TOP + row * cell;
        int item_bot = item_top + cell;
        int sv   = m_scroll->verticalScrollBar()->value();
        int vp_h = m_scroll->viewport()->height();
        if (item_top < sv || item_bot > sv + vp_h)
            m_scroll->verticalScrollBar()->setValue(qMax(0, item_top - (vp_h - cell) / 2));
    }
}

void ThumbnailGrid::add_item_in_place(const QString& path) {
    for (const auto& f : m_visible)
        if (f.path == path) return;

    QFileInfo fi(path);
    ScannedFile sf;
    sf.path   = path;
    sf.name   = fi.fileName();
    sf.is_dir = fi.isDir();
    sf.is_raw = FileScanner::is_raw(path);
    sf.is_psd = FileScanner::is_psd(path);
    sf.size   = fi.size();
    sf.mtime  = fi.lastModified().toSecsSinceEpoch();
    m_all_files.prepend(sf);
    m_visible.prepend(sf);

    // Posortuj: foldery pierwsze, potem nazwy alfabetycznie
    QCollator col; col.setNumericMode(true); col.setCaseSensitivity(Qt::CaseInsensitive);
    auto cmp = [&col](const ScannedFile& a, const ScannedFile& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return col.compare(a.name, b.name) < 0;
    };
    std::stable_sort(m_all_files.begin(), m_all_files.end(), cmp);
    std::stable_sort(m_visible.begin(),   m_visible.end(),   cmp);

    // Przebuduj siatkę żeby nowy element był widoczny od razu
    virt_full_rebuild();
}

void ThumbnailGrid::remove_items_in_place(const QStringList& paths) {
    if (paths.isEmpty()) return;

    // Zawsze chowaj cover — canvas pokaże aktualny stan
    if (m_empty_cover) m_empty_cover->hide();

    int prev_idx = -1;
    for (int i = 0; i < m_visible.size(); ++i) {
        if (paths.contains(m_visible[i].path)) { prev_idx = qMax(0, i - 1); break; }
    }

    for (const QString& path : paths) {
        m_all_files.erase(std::remove_if(m_all_files.begin(), m_all_files.end(),
            [&path](const ScannedFile& f){ return f.path == path; }), m_all_files.end());
        m_visible.erase(std::remove_if(m_visible.begin(), m_visible.end(),
            [&path](const ScannedFile& f){ return f.path == path; }), m_visible.end());
        m_selected.remove(path);
        invalidate_meta_cache(path);
    }
    if (paths.contains(m_primary)) m_primary.clear();
    m_selected.clear();

    if (m_overlay) m_overlay->hide_rect();
    m_rubber_active = false;
    m_rubber_rect   = QRect();

    // Canvas odmalowuje się sam — po set_items() rysuje nowy stan bez żadnych ghost items
    // bo nie ma child widgetów z buforami Wayland
    if (m_visible.isEmpty()) {
        if (m_canvas) m_canvas->set_items({});
        m_scroll->viewport()->setStyleSheet("background: #1e1e1e;");
    } else {
        sync_canvas();
        m_scroll->viewport()->setStyleSheet("");
        m_scroll->setStyleSheet("QScrollArea { border: none; }");
        QTimer::singleShot(0, this, &ThumbnailGrid::request_visible_thumbs);
    }

    if (prev_idx >= 0) {
        int target = qMin(prev_idx, (int)m_visible.size() - 1);
        if (target >= 0) QTimer::singleShot(0, this, [this, target]() {
            navigate_to_index(target);
        });
    }

    emit selection_changed(selected_paths());
    QTimer::singleShot(0, this, [this]() { setFocus(); });
}

void ThumbnailGrid::apply_batch_rename(const QList<QPair<QString,QString>>& pairs) {
    blockSignals(true);
    for (const auto& p : pairs) rename_item(p.first, p.second);
    blockSignals(false);
    virt_full_rebuild();
    emit selection_changed(selected_paths());
    if (!m_primary.isEmpty()) emit primary_changed(m_primary); // L__LINE__
}

void ThumbnailGrid::reapply_filter() {
    apply_filter_and_rebuild();
}

void ThumbnailGrid::load_folder_reselect(const QString& dir, const QString& select_path) {
    m_current_dir.clear();
    load_folder(dir);
    QTimer::singleShot(300, this, [this, select_path]() {
        this->select_path(select_path);
    });
}

void ThumbnailGrid::start_drag_selected() {
    if (m_selected.isEmpty()) {
        m_drag_active = false;  // reset — drag nie może się odbyć bez selekcji
        if (m_canvas) m_canvas->set_drag_active(false);
        return;
    }

    // KRYTYCZNE: wyczyść stan rubber-band PRZED drag exec.
    // Jeśli rubber-band był aktywny (rysował overlay), drag->exec() blokuje event loop —
    // mouseRelease nigdy nie dotrze i overlay zostanie narysowany w buforze.
    m_rubber_active = false;
    m_rubber_rect   = QRect();
    if (m_rubber_timer) m_rubber_timer->stop();
    if (m_overlay) {
        m_overlay->hide_rect();
        m_overlay->hide();
        // Wymuś natychmiastowe odmalowanie viewport przed dragiem
        if (m_scroll) {
            m_scroll->viewport()->repaint();
        }
    }

    QList<QUrl> urls;
    for (const auto& path : m_selected) urls << QUrl::fromLocalFile(path);
    auto* mime = new QMimeData();
    mime->setUrls(urls);
    QPixmap icon(48, 48);
    icon.fill(Qt::transparent);
    {
        QPainter p(&icon);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#2D7DD2"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(icon.rect().adjusted(2,2,-2,-2));
        p.setPen(Qt::white);
        p.setFont(QFont("sans", 14, QFont::Bold));
        p.drawText(icon.rect(), Qt::AlignCenter, QString::number(m_selected.size()));
    }
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(icon);

    // Zapamiętaj zaznaczone ścieżki PRZED exec()
    QStringList dragged_paths = selected_paths();

    // Zatrzymaj rubber-band timer
    if (m_rubber_timer) m_rubber_timer->stop();
    m_rubber_active = false;
    m_rubber_rect   = QRect();
    if (m_overlay) { m_overlay->hide_rect(); m_overlay->hide(); }

    if (m_fs_watcher) m_fs_watcher->blockSignals(true);

    Qt::DropAction result = drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);

    if (m_fs_watcher) m_fs_watcher->blockSignals(false);

    // Wyczyść stan drag
    m_drag_active   = false;
    m_rubber_active = false;
    m_rubber_rect   = QRect();
    if (m_canvas) m_canvas->set_drag_active(false);
    if (m_overlay) { m_overlay->hide_rect(); m_overlay->hide(); }

    if (result == Qt::MoveAction) {
        m_selected.clear();
        m_primary.clear();
        remove_items_in_place(dragged_paths);
        // Chowaj cover — canvas już pokazuje nowy stan
        if (m_empty_cover) m_empty_cover->hide();
        m_container->setUpdatesEnabled(true);
        m_scroll->viewport()->update();
    } else {
        // Odblokuj kontener
        m_container->setUpdatesEnabled(true);

        QStringList actually_moved;
        for (const QString& p : dragged_paths)
            if (!QFileInfo::exists(p)) actually_moved << p;

        if (!actually_moved.isEmpty()) {
            m_selected.clear();
            m_primary.clear();
            remove_items_in_place(actually_moved);
            // Powiadom inne siatki
            const auto topWidgets = QApplication::topLevelWidgets();
            for (QWidget* top : topWidgets) {
                const auto grids = top->findChildren<ThumbnailGrid*>();
                for (ThumbnailGrid* g : grids) {
                    if (g == this) continue;
                    QStringList foreign;
                    for (const QString& p : actually_moved)
                        for (const auto& f : g->m_all_files)
                            if (f.path == p) { foreign << p; break; }
                    if (!foreign.isEmpty()) g->remove_items_in_place(foreign);
                }
            }
        } else {
            // Nic nie przeniesiono — odtwórz wizualną selekcję z m_selected
            for (auto it = m_items.begin(); it != m_items.end(); ++it) {
                if (m_selected.contains(it.key()))
                    it.value()->set_selected(true);
            }
            m_scroll->viewport()->update();
        }
    }
    setFocus();
}

QString ThumbnailGrid::primary_path() const { return m_primary; }

QStringList ThumbnailGrid::visible_file_paths() const {
    QStringList out;
    for (const auto& f : m_visible) if (!f.is_dir) out << f.path;
    return out;
}

void ThumbnailGrid::open_primary() {
    bool renaming = false;
    for (auto* item : m_items) if (item->is_renaming()) { renaming = true; break; }
    if (!renaming && !m_primary.isEmpty())
        on_item_double_clicked(m_primary);
}

void ThumbnailGrid::request_all_thumbs_background() {
    // Zbierz pliki bez miniatur (pomijaj widoczne — już obsługiwane)
    QStringList pending;
    for (const auto& f : m_all_files) {
        if (!f.is_dir && !m_items.contains(f.path))
            pending << f.path;
    }
    if (pending.isEmpty()) return;

    // Wyślij partie po 10 co 300ms — płynne ładowanie
    for (int i = 0; i < pending.size(); i += 10) {
        int delay = (i / 10) * 300;
        QStringList slice = pending.mid(i, 10);
        QTimer::singleShot(delay, this, [this, slice]() {
            if (m_current_dir.isEmpty() && !m_collection_mode) return;
            for (const QString& p : slice)
                m_worker->request(p);
        });
    }
}

void ThumbnailGrid::select_all() {
    for (const auto& f : m_visible) m_selected.insert(f.path);
    for (auto* item : m_items) item->set_selected(true);
    emit selection_changed(selected_paths());
}

void ThumbnailGrid::deselect_all() {
    m_selected.clear();
    for (auto* item : m_items) item->set_selected(false);
    emit selection_changed({});
}

} // namespace LapesEye
