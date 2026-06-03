#include "LapesEye/core/ColorManagement.h"
#include <QColorSpace>
#include <QSettings>
#include <QGuiApplication>
#include <QDebug>

// ─── Odczyt profilu ICC monitora ─────────────────────────────────────────────

#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
#  include <QGuiApplication>
#  include <QtGui/qguiapplication_platform.h>
#  include <xcb/xcb.h>
#  include <xcb/xproto.h>

static QByteArray read_icc_x11() {
    // Pobierz połączenie XCB przez Qt6 native interface
    auto* x11app = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11app) return {};
    xcb_connection_t* conn = x11app->connection();
    if (!conn) return {};

    // Intern atom _ICC_PROFILE
    const char* atom_name = "_ICC_PROFILE";
    xcb_intern_atom_cookie_t atom_cookie =
        xcb_intern_atom(conn, 0, strlen(atom_name), atom_name);
    xcb_intern_atom_reply_t* atom_reply =
        xcb_intern_atom_reply(conn, atom_cookie, nullptr);
    if (!atom_reply) return {};
    xcb_atom_t icc_atom = atom_reply->atom;
    free(atom_reply);
    if (icc_atom == XCB_ATOM_NONE) return {};

    // Pobierz root window pierwszego ekranu
    xcb_screen_t* screen =
        xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    if (!screen) return {};

    // Odczytaj właściwość _ICC_PROFILE z root window
    // Długość w jednostkach 4-bajtowych — max 1MB profilu
    static constexpr uint32_t MAX_LEN = 256 * 1024;  // 256k * 4 = 1MB
    xcb_get_property_cookie_t prop_cookie =
        xcb_get_property(conn, 0, screen->root, icc_atom,
                         XCB_GET_PROPERTY_TYPE_ANY, 0, MAX_LEN);
    xcb_get_property_reply_t* prop_reply =
        xcb_get_property_reply(conn, prop_cookie, nullptr);
    if (!prop_reply) return {};

    QByteArray result;
    int len = xcb_get_property_value_length(prop_reply);
    if (len > 0) {
        const char* data =
            reinterpret_cast<const char*>(xcb_get_property_value(prop_reply));
        result = QByteArray(data, len);
    }
    free(prop_reply);
    return result;
}

#elif defined(Q_OS_WIN)
#  include <windows.h>
#  include <icm.h>
#  include <QFile>

static QByteArray read_icc_windows() {
    // Pobierz ścieżkę do aktywnego profilu ICC monitora
    HDC hdc = GetDC(nullptr);  // DC całego ekranu (główny monitor)
    if (!hdc) return {};
    wchar_t path[MAX_PATH] = {};
    DWORD path_len = MAX_PATH;
    bool ok = GetICMProfileW(hdc, &path_len, path);
    ReleaseDC(nullptr, hdc);
    if (!ok) return {};
    // Wczytaj plik profilu
    QString profile_path = QString::fromWCharArray(path);
    QFile f(profile_path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

#endif

// ─── Publiczne API ────────────────────────────────────────────────────────────

QByteArray monitor_icc_profile() {
    // Cache statyczny — odczytujemy profil raz na start aplikacji
    // (zmiana monitora w trakcie działania programu to edge case)
    static QByteArray cached;
    static bool loaded = false;
    if (loaded) return cached;
    loaded = true;

#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    cached = read_icc_x11();
    if (!cached.isEmpty())
        qDebug() << "[ColorMgmt] Profil ICC monitora (X11):" << cached.size() << "bajtów";
    else
        qDebug() << "[ColorMgmt] Profil ICC monitora: brak (atom _ICC_PROFILE pusty) — fallback sRGB";
#elif defined(Q_OS_WIN)
    cached = read_icc_windows();
    if (!cached.isEmpty())
        qDebug() << "[ColorMgmt] Profil ICC monitora (Windows):" << cached.size() << "bajtów";
    else
        qDebug() << "[ColorMgmt] Profil ICC monitora: brak — fallback sRGB";
#else
    qDebug() << "[ColorMgmt] Profil ICC monitora: platforma nieobsługiwana — fallback sRGB";
#endif

    return cached;
}

QColorSpace monitor_color_space() {
    static QColorSpace cached;
    static bool loaded = false;
    if (loaded) return cached;
    loaded = true;

    QByteArray icc = monitor_icc_profile();
    if (!icc.isEmpty()) {
        cached = QColorSpace::fromIccProfile(icc);
        if (cached.isValid()) {
            qDebug() << "[ColorMgmt] QColorSpace monitora:" << cached.description();
            return cached;
        }
        qWarning() << "[ColorMgmt] Profil ICC monitora niepoprawny — fallback sRGB";
    }
    cached = QColorSpace(QColorSpace::SRgb);
    return cached;
}

// ─── Konwersja per-zdjęcie ────────────────────────────────────────────────────

QImage apply_color_mode(QImage img) {
    QSettings s("Lape", "LapesEye");
    int mode = s.value("color/mode", 1).toInt();
    if (mode == 0) return img;  // Brak konwersji

    // Profil źródłowy — osadzony w pliku lub sRGB jako default
    QColorSpace src = img.colorSpace();
    if (!src.isValid()) {
        src = QColorSpace(QColorSpace::SRgb);
        img.setColorSpace(src);
    }

    // Profil docelowy
    QColorSpace dst;
    if (mode == 2) {
        // Profil monitora — z cache
        dst = monitor_color_space();
    } else {
        // mode=1: sRGB (zalecane, domyślne)
        dst = QColorSpace(QColorSpace::SRgb);
    }

    if (!dst.isValid()) dst = QColorSpace(QColorSpace::SRgb);
    if (src == dst) return img;

    img.convertToColorSpace(dst);
    return img;
}
