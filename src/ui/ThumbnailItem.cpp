#include "LapesEye/ui/ThumbnailItem.h"
#include "LapesEye/ui/ColorLabelEditor.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QResizeEvent>
#include <QFontMetrics>
#include <QLineEdit>
#include <QTimer>
#include <QStyle>
#include <functional>
#include <QDebug>

namespace LapesEye {

bool ThumbnailItem::s_drag_active = false;
void ThumbnailItem::set_drag_active(bool active) { s_drag_active = active; }

// Subklasa QLineEdit — bezpośrednia obsługa Enter przez keyPressEvent
// (niezależna od eventFilter i returnPressed)
class RenameEdit : public QLineEdit {
public:
    explicit RenameEdit(QWidget* parent) : QLineEdit(parent) {}
    std::function<void(bool)> on_commit;

protected:
    void keyPressEvent(QKeyEvent* e) override {
        int k = e->key();
        // Key_Return = lewy Enter, Key_Enter = numpad, scanCode 36/104 jako fallback
        bool is_enter = (k == Qt::Key_Return) || (k == Qt::Key_Enter)
                     || (e->nativeScanCode() == 36)
                     || (e->nativeScanCode() == 104);
        if (is_enter) {
            if (on_commit) on_commit(true);
            return;
        }
        if (k == Qt::Key_Escape) {
            if (on_commit) on_commit(false);
            return;
        }
        QLineEdit::keyPressEvent(e);
    }
};

static constexpr int NAME_H  = 15;
static constexpr int STARS_H = 11;
static constexpr int PAD     = 4;

ThumbnailItem::ThumbnailItem(const ScannedFile& file, QWidget* parent)
    : QFrame(parent), m_file(file)
{
    setFixedSize(m_thumb_size + PAD * 2,
                 m_thumb_size + PAD * 2 + NAME_H + STARS_H + 4);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);  // ThumbnailGrid obsługuje klawiaturę, nie item
}

void ThumbnailItem::set_thumb_size(int size) {
    m_thumb_size = size;
    setFixedSize(size + PAD * 2, size + PAD * 2 + NAME_H + STARS_H + 4);
    if (m_rename_edit) update_rename_geometry();
    update();
}

void ThumbnailItem::set_pixmap(const QPixmap& pix) { m_thumb = pix; update(); }

void ThumbnailItem::set_metadata(const FileMetadata& meta) {
    m_meta = meta; m_meta_loaded = true; update();
}

void ThumbnailItem::set_selected(bool s) { m_selected = s; update(); }
void ThumbnailItem::set_cut_mode(bool cut) { m_cut = cut; update(); }

QColor ThumbnailItem::label_color(ColorLabel c) {
    if (c == ColorLabel::None) return Qt::transparent;
    int idx = static_cast<int>(c) - 1;  // None=0, Red=1, Yellow=2...
    auto labels = LabelConfig::load();
    if (idx >= 0 && idx < labels.size()) return labels[idx].color;
    return Qt::transparent;
}

// ─── Paint ───────────────────────────────────────────────────────────────────

void ThumbnailItem::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect total = rect();
    QRect img_rect(PAD, PAD, m_thumb_size, m_thumb_size);

    // ── Tło — kolor zależy od etykiety ───────────────────────────────────────
    QColor bg;
    if (m_selected && !s_drag_active) {
        bg = QColor(0x2D, 0x7D, 0xD2);
    } else if (m_meta_loaded && m_meta.color_label != ColorLabel::None) {
        // Przyciemniona wersja koloru etykiety jako tło
        QColor lc = label_color(m_meta.color_label);
        bg = QColor(lc.red()/4, lc.green()/4, lc.blue()/4);  // ~25% jasności
        if (m_hovered) bg = bg.lighter(140);
    } else {
        bg = m_hovered ? QColor(0x3A, 0x3A, 0x3A) : QColor(0x28, 0x28, 0x28);
    }
    QPainterPath bgPath;
    bgPath.addRoundedRect(total, 6, 6);
    p.fillPath(bgPath, bg);

    // ── Folder lub zdjęcie ───────────────────────────────────────────────────
    if (m_file.is_dir) {
        // Ikona folderu
        p.fillRect(img_rect, QColor(0x1A, 0x1A, 0x1A));
        QFont f = p.font();
        f.setPixelSize(qMin(m_thumb_size * 2 / 3, 96));
        p.setFont(f);
        p.setPen(QColor(0xF0, 0xC0, 0x20));
        p.drawText(img_rect, Qt::AlignCenter, "📁");
    } else if (m_thumb.isNull()) {
        p.fillRect(img_rect, QColor(0x1A, 0x1A, 0x1A));
        p.setPen(QColor(0x44, 0x44, 0x44));
        p.drawText(img_rect, Qt::AlignCenter, "⏳");
    } else {
        QPixmap scaled = m_thumb.scaled(img_rect.size(),
                                         Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
        int ox = img_rect.x() + (img_rect.width()  - scaled.width())  / 2;
        int oy = img_rect.y() + (img_rect.height() - scaled.height()) / 2;
        p.drawPixmap(ox, oy, scaled);
    }

    // ── Efekt wycinania — półprzezroczysta nakładka ───────────────────────────
    if (m_cut) {
        p.fillPath(bgPath, QColor(0, 0, 0, 120));
    }
    if (m_meta_loaded && m_meta.color_label != ColorLabel::None) {
        // Ramka dookoła miniatury — 2px, zaokrąglone rogi
        QColor lc = label_color(m_meta.color_label);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(lc, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(img_rect).adjusted(1, 1, -1, -1), 4, 4);
    }

    // ── Badge RAW / PSD ───────────────────────────────────────────────────────
    if (m_file.is_raw)
        draw_badge(p, img_rect, "RAW", QColor(0xE5, 0x89, 0x20));
    else if (m_file.is_psd)
        draw_badge(p, img_rect, "PSD", QColor(0x20, 0x6E, 0xE5));

    // ── Nazwa pliku (nie rysuj gdy aktywny rename edit) ──────────────────────
    if (!m_rename_edit) {
        QRect name_rect(PAD, img_rect.bottom() + 3, total.width() - 2*PAD, NAME_H);
        p.setPen(m_selected ? Qt::white : QColor(0xCC, 0xCC, 0xCC));
        QFont fn = p.font();
        fn.setPixelSize(11);
        p.setFont(fn);
        QFontMetrics fm(fn);
        p.drawText(name_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                   fm.elidedText(m_file.name, Qt::ElideMiddle, name_rect.width()));

        // ── Gwiazdki ─────────────────────────────────────────────────────────
        if (m_meta_loaded && m_meta.rating > 0 && !m_file.is_dir) {
            QRect star_rect(PAD, name_rect.bottom() + 1, total.width() - 2*PAD, STARS_H);
            draw_stars(p, star_rect, m_meta.rating);
        }
    }
}

void ThumbnailItem::draw_stars(QPainter& p, const QRect& rect, int rating) {
    const int sw = 11;
    int x = rect.x() + (rect.width() - 5 * sw) / 2;
    int y = rect.y() + (rect.height() - sw) / 2;
    QFont f = p.font(); f.setPixelSize(9); p.setFont(f);
    for (int i = 0; i < 5; ++i) {
        p.setPen(i < rating ? QColor(0xF0, 0xC0, 0x20) : QColor(0x44, 0x44, 0x44));
        p.drawText(QRect(x + i*sw, y, sw, sw), Qt::AlignCenter, "★");
    }
}

void ThumbnailItem::draw_badge(QPainter& p, const QRect& container,
                                const QString& text, const QColor& bg)
{
    QFont f = p.font(); f.setPixelSize(8); f.setBold(true); p.setFont(f);
    QFontMetrics fm(f);
    int tw = fm.horizontalAdvance(text) + 6;
    QRect badge(container.x() + 2, container.bottom() - 14, tw, 12);
    QPainterPath bp; bp.addRoundedRect(badge, 2, 2);
    p.fillPath(bp, bg);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, text);
}

// ─── Inline Rename ────────────────────────────────────────────────────────────

void ThumbnailItem::start_rename() {
    if (m_rename_edit) return;

    m_rename_edit = new RenameEdit(this);
    m_rename_edit->setAlignment(Qt::AlignCenter);
    m_rename_edit->setStyleSheet(
        "QLineEdit { background: #1a1a2e; color: #eee; "
        "border: 1px solid #2D7DD2; border-radius: 3px; "
        "font-size: 11px; padding: 1px 3px; }"
    );

    QFileInfo fi(m_file.name);
    QString base = m_file.is_dir ? m_file.name : fi.completeBaseName();
    m_rename_edit->setText(m_file.is_dir ? m_file.name : fi.fileName());
    m_rename_edit->show();

    int sel_len = base.length();
    QTimer::singleShot(0, m_rename_edit, [this, sel_len]() {
        if (m_rename_edit) {
            m_rename_edit->setFocus();
            m_rename_edit->setSelection(0, sel_len);
        }
    });

    update_rename_geometry();

    m_rename_committed = false;

    // RenameEdit::keyPressEvent wywołuje on_commit bezpośrednio
    static_cast<RenameEdit*>(m_rename_edit)->on_commit = [this](bool accept) {
        if (!m_rename_edit || m_rename_committed) return;
        m_rename_committed = true;
        finish_rename(accept);
        // UWAGA: po finish_rename m_rename_edit == nullptr
    };

    // editingFinished — utrata focusu (klik poza polem) → zatwierdź
    QObject::connect(m_rename_edit, &QLineEdit::editingFinished,
                     this, [this]() {
        if (!m_rename_edit || m_rename_committed) return;
        m_rename_committed = true;
        finish_rename(true);
    });

    update();
}

void ThumbnailItem::update_rename_geometry() {
    if (!m_rename_edit) return;
    QRect total = rect();
    QRect img_rect(PAD, PAD, m_thumb_size, m_thumb_size);
    m_rename_edit->setGeometry(
        PAD, img_rect.bottom() + 2, total.width() - 2*PAD, NAME_H + 2
    );
}

void ThumbnailItem::finish_rename(bool accept) {
    if (!m_rename_edit) return;
    QString new_name = m_rename_edit->text().trimmed();
    // Ustaw nullptr PRZED deleteLater — chroni przed re-entrancy
    QLineEdit* edit = m_rename_edit;
    m_rename_edit      = nullptr;
    m_rename_committed = false;
    edit->deleteLater();
    update();

    if (accept && !new_name.isEmpty() && new_name != m_file.name)
        emit rename_requested(m_file.path, new_name);
}

void ThumbnailItem::resizeEvent(QResizeEvent* e) {
    QFrame::resizeEvent(e);
    update_rename_geometry();
}

// ─── Zdarzenia myszy / klawiatury ────────────────────────────────────────────

void ThumbnailItem::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_F2) { start_rename(); return; }
    if (e->key() == Qt::Key_Escape && m_rename_edit) { finish_rename(false); return; }
    // Enter bez aktywnego rename — propaguj do ThumbnailGrid::keyPressEvent
    // Enter z aktywnym rename — obsługuje RenameEdit::keyPressEvent bezpośrednio
    // (ten kod jest fallback gdy RenameEdit nie dostał eventu)
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_rename_edit) {
        if (!m_rename_committed) {
            m_rename_committed = true;
            finish_rename(true);
        }
        return;
    }
    // Bez rename — propaguj normalnie (ThumbnailGrid::keyPressEvent obsłuży)
    QFrame::keyPressEvent(e);
}

bool ThumbnailItem::eventFilter(QObject* obj, QEvent* event) {
    // RenameEdit obsługuje Enter przez keyPressEvent — eventFilter nie jest już potrzebny
    // Zostawiamy pusty dla zgodności
    return QFrame::eventFilter(obj, event);
}

void ThumbnailItem::mousePressEvent(QMouseEvent* e) {
    // setFocus() na sobie nic nie robi (NoFocus policy) — wymuś fokus na
    // ThumbnailGrid (rodzic rodzica — m_container jest pomiędzy)
    QWidget* w = parentWidget();
    while (w && w->focusPolicy() == Qt::NoFocus) w = w->parentWidget();
    if (w) w->setFocus();
    if (e->button() == Qt::LeftButton) {
        m_press_global = e->globalPosition().toPoint();  // Problem 4: zapisz dla drag
        emit clicked(m_file.path, e->modifiers());
    }
}

void ThumbnailItem::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton)
        emit drag_move(m_press_global, e->globalPosition().toPoint());
    QFrame::mouseMoveEvent(e);
}

void ThumbnailItem::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        emit drag_released(e->globalPosition().toPoint());
    QFrame::mouseReleaseEvent(e);
}

void ThumbnailItem::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    // Klik na obszar nazwy → rename; klik na miniaturę → otwórz
    QRect img_rect(PAD, PAD, m_thumb_size, m_thumb_size);
    if (!img_rect.contains(e->pos()))
        start_rename();
    else
        emit double_clicked(m_file.path);
}

void ThumbnailItem::contextMenuEvent(QContextMenuEvent* e) {
    emit context_menu_requested(m_file.path, e->globalPos());
}

void ThumbnailItem::enterEvent(QEnterEvent*) { m_hovered = true;  update(); }
void ThumbnailItem::leaveEvent(QEvent*)       { m_hovered = false; update(); }

} // namespace LapesEye
