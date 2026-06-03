# Lape's Eye — Roadmap

## ✅ v0.1 — MVP
## ✅ v0.2 — RAW + Cache
## ✅ v0.3 — Wyszukiwanie + Eksport + Drukowanie
## ✅ v0.4 — Wydajność i duże foldery
## ✅ v0.5 — Funkcje użytkowe

### Co zrobiono w v0.5
- Historia nawigacji per-karta (Alt+← / Alt+→)
- Zewnętrzny edytor — skrót E (dowolny program) i Shift+E (jako warstwa w PS przez IPC)
- Ustawienia edytora zewnętrznego (ścieżka, argumenty, tryb warstwy)
- Ikona aplikacji LE — wielorozdzielcza (16–512px), wbudowana w QRC
- Scroll przy strzałkach: minimalne przy małych kafelkach, centrowanie przy max zoom
- Etykiety kolorów folderów ładowane asynchronicznie (bez blokowania UI)
- run.sh: cmake --build zamiast make (działa ze spacjami i nawiasami w ścieżce)

## 📋 v0.6 — Integracja z Lape
- [ ] Batch wysyłanie do Lape (kolejka z paskiem postępu)
- [ ] Watcher systemu plików (inotify — auto-odświeżanie folderu)
- [ ] Podgląd edycji .leye w miniaturze (ramka koloru, gwiazdki widoczne na kafelku)
- [ ] Pełna synchronizacja IPC (status połączenia w pasku, kolejka)
- [ ] Obsługa SMB/NFS (montowanie dysków sieciowych)

## 📋 v0.7 — Lapes RAW Editor (osobny program, C++/Qt6)
- [ ] Podstawowy pipeline: ekspozycja, balans bieli, krzywe tonalne
- [ ] Demozaik przez libraw
- [ ] HSL, redukcja szumów, sharpen
- [ ] Zapis jako .lraw sidecar (niedestrukcyjny)
- [ ] IPC z Lapes Eye

## 📋 v0.8 — Scalenie Lapes RAW z Lapes Eye
- [ ] Embedded panel w prawym docku (jak Camera Raw w Bridge)
- [ ] Podgląd live w PreviewPanel podczas edycji RAW
- [ ] Kolejka batch RAW → eksport JPEG/TIFF

## 📋 v0.9 — GPU (Vulkan/QRhi)
- [ ] Vulkan rendering dla PreviewPanel i FullscreenViewer
- [ ] Płynny zoom Lanczos na GPU
- [ ] Lepsza jakość miniatur przez GPU downsampling

## 📋 Osobny chat — Port na Windows 11
- [ ] MSVC + vcpkg build
- [ ] Instalator .exe
- [ ] Testy parytetu funkcji z Linuxem
