# Lape's Eye v0.1 — Przeglądarka mediów dla Lape

**Lape's Eye** to odpowiednik Adobe Bridge stworzony specjalnie dla
[Lape — Linux Advanced Photo Editor](../Lape-v14-fix2/).
Napisany w C++20 + Qt6, docelowo dla EndeavourOS / Arch Linux.

---

## Zależności

```bash
sudo pacman -S --needed cmake qt6-base libraw exiv2 pkgconf base-devel
```

## Budowanie

```bash
chmod +x build.sh
./build.sh

# Uruchomienie
./build/lapes-eye
./build/lapes-eye ~/Zdjecia    # z folderem startowym
```

---

## Architektura

```
lapes-eye/
├── src/
│   ├── core/
│   │   ├── FileScanner   — skanowanie folderów, wykrywanie formatów
│   │   ├── ThumbCache    — SQLite cache miniatur (~/.cache/lapes-eye/)
│   │   ├── MetaStore     — EXIF (libexiv2) + własne .leye metadane
│   │   ├── Collection    — kolekcje (TODO v0.2)
│   │   └── LapeIPC       — IPC przez Unix socket → Lape
│   ├── ui/
│   │   ├── MainWindow    — główne okno, ciemny motyw (Fusion)
│   │   ├── FolderPanel   — drzewo folderów + ulubione (lewy dock)
│   │   ├── ThumbnailGrid — siatka miniatur z lazy loading (centrum)
│   │   ├── ThumbnailItem — jeden kafelek (custom QPainter)
│   │   ├── PreviewPanel  — duży podgląd (prawy dock góra)
│   │   ├── MetaPanel     — EXIF + edytowalne adnotacje (prawy dock dół)
│   │   ├── FilterBar     — filtrowanie po ocenie/etykiecie/formacie (dół)
│   │   └── BatchRenameDialog — zmiana nazw wsadowych z tokenami
│   └── workers/
│       └── ThumbWorker   — async generowanie miniatur (QThreadPool)
└── include/LapesEye/     — nagłówki

```

---

## Integracja z Lape — IPC

Lape's Eye komunikuje się z Lape przez **Unix domain socket**:

```
~/.config/lape/bridge/lape.sock
```

### Co musi dodać Lape (po stronie serwera):

```cpp
// W MainWindow::MainWindow() Lape — dodać serwer:
#include <QLocalServer>

m_bridge_server = new QLocalServer(this);
m_bridge_server->listen(QDir::homePath() + "/.config/lape/bridge/lape.sock");

connect(m_bridge_server, &QLocalServer::newConnection, this, [this]() {
    auto* conn = m_bridge_server->nextPendingConnection();
    connect(conn, &QLocalSocket::readyRead, this, [this, conn]() {
        auto data = conn->readAll();
        auto doc  = QJsonDocument::fromJson(data);
        QString cmd = doc["cmd"].toString();

        if (cmd == "open_file") {
            open_document(doc["path"].toString());
        } else if (cmd == "open_as_layer") {
            open_as_new_layer(doc["path"].toString());
        } else if (cmd == "batch_open") {
            for (auto p : doc["paths"].toArray())
                open_document(p.toString());
        }
    });
});
```

### Komendy wysyłane przez Lape's Eye:

| Komenda          | Akcja w Lape                              |
|------------------|-------------------------------------------|
| `open_file`      | Otwórz jako nowy dokument                 |
| `open_as_layer`  | Dodaj jako nową warstwę w bieżącym dok.   |
| `open_raw_acr`   | Otwórz RAW przez Camera Raw equivalent    |
| `batch_open`     | Otwórz wiele plików naraz                 |

---

## Format metadanych .leye

Każdy plik `foto.jpg` może mieć towarzyszący `foto.jpg.leye` (JSON):

```json
{
  "rating": 4,
  "color_label": "green",
  "flag": "pick",
  "keywords": ["pejzaż", "jesień", "Tatry"],
  "note": "Do portfolio",
  "lape_edits": {}
}
```

Metadane są **przenośne** — razem z plikiem zdjęcia (tak jak XMP sidecar).

---

## Skróty klawiszowe

| Skrót          | Akcja                              |
|----------------|------------------------------------|
| `Enter`        | Otwórz zaznaczone w Lape           |
| `Shift+Enter`  | Otwórz jako warstwę w Lape         |
| `1–5`          | Ustaw ocenę gwiazdkową             |
| `0`            | Usuń ocenę                         |
| `6–9`          | Etykieta koloru (cz/żó/zie/ni)     |
| `Ctrl+A`       | Zaznacz wszystko                   |
| `Escape`       | Odznacz wszystko                   |
| `Ctrl+O`       | Otwórz folder                      |
| `Ctrl+Shift+R` | Zmiana nazw wsadowych              |
| `Ctrl+Q`       | Wyjdź                              |

---

## Roadmap

### v0.2
- [ ] Kolekcje statyczne i smart (filtrowane automatycznie)
- [ ] Pełny podgląd (spacja → fullscreen)
- [ ] Histogram w PreviewPanel
- [ ] Skan rekurencyjny (podkatalogi)
- [ ] Tryb porównania 2/4 zdjęć obok siebie

### v0.3
- [ ] Synchronizacja ustawień Camera Raw (blok `lape_edits` w .leye)
- [ ] Kopiowanie kart SD z automatycznym backupem
- [ ] Eksport (Image Processor) z wywołaniem Lape w tle
- [ ] Galeria PDF / HTML (Output workspace)

### v1.0
- [ ] Słowa kluczowe hierarchiczne
- [ ] Wyszukiwanie pełnotekstowe (FTS5 w SQLite)
- [ ] Plugin system dla zewnętrznych edytorów
- [ ] GNOME / KDE integracja (portal plików)
