[README.md](https://github.com/user-attachments/files/28426955/README.md)
# Lape's Eye 
Jako że na linux nie działa zestaw Adobe zaczołem robić Lape(Linux advande photo editor)
Lapes Eye jest pierwszym etapem i konkurencją dla Adobe Bridge teraz skupiam się na poprawie wydajności i naprawie błędów. 
W nastepnym etapie będę robił Lape RAW editor.

**Profesjonalna przeglądarka zdjęć dla fotografów** 

![](1.png)

## Funkcje

- ⚡ **Wirtualna siatka** — płynna praca z folderami 1000+ zdjęć
- 🖼️ **Obsługa RAW** — podgląd ARW, CR2, NEF i innych formatów przez libraw
- 🔍 **Podgląd HD** — pełna rozdzielczość JPEG w docku i trybie pełnoekranowym
- 🏷️ **Oznaczenia** — gwiazdki i etykiety kolorów widoczne na miniaturach
- ✏️ **Zmiana nazw F2** — asynchroniczna, bez mrugania
- 📁 **Historia nawigacji** — Alt+← / Alt+→ per zakładka
- 🔗 **Zewnętrzny edytor** — skrót E (dowolny program), Shift+E (jako warstwa w PS)
- 📊 **Histogram** — RGB w prawym docku
- 🔎 **Wyszukiwanie** — po nazwie, ocenie, kolorze, dacie

## Wymagania systemowe

- Linux (EndeavourOS, Arch, Ubuntu 22.04+, Fedora 38+)
- Qt6 6.4+
- exiv2
- libraw

## Instalacja

### Ze źródeł

```bash
# Zależności (Arch/EndeavourOS)
sudo pacman -S qt6-base qt6-tools cmake ninja exiv2 libraw

# Kompilacja i uruchomienie
git clone https://github.com/YOUR_USERNAME/lapes-eye.git
cd lapes-eye
./run.sh
```

### AppImage (bez instalacji)

Pobierz najnowszy `LapesEye-x86_64.AppImage` z [Releases](../../releases).

```bash
chmod +x LapesEye-x86_64.AppImage
./LapesEye-x86_64.AppImage
```

## Skróty klawiszowe

| Skrót | Akcja |
|-------|-------|
| `E` | Otwórz w zewnętrznym edytorze |
| `Shift+E` | Otwórz jako warstwę w Photoshopie |
| `F2` | Zmień nazwę |
| `Alt+←` | Wstecz |
| `Alt+→` | Naprzód |
| `Spacja` | Podgląd pełnoekranowy |
| `1-5` | Ocena gwiazdkowa |
| `R` | Oznacz jako odrzucone |

## Licencja

Copyright © 2025 Tomasz Błędzki / Lape Photography Tools. All rights reserved.  
Szczegóły w pliku [LICENSE](LICENSE).
