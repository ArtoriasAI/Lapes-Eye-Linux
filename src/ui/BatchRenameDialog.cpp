#include "LapesEye/ui/BatchRenameDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QDate>
#include <QRegularExpression>

namespace LapesEye {

BatchRenameDialog::BatchRenameDialog(const QStringList& paths, QWidget* parent)
    : QDialog(parent), m_paths(paths)
{
    setWindowTitle("Zmiana nazw wsadowych");
    setMinimumWidth(520);
    resize(600, 440);

    auto* layout = new QVBoxLayout(this);

    // ── Ustawienia ─────────────────────────────────────────────────────────────
    auto* settings_box = new QGroupBox("Ustawienia", this);
    auto* form = new QFormLayout(settings_box);

    // Prefix (tekst przed licznikiem)
    m_prefix = new QLineEdit(this);
    m_prefix->setPlaceholderText("np. Sesja_2026, Wakacje, DSC ...");
    form->addRow("Prefiks:", m_prefix);

    // Sufiks literowy (opcjonalnie)
    auto* suffix_row = new QHBoxLayout();
    m_letter_suffix = new QLineEdit(this);
    m_letter_suffix->setMaxLength(3);
    m_letter_suffix->setFixedWidth(50);
    m_letter_suffix->setPlaceholderText("a");
    m_letter_suffix->setToolTip(
        "Litera po numerze: puste = brak, 'a' = 1a 2a 3a, 'c' = 1c 2c 3c");
    auto* suffix_lbl = new QLabel("(opcjonalnie — dodawana po numerze, np. 1a, 2a)", this);
    suffix_lbl->setStyleSheet("color: #888; font-size: 10px;");
    suffix_row->addWidget(m_letter_suffix);
    suffix_row->addWidget(suffix_lbl);
    suffix_row->addStretch();
    form->addRow("Sufiks literowy:", suffix_row);

    // Licznik startowy
    m_start_num = new QSpinBox(this);
    m_start_num->setRange(0, 99999);
    m_start_num->setValue(1);
    form->addRow("Licznik od:", m_start_num);

    // Podgląd wyniku
    auto* example_label = new QLabel("Przykład: ", this);
    m_example = new QLabel(this);
    m_example->setStyleSheet("color: #2D7DD2; font-weight: bold;");
    auto* ex_row = new QHBoxLayout();
    ex_row->addWidget(example_label);
    ex_row->addWidget(m_example);
    ex_row->addStretch();
    form->addRow("", ex_row);

    layout->addWidget(settings_box);

    // ── Podgląd listy ────────────────────────────────────────────────────────
    layout->addWidget(new QLabel(
        QString("Podgląd (%1 pliki):").arg(paths.size()), this));

    m_preview = new QListWidget(this);
    m_preview->setAlternatingRowColors(true);
    layout->addWidget(m_preview, 1);

    // ── Przyciski ──────────────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Zmień nazwy");
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &BatchRenameDialog::apply);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Aktualizuj podgląd przy każdej zmianie
    QObject::connect(m_prefix,        &QLineEdit::textChanged,  this, &BatchRenameDialog::update_preview);
    QObject::connect(m_letter_suffix, &QLineEdit::textChanged,  this, &BatchRenameDialog::update_preview);
    QObject::connect(m_start_num, qOverload<int>(&QSpinBox::valueChanged),
                                                                this, &BatchRenameDialog::update_preview);

    update_preview();
}

QString BatchRenameDialog::build_name(const QString& orig_path, int counter) const {
    QFileInfo fi(orig_path);
    QString ext = fi.suffix();  // oryginalne rozszerzenie — bez zmiany

    QString prefix  = m_prefix->text().trimmed();
    QString letter  = m_letter_suffix->text().trimmed();
    int     num     = counter;

    // Format: PREFIX + NUM + LETTER + .EXT
    // np. "Sesja_2026" + "3" + "a" + ".jpg" → "Sesja_20263a.jpg"
    QString base = prefix + QString::number(num) + letter;

    return ext.isEmpty() ? base : base + "." + ext;
}

void BatchRenameDialog::update_preview() {
    m_preview->clear();
    int c = m_start_num->value();
    for (const auto& path : m_paths) {
        QString newn = build_name(path, c);
        QFileInfo fi(path);
        m_preview->addItem(fi.fileName() + "  →  " + newn);
        ++c;
    }

    // Aktualizuj przykład
    if (!m_paths.isEmpty())
        m_example->setText(build_name(m_paths.first(), m_start_num->value())
            + " … " + build_name(m_paths.last(), m_start_num->value() + m_paths.size() - 1));
}

void BatchRenameDialog::apply() {
    m_pairs.clear();
    int c = m_start_num->value();
    for (const auto& path : m_paths) {
        QString newn = build_name(path, c++);
        QFileInfo fi(path);
        QString new_path = fi.dir().filePath(newn);
        if (new_path != path) m_pairs.append({path, newn});
    }
    accept();
}

} // namespace LapesEye
