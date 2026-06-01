#include "LapesEye/ui/ExportDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QScrollArea>
#include <QImageReader>
#include <QImageWriter>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QProgressDialog>
#include <QButtonGroup>
#include <QSettings>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <cmath>

namespace LapesEye {

// Forward declarations funkcji pomocniczych
static void draw_watermark(QImage& img, const QString& text,
                            int direction, int font_size, int opacity,
                            int count, int spacing);

ExportDialog::ExportDialog(const QStringList& paths, QWidget* parent)
    : QDialog(parent), m_paths(paths)
{
    setWindowTitle(QString("Eksport — %1 plik(ów)").arg(paths.size()));
    setMinimumWidth(480);

    auto* root = new QHBoxLayout(this);

    // ── Lewa strona: opcje ────────────────────────────────────────────────────
    auto* left_widget = new QWidget(this);
    auto* left_root   = new QVBoxLayout(left_widget);
    left_root->setContentsMargins(0,0,0,0);
    left_widget->setMinimumWidth(440);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget(scroll);
    scroll->setWidget(inner);
    auto* layout = new QVBoxLayout(inner);
    layout->setSpacing(10);
    left_root->addWidget(scroll, 1);
    root->addWidget(left_widget, 0);

    // ── Prawa strona: podgląd watermark ───────────────────────────────────────
    auto* right_widget = new QWidget(this);
    auto* right_layout = new QVBoxLayout(right_widget);
    right_layout->setContentsMargins(8,0,0,0);
    auto* preview_header = new QLabel("<b>Podgląd watermark</b>", right_widget);
    m_preview_lbl = new QLabel(right_widget);
    m_preview_lbl->setFixedSize(380, 280);
    m_preview_lbl->setAlignment(Qt::AlignCenter);
    m_preview_lbl->setStyleSheet("background:#111; border:1px solid #333;");
    m_preview_lbl->setText("Ładowanie podglądu...");
    right_layout->addWidget(preview_header);
    right_layout->addWidget(m_preview_lbl);
    right_layout->addStretch();
    root->addWidget(right_widget, 0);

    // Debounce timer dla podglądu
    m_preview_timer = new QTimer(this);
    m_preview_timer->setSingleShot(true);
    m_preview_timer->setInterval(300);
    QObject::connect(m_preview_timer, &QTimer::timeout, this, &ExportDialog::update_preview);

    // Załaduj miniaturę pierwszego pliku w tle
    if (!paths.isEmpty()) {
        QString first = paths.first();
        auto future = QtConcurrent::run([this, first]() {
            QImageReader reader(first);
            reader.setAutoTransform(true);
            QSize sz = reader.size().scaled(760, 560, Qt::KeepAspectRatio);
            reader.setScaledSize(sz);
            QImage img = reader.read();
            QMetaObject::invokeMethod(this, [this, img]() {
                m_preview_src = img;
                update_preview();
            }, Qt::QueuedConnection);
        });
        Q_UNUSED(future);
    }

    // ── Folder docelowy ───────────────────────────────────────────────────────
    auto* dir_box  = new QGroupBox("Folder docelowy", inner);
    auto* dir_lay  = new QHBoxLayout(dir_box);
    m_output_dir   = new QLineEdit(inner);
    QSettings s("Lape","LapesEye");
    m_output_dir->setText(s.value("export/last_dir",
        QDir::homePath() + "/Eksport").toString());
    auto* browse_btn = new QPushButton("📁", inner);
    browse_btn->setFixedWidth(32);
    browse_btn->setToolTip("Wybierz folder");
    dir_lay->addWidget(m_output_dir, 1);
    dir_lay->addWidget(browse_btn);
    QObject::connect(browse_btn, &QPushButton::clicked, this, &ExportDialog::browse_output);
    layout->addWidget(dir_box);

    // ── Format ────────────────────────────────────────────────────────────────
    auto* fmt_box  = new QGroupBox("Format wyjściowy", inner);
    auto* fmt_lay  = new QFormLayout(fmt_box);
    m_format = new QComboBox(inner);
    m_format->addItems({"JPEG", "PNG", "TIFF", "WebP", "BMP"});
    m_format->setCurrentText(s.value("export/format","JPEG").toString());

    m_quality_lbl = new QLabel("Jakość:", inner);
    m_quality = new QSpinBox(inner);
    m_quality->setRange(1, 100);
    m_quality->setValue(s.value("export/quality",90).toInt());
    m_quality->setSuffix("%");

    auto* fmt_row = new QHBoxLayout();
    fmt_row->addWidget(m_format);
    fmt_row->addSpacing(12);
    fmt_row->addWidget(m_quality_lbl);
    fmt_row->addWidget(m_quality);
    fmt_row->addStretch();
    fmt_lay->addRow("Format:", fmt_row);

    // Jakość tylko dla JPEG/WebP
    auto update_quality = [this]() {
        bool q = m_format->currentText() == "JPEG"
              || m_format->currentText() == "WebP";
        m_quality_lbl->setVisible(q);
        m_quality->setVisible(q);
    };
    update_quality();
    QObject::connect(m_format, &QComboBox::currentTextChanged,
                     this, [update_quality](const QString&) { update_quality(); });
    layout->addWidget(fmt_box);

    // ── Zmiana rozmiaru ───────────────────────────────────────────────────────
    auto* sz_box  = new QGroupBox("Zmiana rozmiaru", inner);
    auto* sz_lay  = new QVBoxLayout(sz_box);
    m_resize_chk  = new QCheckBox("Zmień rozmiar", inner);
    m_resize_chk->setChecked(s.value("export/resize",false).toBool());
    sz_lay->addWidget(m_resize_chk);

    auto* sz_opts = new QWidget(inner);
    auto* sz_opt_lay = new QHBoxLayout(sz_opts);
    sz_opt_lay->setContentsMargins(20,0,0,0);

    auto* btn_grp = new QButtonGroup(this);
    m_by_width   = new QRadioButton("Szerokość", inner);
    m_by_height  = new QRadioButton("Wysokość", inner);
    m_by_percent = new QRadioButton("Procent", inner);
    m_by_longer  = new QRadioButton("Dłuższy bok", inner);
    btn_grp->addButton(m_by_width, 0);
    btn_grp->addButton(m_by_height, 1);
    btn_grp->addButton(m_by_percent, 2);
    btn_grp->addButton(m_by_longer, 3);

    int resize_mode = s.value("export/resize_mode",0).toInt();
    btn_grp->button(resize_mode)->setChecked(true);

    m_resize_val  = new QSpinBox(inner);
    m_resize_val->setRange(1, 100000);
    m_resize_val->setValue(s.value("export/resize_val",1920).toInt());

    m_resize_unit = new QLabel("px", inner);

    sz_opt_lay->addWidget(m_by_width);
    sz_opt_lay->addWidget(m_by_height);
    sz_opt_lay->addWidget(m_by_percent);
    sz_opt_lay->addWidget(m_by_longer);
    sz_opt_lay->addSpacing(8);
    sz_opt_lay->addWidget(m_resize_val);
    sz_opt_lay->addWidget(m_resize_unit);
    sz_opt_lay->addStretch();
    sz_lay->addWidget(sz_opts);
    sz_opts->setEnabled(m_resize_chk->isChecked());

    auto update_unit = [this]() {
        if (m_by_percent->isChecked()) {
            m_resize_unit->setText("%");
            m_resize_val->setRange(1, 200);
        } else {
            m_resize_unit->setText("px");
            m_resize_val->setRange(1, 100000);
        }
    };
    update_unit();
    for (auto* rb : {m_by_width, m_by_height, m_by_percent, m_by_longer})
        QObject::connect(rb, &QRadioButton::toggled, this,
                         [update_unit](bool) { update_unit(); });

    QObject::connect(m_resize_chk, &QCheckBox::toggled, sz_opts, &QWidget::setEnabled);
    layout->addWidget(sz_box);

    // ── Nazewnictwo ───────────────────────────────────────────────────────────
    auto* nm_box = new QGroupBox("Nazewnictwo plików", inner);
    auto* nm_lay = new QFormLayout(nm_box);
    m_naming = new QComboBox(inner);
    m_naming->addItems({"Oryginalna nazwa", "Prefix + numer", "Tylko numer"});
    m_naming->setCurrentIndex(s.value("export/naming",0).toInt());

    auto* nm_opts = new QHBoxLayout();
    m_prefix = new QLineEdit(s.value("export/prefix","foto_").toString(), inner);
    m_prefix->setPlaceholderText("Prefix...");
    m_prefix->setMaximumWidth(120);
    m_start_nr = new QSpinBox(inner);
    m_start_nr->setRange(1, 99999);
    m_start_nr->setValue(s.value("export/start_nr",1).toInt());
    m_start_nr->setPrefix("od nr ");
    nm_opts->addWidget(new QLabel("Prefix:", inner));
    nm_opts->addWidget(m_prefix);
    nm_opts->addWidget(m_start_nr);
    nm_opts->addStretch();

    auto* nm_opts_w = new QWidget(inner);
    nm_opts_w->setLayout(nm_opts);
    nm_opts_w->setVisible(m_naming->currentIndex() != 0);

    nm_lay->addRow("Schemat:", m_naming);
    nm_lay->addRow(nm_opts_w);
    QObject::connect(m_naming, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, [nm_opts_w](int idx) { nm_opts_w->setVisible(idx != 0); });
    layout->addWidget(nm_box);

    // ── Watermark ─────────────────────────────────────────────────────────────
    auto* wm_box = new QGroupBox("Watermark tekstowy", inner);
    auto* wm_lay = new QFormLayout(wm_box);
    m_wm_chk  = new QCheckBox("Dodaj watermark", inner);
    m_wm_chk->setChecked(s.value("export/wm_enabled",false).toBool());

    auto* wm_opts = new QWidget(inner);
    auto* wm_opt_lay = new QFormLayout(wm_opts);
    wm_opt_lay->setContentsMargins(0,0,0,0);
    m_wm_text = new QLineEdit(s.value("export/wm_text","© ").toString(), inner);
    m_wm_pos  = new QComboBox(inner);
    m_wm_pos->addItems({"Poziomo", "Ukośnie (↗)", "Ukośnie (↘)"});
    m_wm_pos->setCurrentIndex(s.value("export/wm_pos",1).toInt());
    m_wm_size = new QSpinBox(inner);
    m_wm_size->setRange(8, 500); m_wm_size->setValue(s.value("export/wm_size",48).toInt());
    m_wm_size->setSuffix(" px");
    m_wm_opacity = new QSpinBox(inner);
    m_wm_opacity->setRange(10, 100);
    m_wm_opacity->setValue(s.value("export/wm_opacity",70).toInt());
    m_wm_opacity->setSuffix("%");

    auto* wm_size_row = new QHBoxLayout();
    wm_size_row->addWidget(m_wm_size);
    wm_size_row->addWidget(new QLabel("Krycie:", inner));
    wm_size_row->addWidget(m_wm_opacity);
    wm_size_row->addStretch();

    wm_opt_lay->addRow("Tekst:", m_wm_text);
    wm_opt_lay->addRow("Kierunek:", m_wm_pos);
    wm_opt_lay->addRow("Rozmiar:", wm_size_row);

    auto* wm_repeat_row = new QHBoxLayout();
    m_wm_count = new QSpinBox(inner);
    m_wm_count->setRange(1, 20);
    m_wm_count->setValue(s.value("export/wm_count",4).toInt());
    m_wm_count->setPrefix("W rzędzie: ");
    m_wm_spacing = new QSpinBox(inner);
    m_wm_spacing->setRange(50, 2000);
    m_wm_spacing->setValue(s.value("export/wm_spacing",300).toInt());
    m_wm_spacing->setSuffix(" px (odstęp rzędów)");
    wm_repeat_row->addWidget(m_wm_count);
    wm_repeat_row->addWidget(m_wm_spacing);
    wm_repeat_row->addStretch();
    wm_opt_lay->addRow("Powtarzanie:", wm_repeat_row);
    wm_opts->setEnabled(m_wm_chk->isChecked());

    wm_lay->addRow(m_wm_chk);
    wm_lay->addRow(wm_opts);
    QObject::connect(m_wm_chk, &QCheckBox::toggled, wm_opts, &QWidget::setEnabled);
    // Podgląd na żywo — każda zmiana wm triggeruje aktualizację
    auto refresh = [this]() { schedule_preview(); };
    QObject::connect(m_wm_chk,     &QCheckBox::toggled,   this, [this](bool){ schedule_preview(); });
    QObject::connect(m_wm_text,    &QLineEdit::textChanged, this, [this](const QString&){ schedule_preview(); });
    QObject::connect(m_wm_pos,     QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){ schedule_preview(); });
    QObject::connect(m_wm_size,    QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ schedule_preview(); });
    QObject::connect(m_wm_opacity, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ schedule_preview(); });
    QObject::connect(m_wm_count,   QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ schedule_preview(); });
    QObject::connect(m_wm_spacing, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ schedule_preview(); });
    (void)refresh;
    layout->addWidget(wm_box);

    // ── Metadane ──────────────────────────────────────────────────────────────
    auto* meta_box = new QGroupBox("Metadane", inner);
    auto* meta_lay = new QHBoxLayout(meta_box);
    m_keep_exif = new QCheckBox("Zachowaj EXIF", inner);
    m_keep_iptc = new QCheckBox("Zachowaj IPTC/Copyright", inner);
    m_keep_exif->setChecked(s.value("export/keep_exif",true).toBool());
    m_keep_iptc->setChecked(s.value("export/keep_iptc",true).toBool());
    meta_lay->addWidget(m_keep_exif);
    meta_lay->addWidget(m_keep_iptc);
    meta_lay->addStretch();
    layout->addWidget(meta_box);

    layout->addStretch();

    // Info o liczbie plików
    m_info_lbl = new QLabel(
        QString("<b>%1 plik(ów) do eksportu</b>").arg(paths.size()), left_widget);
    left_root->addWidget(m_info_lbl);

    // Przyciski
    auto* btns = new QDialogButtonBox(left_widget);
    auto* export_btn = btns->addButton("Eksportuj", QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    export_btn->setDefault(true);
    QObject::connect(btns, &QDialogButtonBox::accepted, this, &ExportDialog::start_export);
    QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    left_root->addWidget(btns);
}

void ExportDialog::schedule_preview() {
    if (m_preview_timer) m_preview_timer->start();
}

void ExportDialog::update_preview() {
    if (m_preview_src.isNull() || !m_preview_lbl) return;

    QImage preview = m_preview_src.copy();

    if (m_wm_chk && m_wm_chk->isChecked() && !m_wm_text->text().isEmpty()) {
        // Współczynnik skali: miniatura vs. oryginalny rozmiar
        // m_preview_src ma maks 760x560, oryginał może być 6000x4000
        // Musimy znać oryginalny rozmiar — odczytaj z QImageReader
        QSize orig_size;
        if (!m_paths.isEmpty()) {
            QImageReader r(m_paths.first());
            orig_size = r.size();
        }
        if (!orig_size.isValid() || orig_size.isEmpty())
            orig_size = preview.size();  // fallback

        // Skala miniatury względem oryginału
        double scale = (double)preview.width() / orig_size.width();

        // Skaluj wszystkie parametry px proporcjonalnie
        int scaled_font    = qMax(1, int(m_wm_size->value()    * scale));
        int scaled_spacing = qMax(1, int(m_wm_spacing->value() * scale));

        draw_watermark(preview,
                       m_wm_text->text(),
                       m_wm_pos->currentIndex(),
                       scaled_font,
                       m_wm_opacity->value(),
                       m_wm_count->value(),
                       scaled_spacing);
    }

    QPixmap pix = QPixmap::fromImage(preview).scaled(
        m_preview_lbl->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_preview_lbl->setPixmap(pix);
}

void ExportDialog::browse_output() {
    QString dir = QFileDialog::getExistingDirectory(this, "Folder docelowy",
                                                    m_output_dir->text());
    if (!dir.isEmpty()) m_output_dir->setText(dir);
}

// ── Właściwy eksport ─────────────────────────────────────────────────────────

static QSize calc_size(const QSize& orig, int mode, int val) {
    if (!orig.isValid() || orig.isEmpty()) return orig;
    switch (mode) {
        case 0: { // Szerokość
            double ratio = (double)val / orig.width();
            return QSize(val, qRound(orig.height() * ratio));
        }
        case 1: { // Wysokość
            double ratio = (double)val / orig.height();
            return QSize(qRound(orig.width() * ratio), val);
        }
        case 2:   // Procent
            return QSize(qRound(orig.width() * val / 100.0),
                         qRound(orig.height() * val / 100.0));
        case 3: { // Dłuższy bok
            int longer = qMax(orig.width(), orig.height());
            double ratio = (double)val / longer;
            return QSize(qRound(orig.width() * ratio),
                         qRound(orig.height() * ratio));
        }
        default: return orig;
    }
}

// Kierunek: 0=poziomo, 1=ukośnie↗ (+45°), 2=ukośnie↘ (-45°)
// count   = liczba napisów w jednym rzędzie (na szerokości obrazu)
// spacing = odstęp między rzędami (px, w układzie obrazu)
static void draw_watermark(QImage& img, const QString& text,
                            int direction, int font_size, int opacity,
                            int count, int spacing) {
    if (text.isEmpty() || count <= 0 || spacing <= 0) return;

    QPainter p(&img);
    QFont font = p.font();
    font.setPixelSize(font_size);
    font.setBold(true);
    p.setFont(font);

    int alpha_text   = int(255 * opacity / 100.0);
    int alpha_shadow = int(180 * opacity / 100.0);

    double angle = 0.0;
    if (direction == 1) angle = -45.0;
    else if (direction == 2) angle =  45.0;

    QFontMetrics fm(font);
    int tw = fm.horizontalAdvance(text);
    int th = fm.height();

    int W = img.width();
    int H = img.height();

    // Odstęp poziomy: count napisów na szerokości obrazu W
    // x_step = W / count  → dokładnie count napisów mieści się w szerokości
    int x_step = W / count;

    // Przekątna potrzebna by pokryć obraz po obrocie
    int diag = int(std::sqrt(double(W)*W + double(H)*H)) + spacing * 2;
    int row_count = diag / spacing + 2;

    p.save();
    p.translate(W / 2.0, H / 2.0);
    p.rotate(angle);

    // Startujemy od -diag/2 żeby pokryć całą przestrzeń po obrocie
    // ale x_step bazuje na W — więc ilość w rzędzie = ceil(diag / x_step)
    // Start X taki żeby rzędy były wycentrowane i pokrywały cały obszar
    int cols_needed = diag / x_step + 2;
    int x_start = -(cols_needed / 2) * x_step - x_step / 2;

    for (int row = -row_count / 2 - 1; row <= row_count / 2 + 1; ++row) {
        int y = row * spacing + th / 2;
        // Szachownica: co drugi rząd przesuń o pół x_step
        int x_shift = (((row % 2) + 2) % 2 == 1) ? x_step / 2 : 0;

        for (int col = 0; col < cols_needed; ++col) {
            int x = x_start + col * x_step + x_shift - tw / 2;
            p.setPen(QColor(0, 0, 0, alpha_shadow));
            p.drawText(x + 2, y + 2, text);
            p.setPen(QColor(255, 255, 255, alpha_text));
            p.drawText(x, y, text);
        }
    }
    p.restore();
}

static QString build_output_name(const QString& src_path, int naming_mode,
                                  const QString& prefix, int nr,
                                  const QString& ext) {
    switch (naming_mode) {
        case 0: return QFileInfo(src_path).completeBaseName() + "." + ext;
        case 1: return prefix + QString("%1").arg(nr, 4, 10, QChar('0')) + "." + ext;
        case 2: return QString("%1").arg(nr, 4, 10, QChar('0')) + "." + ext;
        default:return QFileInfo(src_path).completeBaseName() + "." + ext;
    }
}

void ExportDialog::start_export() {
    QString out_dir = m_output_dir->text().trimmed();
    if (out_dir.isEmpty()) {
        QMessageBox::warning(this, "Eksport", "Wybierz folder docelowy.");
        return;
    }
    QDir().mkpath(out_dir);

    // Zapisz ustawienia
    QSettings s("Lape","LapesEye");
    s.setValue("export/last_dir",   out_dir);
    s.setValue("export/format",     m_format->currentText());
    s.setValue("export/quality",    m_quality->value());
    s.setValue("export/resize",     m_resize_chk->isChecked());
    s.setValue("export/naming",     m_naming->currentIndex());
    s.setValue("export/prefix",     m_prefix->text());
    s.setValue("export/start_nr",   m_start_nr->value());
    s.setValue("export/wm_enabled", m_wm_chk->isChecked());
    s.setValue("export/wm_text",    m_wm_text->text());
    s.setValue("export/wm_pos",     m_wm_pos->currentIndex());
    s.setValue("export/wm_size",    m_wm_size->value());
    s.setValue("export/wm_opacity", m_wm_opacity->value());
    s.setValue("export/wm_count",   m_wm_count->value());
    s.setValue("export/wm_spacing", m_wm_spacing->value());
    s.setValue("export/keep_exif",  m_keep_exif->isChecked());
    s.setValue("export/keep_iptc",  m_keep_iptc->isChecked());

    // Parametry
    QString fmt = m_format->currentText().toLower();
    if (fmt == "jpeg") fmt = "jpg";
    int quality    = m_quality->value();
    bool do_resize = m_resize_chk->isChecked();
    int  rsz_mode  = -1;
    for (auto* rb : {m_by_width, m_by_height, m_by_percent, m_by_longer}) {
        if (rb->isChecked()) { rsz_mode++; break; }
        rsz_mode++;
    }
    int  rsz_val   = m_resize_val->value();
    int  naming    = m_naming->currentIndex();
    bool do_wm     = m_wm_chk->isChecked();

    auto* progress = new QProgressDialog("Eksportowanie...", "Anuluj",
                                          0, m_paths.size(), this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(500);

    int ok = 0, fail = 0, nr = m_start_nr->value();
    for (int i = 0; i < m_paths.size(); ++i) {
        if (progress->wasCanceled()) break;
        progress->setValue(i);
        progress->setLabelText(QString("Eksport %1 / %2:\n%3")
            .arg(i+1).arg(m_paths.size())
            .arg(QFileInfo(m_paths[i]).fileName()));
        QCoreApplication::processEvents();

        // Wczytaj obraz
        QImageReader reader(m_paths[i]);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (img.isNull()) { ++fail; continue; }

        // Zmiana rozmiaru
        if (do_resize) {
            QSize new_size = calc_size(img.size(), rsz_mode, rsz_val);
            if (new_size != img.size())
                img = img.scaled(new_size, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        }

        // Konwertuj do RGB32 dla watermark i zapisu
        if (fmt != "png" && img.hasAlphaChannel())
            img = img.convertToFormat(QImage::Format_RGB32);

        // Watermark
        if (do_wm)
            draw_watermark(img, m_wm_text->text(),
                           m_wm_pos->currentIndex(),
                           m_wm_size->value(),
                           m_wm_opacity->value(),
                           m_wm_count->value(),
                           m_wm_spacing->value());

        // Nazwa wyjściowa
        QString out_name = build_output_name(m_paths[i], naming,
                                              m_prefix->text(), nr, fmt);
        QString out_path = out_dir + "/" + out_name;
        // Unikalna nazwa gdy istnieje
        if (QFileInfo::exists(out_path)) {
            int dup = 2;
            QString base = QFileInfo(out_path).completeBaseName();
            do {
                out_path = out_dir + "/" + base
                           + QString("_%1.").arg(dup++) + fmt;
            } while (QFileInfo::exists(out_path));
        }

        // Zapis
        QImageWriter writer(out_path);
        writer.setFormat(fmt.toLatin1());
        if (fmt == "jpg" || fmt == "webp") writer.setQuality(quality);
        if (fmt == "tiff") writer.setCompression(1);  // LZW

        if (writer.write(img)) { ++ok; ++nr; }
        else ++fail;
    }

    progress->setValue(m_paths.size());
    progress->deleteLater();

    QString msg = QString("Eksport zakończony.\n\nZapisano: %1\nBłędy: %2")
                  .arg(ok).arg(fail);
    if (fail > 0)
        QMessageBox::warning(this, "Eksport", msg);
    else
        QMessageBox::information(this, "Eksport", msg);

    accept();
}

} // namespace LapesEye
