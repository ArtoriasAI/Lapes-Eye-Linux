#pragma once
#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QStringList>
#include <QResizeEvent>

namespace LapesEye {

enum class CompareMode { Single, Split2, Split4 };

// Jeden panel podglądu w siatce porównania
class CompareCell : public QWidget {
    Q_OBJECT
public:
    explicit CompareCell(QWidget* parent = nullptr);
    void load(const QString& path);
    void clear();
    QString path() const { return m_path; }

signals:
    void clicked(const QString& path);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    void reload_scaled();

    QString  m_path;
    QLabel*  m_label     = nullptr;
    QLabel*  m_name      = nullptr;
    QPixmap  m_full_pix;   // pełna miniatura załadowana w tle
    bool     m_loading   = false;
};

// Widget porównania — 1×1, 1×2 lub 2×2
class CompareView : public QWidget {
    Q_OBJECT
public:
    explicit CompareView(QWidget* parent = nullptr);

    void set_mode(CompareMode mode);
    CompareMode mode() const { return m_mode; }

    // Załaduj ścieżki do porównania (max 4)
    void set_paths(const QStringList& paths);

    // Zaktualizuj primary (zaznaczone zdjęcie) — dla trybu Single
    void set_primary(const QString& path);

signals:
    void cell_clicked(const QString& path);

private:
    void rebuild();

    CompareMode              m_mode  = CompareMode::Single;
    QStringList              m_paths;
    QList<CompareCell*>      m_cells;
    QGridLayout*             m_grid  = nullptr;
};

} // namespace LapesEye
