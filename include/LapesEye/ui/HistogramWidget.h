#pragma once
#include <QWidget>
#include <QImage>
#include <array>

namespace LapesEye {

class HistogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget* parent = nullptr);
    void compute(const QImage& img);
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    static constexpr int BINS = 256;
    std::array<int,BINS> m_r{}, m_g{}, m_b{}, m_luma{};
    int  m_max_val = 1;
    bool m_has_data = false;
};

} // namespace LapesEye
