#include "LapesEye/ui/ColorLabelEditor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QPainter>

namespace LapesEye {

ColorLabelEditor::ColorLabelEditor(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Edytuj etykiety kolorowe");
    setMinimumWidth(340);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Dostosuj nazwy i kolory etykiet:", this));

    auto* grid = new QGridLayout();
    grid->setSpacing(6);
    grid->addWidget(new QLabel("Nr",     this), 0, 0);
    grid->addWidget(new QLabel("Kolor",  this), 0, 1);
    grid->addWidget(new QLabel("Nazwa",  this), 0, 2);

    auto current = LabelConfig::load();
    m_rows.resize(LabelConfig::COUNT);

    for (int i = 0; i < LabelConfig::COUNT; ++i) {
        grid->addWidget(new QLabel(QString::number(i + 1), this), i + 1, 0);

        m_rows[i].color    = current[i].color;
        m_rows[i].color_btn = new QPushButton(this);
        m_rows[i].color_btn->setFixedSize(32, 24);
        m_rows[i].color_btn->setFlat(true);
        update_button(i);
        QObject::connect(m_rows[i].color_btn, &QPushButton::clicked,
                         this, [this, i]() { pick_color(i); });

        m_rows[i].name_edit = new QLineEdit(current[i].name, this);
        m_rows[i].name_edit->setMaxLength(32);

        grid->addWidget(m_rows[i].color_btn,  i + 1, 1);
        grid->addWidget(m_rows[i].name_edit,  i + 1, 2);
    }
    layout->addLayout(grid);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(btns);
}

void ColorLabelEditor::pick_color(int idx) {
    QColor c = QColorDialog::getColor(m_rows[idx].color, this,
                                       "Wybierz kolor etykiety");
    if (c.isValid()) {
        m_rows[idx].color = c;
        update_button(idx);
    }
}

void ColorLabelEditor::update_button(int idx) {
    QPixmap px(28, 18);
    px.fill(m_rows[idx].color);
    QPainter p(&px);
    p.setPen(QColor(0,0,0,80));
    p.drawRect(0, 0, 27, 17);
    m_rows[idx].color_btn->setIcon(QIcon(px));
    m_rows[idx].color_btn->setIconSize(px.size());
}

QList<CustomLabel> ColorLabelEditor::labels() const {
    QList<CustomLabel> list;
    for (const auto& row : m_rows)
        list << CustomLabel{row.name_edit->text().trimmed(), row.color};
    return list;
}

} // namespace LapesEye
