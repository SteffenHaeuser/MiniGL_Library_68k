# MiniGL Classic V25 — GCC 16.2, 68060

**Opis historycznego builda Bernie.** Aktualny Makefile używa RC9 z oddzielną
nakładką FPU modefix; nowe pliki, ścieżki i hashe opisuje V25-RC9-MODEFIX.md.
Poniższe porównanie źródeł i ograniczenia API pozostają aktualne.

Zbudowano 14.09.2026 sprawdzonym toolchainem:
`/opt/amiga-gcc16-bernie/bin/m68k-amigaos-gcc`,
GCC 16.2.0b 20260825082934.

Flagi: `-m68060 -mhard-float -O3 -ffast-math -fomit-frame-pointer
-fno-strict-aliasing`, plus dotychczasowe ustawienia freestanding Classic.
Bez profilowania, bez obiektów GCC 6 i bez starych archiwów z paczki.
Standardowa nazwa pliku i resident: `minigl.library`, wersja **25.0**.
To backend Classic/Warp3D, NIE natywny PiStorm3D.

## Dwa osobne warianty

- `stock/minigl.library`: kod V25 bez naszej optymalizacji ortograficznej.
- `ortho/minigl.library`: V25 z przeniesioną z naszego V23 optymalizacją
  TransformIndex. To lokalny dodatek, NIE nowość autorów paczki V25.
  Wersja identyfikuje się dodatkowo jako `local ortho optimization`.

Oba warianty zachowują nazwę minigl.library i są alternatywami do testowania,
nie dwiema bibliotekami instalowanymi równocześnie. Do pierwszego porównania
samej aktualizacji V25 użyj stock; do zachowania wcześniejszego ortho — ortho.
Nie gwarantujemy wzrostu FPS z samej aktualizacji V25.

Przy podmianie w TESTOWEJ kopii środowiska zachowaj poprzedni plik i zrestartuj
Amigę, żeby nie korzystała z biblioteki pozostającej w pamięci. Nie zmieniono
obecnego LIBS:, ustawień FPSE ani działających plików V23.
FPSE gpuminigl-ortho.dll nadal otwiera prywatne minigl-ortho.library;
te standardowo nazwane buildy nie podmieniają go automatycznie.

## Co rzeczywiście zmieniono w paczce V25 względem naszej bazy V23.1

Porównano źródła obu lokalnych katalogów, a nie tylko opis wydania.

1. **Wersja resident i $VER: 25.0**, zamiast 23.1.
2. **Rozszerzenie tablicy dispatch o 15 wskaźników**: z 564 do 624 bajtów,
   151 funkcji zamiast 136. ABI pozostaje 3. Istniejące pola, w tym siedem
   końcowych funkcji opublikowanego V23, pozostają pod tymi samymi offsetami.
   Nowe pola dopisano na końcu, a nie w środku.
3. **GLInterleavedArrays podłączono do rzeczywistej implementacji Classic**.
   W naszej bibliotece V23 była tam atrapa. Implementacja w samym backendzie
   już istniała; nowością jest udostępnienie jej przez bibliotekę.
4. **Nowe sloty mają w Classic głównie atrapy zgodności**, nie implementacje
   możliwości graficznych. Dotyczy to m.in. GLLineWidth, GLTexGenfv,
   GLCopyTexImage2D, GLCopyTexSubImage2D oraz jedenastu kolejnych funkcji:
   GLAreTexturesResident, GLEdgeFlag, GLEdgeFlagPointer, GLEdgeFlagv,
   GLGetDoublev, GLGetPointerv, GLIndexi, GLIndexiv, GLIndexPointer,
   GLPrioritizeTextures i GLReadBuffer.
   Przykłady: GLGetPointerv zwraca NULL, GLGetDoublev zapisuje tylko pierwszy
   element jako zero, GLAreTexturesResident zwraca TRUE. Nie należy z tego
   wyciągać wniosku o pełnej obsłudze OpenGL 1.1 przez Classic.
5. **MGLResizeContext pozostało atrapą**, teraz jawnie lokalną. Brakujący
   backend jest nadal wyłączony przez #if 0. To NIE naprawa zmiany rozmiaru.
6. **Porządki kompilacyjne**: wspólną deklarację GfxBase dodano do sysinc.h;
   w others.c ujednolicono typy porównania W3D_UNSUPPORTEDBLEND i usunięto
   nieużywany wynik fwrite.
7. **Wcześniejsze poprawki Q3/ReadPixels/blending/texenv pozostają**.
   Kod rysowania i tekstur zasadniczo nie został przebudowany. Naszego
   TransformIndexOrtho nie było w dostarczonej paczce — dodano go lokalnie
   wyłącznie do osobnego wariantu ortho.

Wcześniejszy README zawiera też historyczne opisy wersji 14/22/23 i zmian
backendu natywnego. Nie są one dowodem nowych funkcji w tej bibliotece Classic.

## Ważna różnica nagłówków

Publiczne `dev/MiniGL_Library_Source_Code/include/mgl/` zostało odświeżone
do nowszego drzewa: dochodzą prototypy, stałe, poprawione nazwy i definicje
obsługi błędów. GL_TEXTURE0_ARB ma tam teraz wartość 0x84C0, a po tej grupie
przekotwiczono numerację MGL. Backend `mglQ3/include/` nadal ma historyczne
wartości Classic. Nie zmieniono jego kodu na uniwersalną obsługę tych wartości.

Dlatego build Classic używa **najpierw `mglQ3/include`**, potem publicznego
include biblioteki (tak samo robi Makefile Classic w paczce). Przy przebudowie
klientów dla Classic również trzeba zadbać o tę kolejność. Zgodność offsetów
tablicy ABI nie oznacza zgodności wszystkich enumów dowolnych nagłówków.
Publiczny config.h zmienia obsługę błędów, ale backend Classic korzysta
z własnego, niezmienionego config.h — nie przypisujemy mu tych nowych funkcji.

## Weryfikacja

Dla stock i ortho wykonano:

- test 68k rzeczywistego makra W3D_CreateContext: **base-30, PASS**;
- test 57600 transformacji/outcodes: PASS;
- test ABI 3, 624 bajty i zachowanie offsetów V23: PASS;
- odczyt GOTOWEJ tablicy z pliku: 151 niezerowych wskaźników, slot
  GLInterleavedArrays wskazuje prawdziwą funkcję backendu;
- sprawdzenie trzech miejsc tworzenia kontekstu w disassembly gotowego pliku:
  wszystkie używają adresu Warp3DBase-30, bez błędu GCC 6 z dodatnim offsetem;
- brak nierozwiązanych symboli i instrumentacji mcount/profilera.

Testy 68k uruchomiono w vamos/68040. Nie zastępują one testu obrazu ani FPS
w grze na WinUAE/68060. Nowe atrapy API nie stają się pełnymi implementacjami
przez sam fakt pomyślnej kompilacji i linkowania.

## Odtworzenie builda

W `dev/MiniGL_Library_Source_Code`:

```sh
make -f Makefile_classic_verified.gcc -j4 variants
```

Wyniki: `bin/verified-gcc16-060/{stock,ortho}/minigl.library`.
Oddzielne obiekty: `obj_verified_gcc16_060/{stock,ortho}`.
Testy są w tych samych katalogach wynikowych. Skrypt
`tests/verify_built_dispatch.py` sprawdza tablicę w linked binary oraz wektory
w disassembly; wymagane pliki pomocnicze opisuje jego nagłówek.

SHA-256 plików wydanych:

```text
stock a90ecc8194b265fc2a3ddbb2d521c4b206ae4fdd2ed7fb4746a2bd0234c48fdb
ortho ea02bb7e71676625061a1671e55f5e13c96759bb644ca23c9953a7f3786f0fd1
```
