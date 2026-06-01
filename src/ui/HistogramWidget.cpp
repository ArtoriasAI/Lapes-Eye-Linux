#include "LapesEye/ui/HistogramWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace LapesEye {

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(160);
    setMaximumHeight(200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip("Histogram RGB");
}

void HistogramWidget::clear() {
    m_r.fill(0); m_g.fill(0); m_b.fill(0); m_luma.fill(0);
    m_max_val = 1;
    m_has_data = false;
    update();
}

void HistogramWidget::compute(const QImage& src) {
    m_r.fill(0); m_g.fill(0); m_b.fill(0); m_luma.fill(0);

    // Pracuj na mniejszej kopii dla wydajności (max 400px szerokości)
    QImage img = src.width() > 400
        ? src.scaledToWidth(400, Qt::FastTransformation)
        : src;
    img = img.convertToFormat(QImage::Format_RGB32);

    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb px = line[x];
            int r = qRed(px), g = qGreen(px), b = qBlue(px);
            m_r[r]++;
            m_g[g]++;
            m_b[b]++;
            // Luminancja BT.601
            int luma = qBound(0, (r*77 + g*150 + b*29) >> 8, 255);
            m_luma[luma]++;
        }
    }

    // Pomiń skrajne biny (0 i 255) przy wyznaczaniu max — nie zaburza skali
    m_max_val = 1;
    for (int i = 1; i < 255; ++i) {
        m_max_val = std::max({m_max_val, m_r[i], m_g[i], m_b[i]});
    }
    m_has_data = true;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Tło
    p.fillRect(rect(), QColor("#1a1a1a"));

    if (!m_has_data) {
        p.setPen(QColor("#444"));
        p.drawText(rect(), Qt::AlignCenter, "—");
        return;
    }

    const int W = width(), H = height() - 2;
    const double x_scale = (double)BINS / W;
    const double y_scale = (double)(H - 4) / m_max_val;

    // Rysuj każdy kanał jako wypełnioną krzywą z mieszaniem addytywnym
    struct Channel { const std::array<int,BINS>& data; QColor color; };
    Channel channels[] = {
        { m_luma, QColor(160, 160, 160, 60) },  // jasność — szara, pod spodem
        { m_r,    QColor(220,  60,  60, 80) },
        { m_g,    QColor( 60, 180,  60, 80) },
        { m_b,    QColor( 60, 100, 220, 80) },
    };

    p.setCompositionMode(QPainter::CompositionMode_Plus);

    for (auto& ch : channels) {
        QPainterPath path;
        path.moveTo(0, H);
        for (int px = 0; px < W; ++px) {
            int bin = qMin((int)(px * x_scale), 255);
            int val = (int)(ch.data[bin] * y_scale);
            path.lineTo(px, H - val);
        }
        path.lineTo(W, H);
        path.closeSubpath();
        p.fillPath(path, ch.color);
    }

    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Ramka
    p.setPen(QColor("#333"));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // Linie pomocnicze (co 64 wartości) — subtelne
    p.setPen(QColor(255,255,255, 20));
    for (int v : {64, 128, 192}) {
        int px = (int)(v / x_scale);
        p.drawLine(px, 0, px, H);
    }
}

} // namespace LapesEye
