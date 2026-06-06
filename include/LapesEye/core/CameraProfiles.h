#pragma once
#include <QString>

namespace LapesEye {

// Szuka profilu DCP/ICC dla aparatu w standardowych lokalizacjach:
// Adobe DNG Converter, darktable, RawTherapee, systemowe ICC
// Zwraca pełną ścieżkę lub pusty QString jeśli nie znaleziono
QString find_camera_profile(const QString& camera_make,
                             const QString& camera_model);

} // namespace LapesEye
