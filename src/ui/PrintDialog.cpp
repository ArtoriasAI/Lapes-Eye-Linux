#include "LapesEye/ui/PrintDialog.h"
#include "LapesEye/core/MetaStore.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPageSetupDialog>
#include <QImageReader>
#include <QFileInfo>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QScrollArea>
#include <cmath>

namespace LapesEye {

PrintDialog::PrintDialog(const QStringList& paths, QWidget* parent)
    : QDialog(parent), m_paths(paths)
{
    setWindowTitle(QString("Drukowanie — %1 plik(ów)").arg(paths.size()));
    setMinimumSize(800, 520);

    auto* root = new QHBoxLayout(this);

    // ── Lewa: opcje ───────────────────────────────────────────────────────────
    auto* left  = new QWidget(this);
    auto* llayout = new QVBoxLayout(left);
    llayout->setSpacing(8);
    left->setFixedWidth(320);
    root->addWidget(left);

    // Tryb drukowania
    auto* mode_box = new QGroupBox("Tryb drukowania", left);
    auto* mode_lay = new QVBoxLayout(mode_box);
    auto* mode_grp = new QButtonGroup(this);
    m_mode_single  = new QRadioButton("Pojedyncze zdjęcie", mode_box);
    m_mode_contact = new QRadioButton(
        QString("Arkusz kontaktowy (%1 zdjęć)").arg(paths.size()), mode_box);
    m_mode_single->setChecked(true);
    mode_grp->addButton(m_mode_single, 0);
    mode_grp->addButton(m_mode_contact, 1);
    mode_lay->addWidget(m_mode_single);
    mode_lay->addWidget(m_mode_contact);
    llayout->addWidget(mode_box);

    // ── Opcje pojedynczego ────────────────────────────────────────────────────
    auto* single_box = new QGroupBox("Opcje — pojedyncze zdjęcie", left);
    auto* single_lay = new QFormLayout(single_box);
    m_fit_mode = new QComboBox(single_box);
    m_fit_mode->addItems({"Dopasuj do strony", "Wypełnij stronę", "Rozmiar rzeczywisty (100%)"});
    m_caption_chk = new QCheckBox("Nazwa pliku pod zdjęciem", single_box);
    m_caption_chk->setChecked(true);
    m_exif_chk = new QCheckBox("Dane EXIF pod zdjęciem", single_box);
    single_lay->addRow("Dopasowanie:", m_fit_mode);
    single_lay->addRow(m_caption_chk);
    single_lay->addRow(m_exif_chk);
    llayout->addWidget(single_box);

    // ── Opcje arkusza kontaktowego ────────────────────────────────────────────
    auto* contact_box = new QGroupBox("Opcje — arkusz kontaktowy", left);
    auto* contact_lay = new QFormLayout(contact_box);
    contact_box->setVisible(false);

    m_cols = new QSpinBox(contact_box);
    m_cols->setRange(1, 10); m_cols->setValue(4);
    m_rows = new QSpinBox(contact_box);
    m_rows->setRange(1, 10); m_rows->setValue(5);

    auto* grid_row = new QHBoxLayout();
    grid_row->addWidget(m_cols);
    grid_row->addWidget(new QLabel("×", contact_box));
    grid_row->addWidget(m_rows);
    grid_row->addStretch();
    contact_lay->addRow("Siatka (kolumny × wiersze):", grid_row);

    m_margin = new QDoubleSpinBox(contact_box);
    m_margin->setRange(0, 30); m_margin->setValue(5.0);
    m_margin->setSuffix(" mm");
    contact_lay->addRow("Margines:", m_margin);

    m_contact_names = new QCheckBox("Nazwy plików pod miniaturami", contact_box);
    m_contact_names->setChecked(true);
    m_border_chk = new QCheckBox("Ramka wokół miniatur", contact_box);
    m_border_chk->setChecked(true);
    contact_lay->addRow(m_contact_names);
    contact_lay->addRow(m_border_chk);
    llayout->addWidget(contact_box);

    // Włącz/wyłącz sekcje zależnie od trybu
    QObject::connect(m_mode_single, &QRadioButton::toggled, this,
        [single_box, contact_box](bool checked) {
            single_box->setVisible(checked);
            contact_box->setVisible(!checked);
        });

    m_info_lbl = new QLabel(left);
    m_info_lbl->setWordWrap(true);
    m_info_lbl->setStyleSheet("color:#aaa; font-size:11px;");
    llayout->addWidget(m_info_lbl);
    llayout->addStretch();

    // Przyciski
    auto* btns = new QDialogButtonBox(left);
    auto* setup_btn  = btns->addButton("Ustawienia strony...", QDialogButtonBox::ActionRole);
    auto* print_btn  = btns->addButton("Drukuj", QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    print_btn->setDefault(true);
    llayout->addWidget(btns);

    QObject::connect(setup_btn, &QPushButton::clicked, this, [this]() {
        QPageSetupDialog dlg(m_printer, this);
        dlg.exec();
        schedule_preview();
    });
    QObject::connect(btns, &QDialogButtonBox::accepted, this, &PrintDialog::do_print);
    QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ── Prawa: podgląd ────────────────────────────────────────────────────────
    auto* right  = new QWidget(this);
    auto* rlayout = new QVBoxLayout(right);
    rlayout->addWidget(new QLabel("<b>Podgląd</b>", right));
    m_preview_lbl = new QLabel(right);
    m_preview_lbl->setMinimumSize(420, 420);
    m_preview_lbl->setAlignment(Qt::AlignCenter);
    m_preview_lbl->setStyleSheet(
        "background: white; border: 1px solid #888;");
    m_preview_lbl->setText("Ładowanie...");
    rlayout->addWidget(m_preview_lbl, 1);
    root->addWidget(right, 1);

    // Timer podglądu (debounce)
    m_preview_timer = new QTimer(this);
    m_preview_timer->setSingleShot(true);
    m_preview_timer->setInterval(250);
    QObject::connect(m_preview_timer, &QTimer::timeout,
                     this, &PrintDialog::update_preview);

    // Podłącz schedule_preview do wszystkich kontrolek
    auto refresh = [this]() { schedule_preview(); };
    QObject::connect(m_mode_single,    &QRadioButton::toggled,   this, [refresh](bool){ refresh(); });
    QObject::connect(m_mode_contact,   &QRadioButton::toggled,   this, [refresh](bool){ refresh(); });
    QObject::connect(m_fit_mode,       QOverload<int>::of(&QComboBox::currentIndexChanged), this, [refresh](int){ refresh(); });
    QObject::connect(m_caption_chk,    &QCheckBox::toggled,      this, [refresh](bool){ refresh(); });
    QObject::connect(m_exif_chk,       &QCheckBox::toggled,      this, [refresh](bool){ refresh(); });
    QObject::connect(m_cols,           QOverload<int>::of(&QSpinBox::valueChanged), this, [refresh](int){ refresh(); });
    QObject::connect(m_rows,           QOverload<int>::of(&QSpinBox::valueChanged), this, [refresh](int){ refresh(); });
    QObject::connect(m_contact_names,  &QCheckBox::toggled,      this, [refresh](bool){ refresh(); });
    QObject::connect(m_border_chk,     &QCheckBox::toggled,      this, [refresh](bool){ refresh(); });
    QObject::connect(m_margin,         QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [refresh](double){ refresh(); });

    // Trwały QPrinter — zachowuje orientację między wywołaniami
    m_printer = new QPrinter(QPrinter::HighResolution);
    m_printer->setColorMode(QPrinter::Color);

    // Załaduj miniatury wszystkich plików w tle
    if (!paths.isEmpty()) {
        auto fut = QtConcurrent::run([this, paths]() {
            QList<QImage> thumbs;
            for (const QString& path : paths) {
                QImageReader reader(path);
                reader.setAutoTransform(true);
                QSize sz = reader.size().scaled(400, 400, Qt::KeepAspectRatio);
                reader.setScaledSize(sz);
                QImage img = reader.read();
                thumbs << img;
            }
            QMetaObject::invokeMethod(this, [this, thumbs]() {
                m_thumbs = thumbs;
                if (!thumbs.isEmpty()) m_preview_src = thumbs.first();
                update_preview();
            }, Qt::QueuedConnection);
        });
        Q_UNUSED(fut);
    }
}

void PrintDialog::schedule_preview() {
    if (m_preview_timer) m_preview_timer->start();
}

// ── Rysowanie podglądu ────────────────────────────────────────────────────────

void PrintDialog::update_preview() {
    if (!m_preview_lbl) return;

    // Pobierz proporcje strony z m_printer (uwzględnia orientację)
    QSizeF page_mm = m_printer->pageLayout().pageSize().size(QPageSize::Millimeter);
    // Zamień wymiary jeśli landscape
    if (m_printer->pageLayout().orientation() == QPageLayout::Landscape)
        page_mm.transpose();
    double pw_mm = page_mm.width();
    double ph_mm = page_mm.height();

    // Przelicz na piksele podglądu
    QSize avail = m_preview_lbl->size();
    double scale;
    if (pw_mm / ph_mm > (double)avail.width() / avail.height())
        scale = avail.width() / pw_mm;
    else
        scale = avail.height() / ph_mm;

    int pw = int(pw_mm * scale);
    int ph = int(ph_mm * scale);

    QImage canvas(pw, ph, QImage::Format_RGB32);
    canvas.fill(Qt::white);
    QPainter p(&canvas);

    double margin_px = m_margin->value() / pw_mm * pw;

    if (m_mode_single->isChecked()) {
        if (m_preview_src.isNull()) {
            p.setPen(Qt::gray);
            p.drawText(canvas.rect(), Qt::AlignCenter, "Ładowanie...");
        } else {
            // Dla Wypełnij — cały canvas, bez marginesów
            bool fill_mode = (m_fit_mode->currentIndex() == 1);
            QRectF area = fill_mode
                ? QRectF(0, 0, pw, ph)
                : QRectF(margin_px, margin_px, pw - 2*margin_px, ph - 2*margin_px);

            double caption_h = (!fill_mode && (m_caption_chk->isChecked() || m_exif_chk->isChecked()))
                               ? 18 : 0;
            area.adjust(0, 0, 0, -caption_h);

            QImage img;
            switch (m_fit_mode->currentIndex()) {
                case 0: // Dopasuj
                    img = m_preview_src.scaled(area.size().toSize(),
                          Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    break;
                case 1: { // Wypełnij — skaluj i przytnij do całej strony
                    QImage exp = m_preview_src.scaled(area.size().toSize(),
                          Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    int cx = (exp.width()  - int(area.width()))  / 2;
                    int cy = (exp.height() - int(area.height())) / 2;
                    img = exp.copy(cx, cy, int(area.width()), int(area.height()));
                    break;
                }
                default: // 100%
                    img = m_preview_src;
                    break;
            }
            double ix = area.x() + (area.width()  - img.width())  / 2;
            double iy = area.y() + (area.height() - img.height()) / 2;
            p.setClipRect(area);
            p.drawImage(QPointF(ix, iy), img);
            p.setClipping(false);

            if (!fill_mode && m_caption_chk->isChecked() && !m_paths.isEmpty()) {
                p.setPen(Qt::black);
                QFont f = p.font(); f.setPixelSize(10); p.setFont(f);
                p.drawText(QRectF(margin_px, ph - caption_h - margin_px/2,
                                  pw - 2*margin_px, caption_h),
                           Qt::AlignCenter,
                           QFileInfo(m_paths.first()).fileName());
            }
        }
    } else {
        // Arkusz kontaktowy
        int cols  = m_cols->value();
        int rows  = m_rows->value();
        int total = cols * rows;

        double cell_w = (pw - 2*margin_px) / cols;
        double cell_h = (ph - 2*margin_px) / rows;
        double cap_h  = m_contact_names->isChecked() ? 14 : 0;

        QFont f = p.font(); f.setPixelSize(7); p.setFont(f);

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int idx = r * cols + c;
                double x = margin_px + c * cell_w;
                double y = margin_px + r * cell_h;
                QRectF cell(x, y, cell_w, cell_h);
                QRectF img_rect(x+2, y+2, cell_w-4, cell_h-4-cap_h);

                if (m_border_chk->isChecked()) {
                    p.setPen(QPen(Qt::gray, 0.5));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(cell);
                }

                if (idx < m_thumbs.size() && !m_thumbs[idx].isNull()) {
                    // Rysuj rzeczywistą miniaturę
                    QImage thumb = m_thumbs[idx].scaled(
                        img_rect.size().toSize(),
                        Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    double tx = x + 2 + (img_rect.width()  - thumb.width())  / 2;
                    double ty = y + 2 + (img_rect.height() - thumb.height()) / 2;
                    p.drawImage(QPointF(tx, ty), thumb);
                } else if (idx < m_paths.size()) {
                    p.fillRect(img_rect, QColor(220, 220, 220));
                    p.setPen(Qt::darkGray);
                    p.drawText(img_rect, Qt::AlignCenter, QString::number(idx + 1));
                } else {
                    p.fillRect(img_rect, QColor(240, 240, 240));
                }

                if (m_contact_names->isChecked() && idx < m_paths.size()) {
                    p.setPen(Qt::black);
                    p.drawText(QRectF(x+1, y+cell_h-cap_h-1, cell_w-2, cap_h),
                               Qt::AlignCenter,
                               QFileInfo(m_paths[idx]).baseName());
                }
            }
        }

        m_info_lbl->setText(QString("Arkusz: %1×%2 = %3 miniatur na stronę\n"
                                    "Pliki: %4  →  %5 stron")
            .arg(cols).arg(rows).arg(total)
            .arg(m_paths.size())
            .arg((m_paths.size() + total - 1) / total));
    }

    p.end();

    // Wyśrodkuj na etykiecie
    QPixmap bg(m_preview_lbl->size());
    bg.fill(QColor(60, 60, 60));
    QPainter bp(&bg);
    int ox = (m_preview_lbl->width()  - pw) / 2;
    int oy = (m_preview_lbl->height() - ph) / 2;
    bp.drawImage(ox, oy, canvas);
    m_preview_lbl->setPixmap(bg);
}

// ── Właściwe drukowanie ───────────────────────────────────────────────────────

void PrintDialog::do_print() {
    QPrintDialog print_dlg(m_printer, this);
    if (print_dlg.exec() != QDialog::Accepted) return;

    QPainter painter(m_printer);

    if (m_mode_single->isChecked()) {
        for (int i = 0; i < m_paths.size(); ++i) {
            if (i > 0) m_printer->newPage();
            QImageReader reader(m_paths[i]);
            reader.setAutoTransform(true);
            QImage img = reader.read();
            if (img.isNull()) continue;
            draw_single(painter, *m_printer, img,
                        m_caption_chk->isChecked()
                            ? QFileInfo(m_paths[i]).fileName() : QString());
        }
    } else {
        draw_contact_sheet(painter, *m_printer);
    }
    painter.end();
    accept();
}

void PrintDialog::draw_single(QPainter& painter, QPrinter& printer,
                               const QImage& img, const QString& caption) {
    QRect page = printer.pageLayout().paintRectPixels(printer.resolution());

    bool fill_mode = (m_fit_mode->currentIndex() == 1);
    // Dla Wypełnij — cały obszar bez marginesów (paintRect już uwzględnia marginesy drukarki)
    // Użyjemy fullPageRect żeby faktycznie wypełnić całą stronę
    QRect img_rect = fill_mode
        ? printer.pageLayout().fullRectPixels(printer.resolution())
        : page;

    int caption_h = 0;
    if (!fill_mode && (!caption.isEmpty() || m_exif_chk->isChecked()))
        caption_h = int(printer.resolution() * 0.3);
    if (!fill_mode) img_rect.adjust(0, 0, 0, -caption_h);

    QImage scaled;
    switch (m_fit_mode->currentIndex()) {
        case 0: scaled = img.scaled(img_rect.size(), Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation); break;
        case 1: {
            QImage exp = img.scaled(img_rect.size(), Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
            int cx = (exp.width()  - img_rect.width())  / 2;
            int cy = (exp.height() - img_rect.height()) / 2;
            scaled = exp.copy(cx, cy, img_rect.width(), img_rect.height());
            break;
        }
        default: scaled = img; break;
    }

    int dx = (img_rect.width()  - scaled.width())  / 2;
    int dy = (img_rect.height() - scaled.height()) / 2;
    painter.drawImage(img_rect.topLeft() + QPoint(dx, dy), scaled);

    // Podpis
    if (!caption.isEmpty()) {
        QFont f = painter.font();
        f.setPixelSize(int(printer.resolution() * 0.15));
        painter.setFont(f);
        painter.setPen(Qt::black);
        painter.drawText(QRect(page.x(), page.bottom() - caption_h,
                               page.width(), caption_h),
                         Qt::AlignCenter, caption);
    }
}

void PrintDialog::draw_contact_sheet(QPainter& painter, QPrinter& printer) {
    QRect page    = printer.pageLayout().paintRectPixels(printer.resolution());
    int   cols    = m_cols->value();
    int   rows    = m_rows->value();
    int   per_page = cols * rows;
    double margin = m_margin->value() / 25.4 * printer.resolution();
    double cell_w = (page.width()  - 2*margin) / cols;
    double cell_h = (page.height() - 2*margin) / rows;
    double caption_h = m_contact_names->isChecked()
                       ? printer.resolution() * 0.15 : 0;

    QFont caption_font = painter.font();
    caption_font.setPixelSize(qMax(8, int(printer.resolution() * 0.08)));

    for (int i = 0; i < m_paths.size(); ++i) {
        if (i > 0 && i % per_page == 0) printer.newPage();
        int pos = i % per_page;
        int c = pos % cols;
        int r = pos / cols;

        double x = page.x() + margin + c * cell_w;
        double y = page.y() + margin + r * cell_h;
        QRectF img_rect(x + 2, y + 2, cell_w - 4, cell_h - 4 - caption_h);

        // Ramka
        if (m_border_chk->isChecked()) {
            painter.setPen(QPen(Qt::gray, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(x, y, cell_w, cell_h));
        }

        // Obraz
        QImageReader reader(m_paths[i]);
        reader.setAutoTransform(true);
        QSize thumb_size = img_rect.size().toSize();
        QSize orig = reader.size();
        reader.setScaledSize(orig.scaled(thumb_size, Qt::KeepAspectRatio));
        QImage img = reader.read();
        if (!img.isNull()) {
            QImage thumb = img.scaled(thumb_size, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
            double ix = x + 2 + (img_rect.width()  - thumb.width())  / 2;
            double iy = y + 2 + (img_rect.height() - thumb.height()) / 2;
            painter.drawImage(QPointF(ix, iy), thumb);
        }

        // Podpis
        if (m_contact_names->isChecked()) {
            painter.setFont(caption_font);
            painter.setPen(Qt::black);
            painter.drawText(
                QRectF(x, y + cell_h - caption_h, cell_w, caption_h),
                Qt::AlignCenter | Qt::TextWordWrap,
                QFileInfo(m_paths[i]).baseName());
        }
    }
}

} // namespace LapesEye
