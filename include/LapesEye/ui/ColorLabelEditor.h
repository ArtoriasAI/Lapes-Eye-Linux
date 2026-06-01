#pragma once
#include <QDialog>
#include <QList>
#include <QString>
#include <QColor>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>

namespace LapesEye {

struct CustomLabel {
    QString name;
    QColor  color;
};

// Globalne ustawienia etykiet — 5 slotów
class LabelConfig {
public:
    static const int COUNT = 5;

    static QList<CustomLabel> load() {
        QSettings s("Lape", "LapesEye");
        QList<CustomLabel> list;
        // Domyślne wartości
        const CustomLabel defaults[COUNT] = {
            {"Czerwona",  QColor(0xE5, 0x34, 0x34)},
            {"Żółta",     QColor(0xF0, 0xC0, 0x20)},
            {"Zielona",   QColor(0x2E, 0xCC, 0x71)},
            {"Niebieska", QColor(0x30, 0x98, 0xDB)},
            {"Fioletowa", QColor(0x9B, 0x59, 0xB6)},
        };
        for (int i = 0; i < COUNT; ++i) {
            s.beginGroup(QString("label_%1").arg(i));
            CustomLabel lbl;
            lbl.name  = s.value("name",  defaults[i].name).toString();
            lbl.color = QColor(s.value("color", defaults[i].color.name()).toString());
            if (!lbl.color.isValid()) lbl.color = defaults[i].color;
            s.endGroup();
            list << lbl;
        }
        return list;
    }

    static void save(const QList<CustomLabel>& list) {
        QSettings s("Lape", "LapesEye");
        for (int i = 0; i < qMin(list.size(), COUNT); ++i) {
            s.beginGroup(QString("label_%1").arg(i));
            s.setValue("name",  list[i].name);
            s.setValue("color", list[i].color.name());
            s.endGroup();
        }
    }
};

// Dialog edycji etykiet
class ColorLabelEditor : public QDialog {
    Q_OBJECT
public:
    explicit ColorLabelEditor(QWidget* parent = nullptr);
    QList<CustomLabel> labels() const;

private:
    void pick_color(int idx);
    void update_button(int idx);

    struct Row {
        QLineEdit*   name_edit   = nullptr;
        QPushButton* color_btn   = nullptr;
        QColor       color;
    };
    QList<Row> m_rows;
};

} // namespace LapesEye
