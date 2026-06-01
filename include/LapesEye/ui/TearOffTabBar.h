#pragma once
#include <QTabBar>
#include <QTabWidget>
#include <QPoint>

namespace LapesEye {

// QTabBar z obsługą "tear-off" — przeciągnięcie zakładki poza pasek otwiera nowe okno
class TearOffTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit TearOffTabBar(QWidget* parent = nullptr);

signals:
    void tab_torn_off(int index, const QString& path);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QPoint  m_press_pos;
    int     m_drag_tab    = -1;
    bool    m_tear_active = false;
};

// QTabWidget który pozwala ustawić własny TabBar (setTabBar jest protected)
class TearOffTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit TearOffTabWidget(QWidget* parent = nullptr) : QTabWidget(parent) {
        auto* bar = new TearOffTabBar(this);
        bar->setFocusPolicy(Qt::NoFocus);  // QTabBar nie kradnie focusu od siatki
        setTabBar(bar);  // dostępne bo jesteśmy w subklasie
        QObject::connect(bar, &TearOffTabBar::tab_torn_off,
                         this, &TearOffTabWidget::tab_torn_off);
    }

signals:
    void tab_torn_off(int index, const QString& path);
};

} // namespace LapesEye
