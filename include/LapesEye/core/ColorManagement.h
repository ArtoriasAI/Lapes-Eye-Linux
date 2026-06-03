#pragma once
// Zarządzanie kolorem — konwersja ICC profilu zdjęcia
// Użyj apply_color_mode() po każdym odczycie QImage z dysku.
#include <QImage>
#include <QByteArray>
#include <QColorSpace>

// Zwraca surowe bajty profilu ICC aktywnego monitora.
// Linux/X11: odczyt z atoma _ICC_PROFILE przez XCB.
// Windows:   odczyt przez GetICMProfileW().
// Fallback:  pusty QByteArray (apply_color_mode użyje sRGB).
// Wynik jest cache'owany — bezpieczne wywoływanie per-zdjęcie.
QByteArray monitor_icc_profile();

// Zwraca QColorSpace monitora (z cache). Jeśli profil niedostępny → sRGB.
QColorSpace monitor_color_space();

QImage apply_color_mode(QImage img);
