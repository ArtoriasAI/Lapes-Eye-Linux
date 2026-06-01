#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QStringList>

namespace LapesEye {

class BatchRenameDialog : public QDialog {
    Q_OBJECT
public:
    explicit BatchRenameDialog(const QStringList& paths, QWidget* parent = nullptr);

private slots:
    void update_preview();
    void apply();

public:
    // Zwraca pary (stara_ścieżka, nowa_nazwa) po akceptacji
    QList<QPair<QString,QString>> renamed_pairs() const { return m_pairs; }

private:
    QString build_name(const QString& orig_path, int counter) const;

    QLineEdit*   m_prefix        = nullptr;
    QLineEdit*   m_letter_suffix = nullptr;
    QSpinBox*    m_start_num     = nullptr;
    QLabel*      m_example       = nullptr;
    QListWidget* m_preview       = nullptr;
    QStringList                  m_paths;
    QList<QPair<QString,QString>> m_pairs;  // wypełniane przez apply()
};

} // namespace LapesEye
