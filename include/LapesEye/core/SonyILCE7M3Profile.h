#pragma once
#include <QImage>
// Wbudowany profil Adobe Standard dla Sony ILCE-7M3
// Dane wyekstrahowane z oficjalnego profilu Adobe (ProfileEmbedPolicy=0)

namespace LapesEye {

extern const float SONY_ILCE7M3_COLOR_MATRIX_D65[9];
extern const float SONY_ILCE7M3_FORWARD_MATRIX_D65[9];
extern const int   SONY_ILCE7M3_HSM_HUE_DIVS;
extern const int   SONY_ILCE7M3_HSM_SAT_DIVS;
extern const int   SONY_ILCE7M3_HSM_VAL_DIVS;
extern const float SONY_ILCE7M3_HSM_D65[8100];

// Aplikuje profil Adobe Standard na zdekodowanym obrazie RAW
QImage apply_sony_ilce7m3_profile(const QImage& src);

} // namespace LapesEye
