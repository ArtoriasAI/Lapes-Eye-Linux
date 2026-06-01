#pragma once
#include <QRubberBand>
#include <QWidget>
#include <QRect>

namespace LapesEye {

// Wrapper dla QRubberBand — wbudowany Qt widget do rubber-band selekcji.
// QRubberBand jest zarządzany przez Qt i nie ma problemów z WA_TranslucentBackground.
// Musi być dzieckiem viewport QScrollArea.
class RubberOverlay : public QWidget {
    Q_OBJECT
public:
    explicit RubberOverlay(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setVisible(false);
        // Utwórz QRubberBand jako dziecko tego samego parenta
        m_band = new QRubberBand(QRubberBand::Rectangle, parent);
        m_band->hide();
    }

    ~RubberOverlay() {
        // m_band jest dzieckiem parent, zostanie usunięty razem z nim
    }

    void show_rect(const QRect& r) {
        m_rect = r;
        if (r.isNull() || !r.isValid()) {
            m_band->hide();
            return;
        }
        m_band->setGeometry(r.normalized());
        m_band->show();
        m_band->raise();
    }

    void hide_rect() {
        m_rect = QRect();
        m_band->hide();
    }

    bool has_rect() const { return !m_rect.isNull(); }
    bool isVisible() const { return m_band->isVisible(); }
    void raise() { m_band->raise(); }
    void lower() { m_band->lower(); }
    void hide()  { m_band->hide(); setVisible(false); }

protected:
    void paintEvent(QPaintEvent*) override {}  // nic — rysuje QRubberBand

private:
    QRubberBand* m_band = nullptr;
    QRect m_rect;
};

} // namespace LapesEye
