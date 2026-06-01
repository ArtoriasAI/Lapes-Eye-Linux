#include "LapesEye/ui/FilterBar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QComboBox>

namespace LapesEye {

FilterBar::FilterBar(QWidget* parent) : QWidget(parent) {
    setup_ui();
    setFixedHeight(40);
}

void FilterBar::setup_ui() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(8);

    auto make_btn = [&](const QString& text, const QString& tip) -> QToolButton* {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setStyleSheet(R"(
            QToolButton { padding: 3px 10px; border-radius: 3px;
                          border: 1px solid #444; color: #bbb; }
            QToolButton:checked { background: #2D7DD2; border-color: #2D7DD2; color: white; }
            QToolButton:hover:!checked { background: #333; }
        )");
        return b;
    };

    // ── Widoczność ─────────────────────────────────────────────────────────────
    layout->addWidget(new QLabel("Widoczność:", this));

    m_all_btn = make_btn("Wszystkie", "Pokaż wszystkie pliki");
    m_all_btn->setChecked(true);  // domyślnie
    layout->addWidget(m_all_btn);

    m_pick_only = make_btn("✓  Wybrane", "Pokaż tylko wybrane (z flagą Pick)");
    layout->addWidget(m_pick_only);

    m_reject_only = make_btn("✗  Odrzucone", "Pokaż tylko odrzucone");
    layout->addWidget(m_reject_only);

    layout->addSpacing(16);

    // ── Format ────────────────────────────────────────────────────────────────
    m_raw_only = make_btn("RAW", "Pokaż tylko pliki RAW");
    m_psd_only = make_btn("PSD", "Pokaż tylko pliki PSD");
    layout->addWidget(m_raw_only);
    layout->addWidget(m_psd_only);

    layout->addStretch();

    // ── Obrót ─────────────────────────────────────────────────────────────────
    // Przyciski zgodne ze stylem Bridge: małe, kwadratowe, ikony Unicode
    auto make_rot = [&](const QString& icon, const QString& tip) -> QToolButton* {
        auto* b = new QToolButton(this);
        b->setText(icon);
        b->setToolTip(tip);
        b->setFixedSize(28, 28);
        b->setAutoRaise(true);
        b->setStyleSheet(R"(
            QToolButton {
                font-size: 16px; color: #ccc;
                background: #2b2b2b; border: 1px solid #555; border-radius: 4px;
            }
            QToolButton:hover   { background: #383838; border-color: #888; }
            QToolButton:pressed { background: #1d5fa0; border-color: #2D7DD2; color: #fff; }
        )");
        return b;
    };
    // ↶ = ↶ zakrzywiona strzałka w lewo (CCW), ↷ = ↷ w prawo (CW)
    m_rotate_ccw = make_rot("↶", "Obróć w lewo (-90°)");
    m_rotate_cw  = make_rot("↷", "Obróć w prawo (+90°)");
    layout->addWidget(m_rotate_ccw);
    layout->addWidget(m_rotate_cw);
    layout->addSpacing(6);

    connect(m_rotate_ccw, &QToolButton::clicked, this, [this]{ emit rotate_requested(-90); });
    connect(m_rotate_cw,  &QToolButton::clicked, this, [this]{ emit rotate_requested(+90); });

    // ── Szukaj ────────────────────────────────────────────────────────────────
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Szukaj nazwy...");
    m_search->setFixedWidth(180);
    m_search->setClearButtonEnabled(true);
    m_search->setStyleSheet("padding: 3px 6px; border-radius: 3px;");
    layout->addWidget(m_search);

    layout->addSpacing(16);

    // ── Tryb widoku (po prawej) ───────────────────────────────────────────────
    auto make_view_btn = [&](const QString& tip) -> QToolButton* {
        auto* b = new QToolButton(this);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setFixedSize(28, 28);
        b->setStyleSheet(R"(
            QToolButton { border-radius: 3px; border: 1px solid #444; background: #222; }
            QToolButton:checked { background: #2D7DD2; border-color: #2D7DD2; }
            QToolButton:hover:!checked { background: #333; }
        )");
        return b;
    };

    m_single_btn = make_view_btn("Tryb pojedynczy (1 zdjęcie)");
    m_split2_btn = make_view_btn("Porównanie 2 zdjęć");
    m_split4_btn = make_view_btn("Porównanie 4 zdjęć");

    // Ikony rysowane jako SVG inline przez QPainter — brak zależności od plików
    m_single_btn->setIcon(QIcon(make_single_icon()));
    m_split2_btn->setIcon(QIcon(make_split2_icon()));
    m_split4_btn->setIcon(QIcon(make_split4_icon()));
    m_single_btn->setIconSize({18, 18});
    m_split2_btn->setIconSize({18, 18});
    m_split4_btn->setIconSize({18, 18});

    m_single_btn->setChecked(true);  // domyślnie tryb pojedynczy

    layout->addWidget(m_single_btn);
    layout->addWidget(m_split2_btn);
    layout->addWidget(m_split4_btn);

    // ── Połączenia ────────────────────────────────────────────────────────────

    // Przyciski widoczności są wzajemnie wykluczające
    auto exclusive_toggle = [this](QToolButton* clicked_btn) {
        // Odznacz pozostałe przyciski widoczności
        for (auto* btn : {m_all_btn, m_pick_only, m_reject_only}) {
            if (btn != clicked_btn) btn->setChecked(false);
        }
        // Jeśli odznaczono aktywny — wróć do "Wszystkie"
        if (!clicked_btn->isChecked()) {
            m_all_btn->setChecked(true);
        }
        emit_filter();
    };

    QObject::connect(m_all_btn,    &QToolButton::clicked, this, [this, exclusive_toggle]() { exclusive_toggle(m_all_btn); });
    QObject::connect(m_pick_only,  &QToolButton::clicked, this, [this, exclusive_toggle]() { exclusive_toggle(m_pick_only); });
    QObject::connect(m_reject_only,&QToolButton::clicked, this, [this, exclusive_toggle]() { exclusive_toggle(m_reject_only); });

    QObject::connect(m_raw_only, &QToolButton::toggled, this, [this](bool) { emit_filter(); });
    QObject::connect(m_psd_only, &QToolButton::toggled, this, [this](bool) { emit_filter(); });
    QObject::connect(m_search,   &QLineEdit::textChanged, this, [this](const QString&) { emit_filter(); });

    // Sortowanie
    m_sort_combo = new QComboBox(this);
    m_sort_combo->addItems({
        "Nazwa ↑", "Nazwa ↓",
        "Data ↑",  "Data ↓",
        "Rozmiar ↑","Rozmiar ↓",
        "Typ ↑"
    });
    m_sort_combo->setToolTip("Sortowanie");
    m_sort_combo->setMaximumWidth(110);
    layout->addWidget(m_sort_combo);
    QObject::connect(m_sort_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, [this](int) { emit_filter(); });

    // Przycisk zaawansowanego wyszukiwania
    auto* adv_btn = new QToolButton(this);
    adv_btn->setText("🔍");
    adv_btn->setToolTip("Wyszukiwanie zaawansowane (Ctrl+F)");
    adv_btn->setAutoRaise(true);
    adv_btn->setCheckable(true);
    adv_btn->setStyleSheet(R"(
        QToolButton { padding: 3px 6px; border-radius: 3px;
                      border: 1px solid #444; color: #bbb; }
        QToolButton:checked { background: #e67e22; border-color: #e67e22; color: white; }
        QToolButton:hover:!checked { background: #333; }
    )");
    layout->addWidget(adv_btn);
    QObject::connect(adv_btn, &QToolButton::clicked, this,
                     [this, adv_btn](bool) { emit advanced_search_requested(); });

    // Przyciski trybu — wzajemnie wykluczające
    auto set_view_mode = [this](QToolButton* btn, CompareMode mode) {
        for (auto* b : {m_single_btn, m_split2_btn, m_split4_btn})
            b->setChecked(b == btn);
        emit view_mode_changed(mode);
    };
    QObject::connect(m_single_btn, &QToolButton::clicked, this,
        [this, set_view_mode]() { set_view_mode(m_single_btn, CompareMode::Single); });
    QObject::connect(m_split2_btn, &QToolButton::clicked, this,
        [this, set_view_mode]() { set_view_mode(m_split2_btn, CompareMode::Split2); });
    QObject::connect(m_split4_btn, &QToolButton::clicked, this,
        [this, set_view_mode]() { set_view_mode(m_split4_btn, CompareMode::Split4); });
}

void FilterBar::emit_filter() {
    GridFilter f;

    if (m_pick_only->isChecked())
        f.pick_flag = PickFlag::Pick;
    else if (m_reject_only->isChecked())
        f.pick_flag = PickFlag::Reject;
    else
        f.pick_flag = PickFlag::None;

    f.only_raw      = m_raw_only->isChecked();
    f.only_psd      = m_psd_only->isChecked();
    f.name_contains = m_search->text().trimmed();
    f.sort_mode     = m_sort_combo
                      ? static_cast<SortMode>(m_sort_combo->currentIndex())
                      : SortMode::NameAsc;

    emit filter_changed(f);
}

// ─── Ikony trybów (rysowane programowo) ──────────────────────────────────────

QPixmap FilterBar::make_single_icon() {
    QPixmap pix(18, 18);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#bbb"), 1.5));
    p.setBrush(Qt::NoBrush);
    // Jeden prostokąt wypełniający całość
    p.drawRect(2, 2, 14, 14);
    return pix;
}

QPixmap FilterBar::make_split2_icon() {
    QPixmap pix(18, 18);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#bbb"), 1.5));
    p.setBrush(Qt::NoBrush);
    // Prostokąt podzielony pionową kreską na 2 części
    p.drawRect(2, 2, 14, 14);
    p.drawLine(9, 2, 9, 16);
    return pix;
}

QPixmap FilterBar::make_split4_icon() {
    QPixmap pix(18, 18);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#bbb"), 1.5));
    p.setBrush(Qt::NoBrush);
    // Prostokąt podzielony na 4 części
    p.drawRect(2, 2, 14, 14);
    p.drawLine(9, 2, 9, 16);   // pionowa
    p.drawLine(2, 9, 16, 9);   // pozioma
    return pix;
}

} // namespace LapesEye