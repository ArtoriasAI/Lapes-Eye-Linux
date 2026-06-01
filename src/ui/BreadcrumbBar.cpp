#include "LapesEye/ui/BreadcrumbBar.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QDir>
#include <QLabel>
#include <QApplication>
#include <QFontMetrics>
#include <QTimer>
#include <QMenu>

namespace LapesEye {

BreadcrumbBar::BreadcrumbBar(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(28);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0,0,0,0);
    m_layout->setSpacing(0);

    // Widget z breadcrumbs
    m_crumbs = new QWidget(this);
    m_crumbs_layout = new QHBoxLayout(m_crumbs);
    m_crumbs_layout->setContentsMargins(4,0,4,0);
    m_crumbs_layout->setSpacing(0);
    m_crumbs_layout->addStretch();
    m_layout->addWidget(m_crumbs);

    // LineEdit (ukryty domyślnie)
    m_edit = new QLineEdit(this);
    m_edit->hide();
    m_edit->installEventFilter(this);
    m_layout->addWidget(m_edit);

    setStyleSheet(R"(
        BreadcrumbBar { background: #1e1e1e; border: 1px solid #444; border-radius: 4px; }
        QToolButton {
            background: transparent; color: #ccc; border: none;
            padding: 2px 6px; font-size: 13px;
        }
        QToolButton:hover { background: #333; border-radius: 3px; color: #fff; }
        QToolButton:pressed { background: #2D7DD2; color: #fff; border-radius: 3px; }
        QLabel { color: #666; padding: 0 2px; font-size: 13px; }
        QLineEdit {
            background: #1e1e1e; color: #fff; border: none;
            padding: 2px 6px; font-size: 13px; selection-background-color: #2D7DD2;
        }
    )");

    QObject::connect(m_edit, &QLineEdit::returnPressed, this, [this]() {
        QString p = m_edit->text().trimmed();
        if (p.startsWith("~/")) p = QDir::homePath() + p.mid(1);
        if (QFileInfo(p).isFile()) p = QFileInfo(p).absolutePath();  // plik → folder
        if (QFileInfo::exists(p)) {
            leave_edit_mode();
            emit navigate_requested(p);
        }
    });
}

void BreadcrumbBar::set_path(const QString& path) {
    m_path = path;
    if (!m_editing) rebuild_breadcrumbs();
}

void BreadcrumbBar::enter_edit_mode() {
    if (m_editing) return;
    m_editing = true;
    m_crumbs->hide();
    m_edit->setText(m_path);
    m_edit->show();
    m_edit->setFocus();
    m_edit->selectAll();
}

void BreadcrumbBar::leave_edit_mode() {
    if (!m_editing) return;
    m_editing = false;
    m_edit->hide();
    m_crumbs->show();
    rebuild_breadcrumbs();
    // Zwróć fokus do widgetu nadrzędnego (siatka miniatur)
    if (parentWidget()) parentWidget()->setFocus();
}

void BreadcrumbBar::rebuild_breadcrumbs() {
    // Usuń wszystkie stare widgety z layoutu (oprócz stretch)
    while (m_crumbs_layout->count() > 1)
        delete m_crumbs_layout->takeAt(0)->widget();

    if (m_path.isEmpty()) return;

    // Podziel ścieżkę na segmenty
    QStringList parts = m_path.split('/', Qt::SkipEmptyParts);

    // Buduj segmenty: każdy to przycisk z pełną ścieżką do tego miejsca
    QString cumulative = "";
    for (int i = 0; i < parts.size(); ++i) {
        // Ścieżka poprzedniego segmentu (przed dodaniem bieżącego)
        QString prev_path = cumulative;
        cumulative += "/" + parts[i];
        QString seg_path = cumulative;
        QString seg_name = parts[i];

        // Separator "›" między segmentami
        if (i > 0) {
            // Menu pokazuje podfoldery POPRZEDNIEGO segmentu (jak w Dolphin)
            auto* sep = new QToolButton(m_crumbs);
            sep->setText("›");
            sep->setFixedWidth(16);
            sep->setStyleSheet("QToolButton { color: #666; padding: 0; border: none; font-size: 14px; }"
                               "QToolButton:hover { color: #fff; }");
            QObject::connect(sep, &QToolButton::clicked, this, [this, prev_path, sep]() {
                QMenu* menu = new QMenu(this);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                QDir dir(prev_path);
                const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                if (entries.isEmpty()) {
                    menu->addAction("(brak podfolderów)")->setEnabled(false);
                } else {
                    for (const QString& sub : entries) {
                        QString sub_path = prev_path + "/" + sub;
                        menu->addAction("📁 " + sub, this, [this, sub_path]() {
                            emit navigate_requested(sub_path);
                        });
                    }
                }
                menu->popup(sep->mapToGlobal(QPoint(0, sep->height())));
            });
            m_crumbs_layout->insertWidget(m_crumbs_layout->count()-1, sep);
        }

        auto* btn = new QToolButton(m_crumbs);
        btn->setText(seg_name);
        // Ostatni segment — pogrubiony (aktualny folder)
        if (i == parts.size() - 1) {
            QFont f = btn->font();
            f.setBold(true);
            btn->setFont(f);
            btn->setStyleSheet("QToolButton { color: #fff; font-weight: bold; }");
        }
        QObject::connect(btn, &QToolButton::clicked, this, [this, seg_path]() {
            emit navigate_requested(seg_path);
        });
        m_crumbs_layout->insertWidget(m_crumbs_layout->count()-1, btn);

        // Po ostatnim segmencie — separator pokazujący podfoldery aktualnego folderu
        if (i == parts.size() - 1) {
            auto* end_sep = new QToolButton(m_crumbs);
            end_sep->setText("›");
            end_sep->setFixedWidth(16);
            end_sep->setStyleSheet("QToolButton { color: #555; padding: 0; border: none; font-size: 14px; }"
                                   "QToolButton:hover { color: #aaa; }");
            QObject::connect(end_sep, &QToolButton::clicked, this, [this, seg_path, end_sep]() {
                QMenu* menu = new QMenu(this);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                QDir dir(seg_path);
                const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                if (entries.isEmpty()) {
                    menu->addAction("(brak podfolderów)")->setEnabled(false);
                } else {
                    for (const QString& sub : entries) {
                        QString sub_path = seg_path + "/" + sub;
                        menu->addAction("📁 " + sub, this, [this, sub_path]() {
                            emit navigate_requested(sub_path);
                        });
                    }
                }
                menu->popup(end_sep->mapToGlobal(QPoint(0, end_sep->height())));
            });
            m_crumbs_layout->insertWidget(m_crumbs_layout->count()-1, end_sep);
        }
    }
}

void BreadcrumbBar::mousePressEvent(QMouseEvent* e) {
    // Kliknięcie w pusty obszar breadcrumb → tryb edycji
    if (!m_editing && e->button() == Qt::LeftButton) {
        QWidget* child = childAt(e->pos());
        if (!child || child == m_crumbs) {
            enter_edit_mode();
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

bool BreadcrumbBar::eventFilter(QObject* obj, QEvent* e) {
    if (obj == m_edit && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Escape) {
            leave_edit_mode();
            return true;
        }
    }
    if (obj == m_edit && e->type() == QEvent::FocusOut) {
        // Opóźnij żeby returnPressed zdążył się wykonać
        QTimer::singleShot(150, this, [this]() {
            if (m_editing) leave_edit_mode();
        });
    }
    return QWidget::eventFilter(obj, e);
}

} // namespace LapesEye
