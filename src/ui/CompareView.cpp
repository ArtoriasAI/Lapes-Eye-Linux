#include "LapesEye/ui/CompareView.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
#include <QImageReader>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>

namespace LapesEye {

// ─── CompareCell ──────────────────────────────────────────────────────────────

CompareCell::CompareCell(QWidget* parent) : QWidget(parent) {
    setMinimumSize(80, 80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(2);

    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_label->setMinimumSize(40, 40);

    m_name = new QLabel(this);
    m_name->setAlignment(Qt::AlignCenter);
    m_name->setStyleSheet("color: #aaa; font-size: 10px; background: transparent;");
    m_name->setFixedHeight(16);

    lay->addWidget(m_label, 1);
    lay->addWidget(m_name);

    setStyleSheet("CompareCell { background: #1a1a1a; border: 1px solid #333; border-radius: 3px; }");
}

void CompareCell::load(const QString& path) {
    if (path == m_path && !m_full_pix.isNull()) return;
    m_path    = path;
    m_loading = true;
    m_full_pix = QPixmap();
    m_label->setText("...");
    m_name->setText(QFileInfo(path).fileName());

    // Ładuj w tle
    (void)QtConcurrent::run([this, path]() {
        QImageReader reader(path);
        reader.setAutoTransform(true);

        QSize orig = reader.size();
        if (!orig.isValid()) {
            QMetaObject::invokeMethod(this, [this]() {
                m_label->setText("Błąd");
                m_loading = false;
            }, Qt::QueuedConnection);
            return;
        }
        // Dekoduj do max 2400px — wyraźne przy każdym rozmiarze panelu
        int dec = qMin(qMax(orig.width(), orig.height()), 2400);
        reader.setScaledSize(orig.scaled(dec, dec, Qt::KeepAspectRatio));
        QImage img = reader.read();

        QMetaObject::invokeMethod(this, [this, img]() {
            if (!img.isNull()) {
                m_full_pix = QPixmap::fromImage(img);
                m_loading  = false;
                reload_scaled();
            } else {
                m_label->setText("Błąd");
                m_loading = false;
            }
        }, Qt::QueuedConnection);
    });
}

void CompareCell::clear() {
    m_path = {};
    m_full_pix = {};
    m_label->clear();
    m_label->setText("—");
    m_name->clear();
}

void CompareCell::reload_scaled() {
    if (m_full_pix.isNull() || !m_label) return;
    QSize avail = m_label->size();
    if (avail.width() < 4 || avail.height() < 4) return;
    m_label->setPixmap(
        m_full_pix.scaled(avail, Qt::KeepAspectRatio, Qt::SmoothTransformation)
    );
}

void CompareCell::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    // Debounce — nie skaluj przy każdym pikselu zmiany rozmiaru
    QTimer::singleShot(60, this, [this]() { reload_scaled(); });
}

void CompareCell::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && !m_path.isEmpty())
        emit clicked(m_path);
    QWidget::mousePressEvent(e);
}

void CompareCell::paintEvent(QPaintEvent* e) {
    QWidget::paintEvent(e);
    // Obramowanie aktywnej komórki (opcjonalnie można podświetlać)
}

// ─── CompareView ──────────────────────────────────────────────────────────────

CompareView::CompareView(QWidget* parent) : QWidget(parent) {
    m_grid = new QGridLayout(this);
    m_grid->setSpacing(4);
    m_grid->setContentsMargins(4, 4, 4, 4);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void CompareView::set_mode(CompareMode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    rebuild();
}

void CompareView::set_paths(const QStringList& paths) {
    m_paths = paths;
    rebuild();
}

void CompareView::set_primary(const QString& path) {
    if (m_mode == CompareMode::Single) {
        if (m_paths.isEmpty() || m_paths.first() != path) {
            m_paths = { path };
            rebuild();
        }
    }
}

void CompareView::rebuild() {
    // Usuń stare komórki z layoutu (nie kasuj — recycluj)
    while (m_grid->count()) m_grid->takeAt(0);

    int needed = (m_mode == CompareMode::Single) ? 1
               : (m_mode == CompareMode::Split2)  ? 2 : 4;

    // Dostrój liczbę komórek
    while (m_cells.size() < needed) {
        auto* cell = new CompareCell(this);
        QObject::connect(cell, &CompareCell::clicked,
                         this, &CompareView::cell_clicked);
        m_cells.append(cell);
    }

    // Ukryj nadmiarowe
    for (int i = 0; i < m_cells.size(); ++i)
        m_cells[i]->setVisible(i < needed);

    // Rozmieść w siatce
    if (m_mode == CompareMode::Single || m_mode == CompareMode::Split2) {
        // Single: 1×1 — Split2: 1×2 (obok siebie)
        for (int i = 0; i < needed; ++i) {
            m_grid->addWidget(m_cells[i], 0, i);
            m_grid->setColumnStretch(i, 1);
        }
        m_grid->setRowStretch(0, 1);
        m_grid->setRowStretch(1, 0);
    } else {
        // Split4: 2×2
        for (int i = 0; i < 4; ++i) {
            m_grid->addWidget(m_cells[i], i / 2, i % 2);
            m_grid->setColumnStretch(i % 2, 1);
            m_grid->setRowStretch(i / 2, 1);
        }
    }

    // Załaduj zdjęcia do komórek
    for (int i = 0; i < needed; ++i) {
        if (i < m_paths.size() && !m_paths[i].isEmpty())
            m_cells[i]->load(m_paths[i]);
        else
            m_cells[i]->clear();
    }
}

} // namespace LapesEye
