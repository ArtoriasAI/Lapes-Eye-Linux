#pragma once
#include <QWidget>
#include <QStringList>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QToolButton>

namespace LapesEye {

// Pasek nawigacji w stylu Dolphin:
// - tryb breadcrumb: przyciski dla każdego segmentu ścieżki
// - tryb edycji: QLineEdit z pełną ścieżką (po kliknięciu w pusty obszar lub Ctrl+L)
class BreadcrumbBar : public QWidget {
    Q_OBJECT
public:
    explicit BreadcrumbBar(QWidget* parent = nullptr);
    void set_path(const QString& path);
    QString path() const { return m_path; }
    void enter_edit_mode();
    void leave_edit_mode();
    bool is_editing() const { return m_editing; }

signals:
    void navigate_requested(const QString& path);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void rebuild_breadcrumbs();

    QString         m_path;
    bool            m_editing = false;
    QHBoxLayout*    m_layout  = nullptr;
    QLineEdit*      m_edit    = nullptr;
    QWidget*        m_crumbs  = nullptr;
    QHBoxLayout*    m_crumbs_layout = nullptr;
};

} // namespace LapesEye
