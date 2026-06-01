#include "LapesEye/ui/TearOffTabBar.h"
#include <QMouseEvent>
#include <QApplication>

namespace LapesEye {

TearOffTabBar::TearOffTabBar(QWidget* parent) : QTabBar(parent) {
    setMouseTracking(true);
}

void TearOffTabBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_press_pos   = e->pos();
        m_drag_tab    = tabAt(e->pos());
        m_tear_active = false;
    }
    QTabBar::mousePressEvent(e);
}

void TearOffTabBar::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag_tab < 0 || !(e->buttons() & Qt::LeftButton)) {
        QTabBar::mouseMoveEvent(e);
        return;
    }

    int dist = (e->pos() - m_press_pos).manhattanLength();

    // Gdy zakładka wyjdzie poza pasek pionowo (> 40px), otwórz nowe okno
    if (!m_tear_active && dist > QApplication::startDragDistance()) {
        QPoint global = mapToGlobal(e->pos());
        QRect bar_rect = QRect(mapToGlobal(QPoint(0,0)), size());
        // Tear-off gdy kursor jest co najmniej 40px poniżej paska
        if (global.y() > bar_rect.bottom() + 40) {
            m_tear_active = true;
            QString path = tabToolTip(m_drag_tab);
            emit tab_torn_off(m_drag_tab, path);
            m_drag_tab = -1;
            return;
        }
    }

    QTabBar::mouseMoveEvent(e);
}

void TearOffTabBar::mouseReleaseEvent(QMouseEvent* e) {
    m_drag_tab    = -1;
    m_tear_active = false;
    QTabBar::mouseReleaseEvent(e);
}

} // namespace LapesEye
