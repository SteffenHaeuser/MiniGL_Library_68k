# MiniGL Classic 25.0 — RC9 SDK + FPU modefix

Oba warianty przebudowano od zera 14.09.2026 tym zestawem:

```sh
/opt/amiga-gcc16-rc9/bin/m68k-amigaos-gcc \
  -B/home/arczi/toolchains/amiga-gcc16-modefix/
```

To NIE samo RC9 i NIE Bernie. Sterownik, assembler, linker, nagłówki i runtime
pochodzą z RC9. Nakładka dostarcza oddzielne poprawione cc1/cc1plus.
Według README nakładki jej lokalna baza źródłowa ma HEAD 9529cbe26;
nie jest deklarowana jako dokładna kopia źródeł dystrybucji RC9.

Sprawdzono `-print-prog-name=cc1`: wskazuje katalog modefix.
SHA-256 faktycznie wybranego cc1:
`c1b42d6136108a5179c8e56c09a5549aa87b33a7fbdeac1bb5f79bb62df1fa55`.

## Pliki

- stock/minigl.library: standardowa V25 Classic.
- ortho/minigl.library: V25 Classic z naszą optymalizacją ortograficzną.

Flagi bez zmian: -m68060 -mhard-float -O3 -ffast-math
-fomit-frame-pointer -fno-strict-aliasing, bez profilowania.
Nazwa pliku i resident obu alternatyw: minigl.library, wersja 25.0.
Instalować tylko jeden wariant naraz, zachowując poprzednią bibliotekę
i restartując testową Amigę. Nie podmieniono LIBS: ani działającego FPSE.

## Weryfikacja

Dla obu wariantów przeszły w vamos/68040:
- test wektora Warp3D: rzeczywisty skok do base-30;
- 57600 porównań transformacji i kodów clippingu;
- test układu ABI 3/V25 (624 bajty, zachowane offsety V23).

Sprawdzono także gotowe pliki biblioteki: 151 niezerowych wskaźników API,
realne GLInterleavedArrays, trzy prawidłowe ujemne offsety CreateContext,
brak nierozwiązanych symboli. Testy nie zastępują testu grafiki w grze.

Reproducer FpuModeChangeRepro.cpp z Jazz2 skompilowano tą samą nakładką
przy 68060/O3/fast-math. Po wywołaniu change_animation zachowuje poprawne:

```asm
fmove.x (sp),fp0
ftst.x fp0
```

Nie występuje błędne zastąpienie odczytem pojedynczego słowa ze spill slotu.
To kontrola kodu reproduktora, nie ponowne wykonanie pełnego testu FPU w grze.

## Odtworzenie

Makefile_classic_verified.gcc domyślnie używa już RC9 + modefix i odmawia
budowania, gdy brakuje poprawionego cc1. Stare obiekty Bernie nie są używane.

```sh
make -f Makefile_classic_verified.gcc -j4 variants
```

Obiekty: obj_verified_rc9_modefix_060/{stock,ortho}.
Wyniki: bin/verified-rc9-modefix-060/{stock,ortho}.
Log: bin/verified-rc9-modefix-build.log.

SHA-256:

```text
stock e8244573425bde5be85800bc62a86fcdf0971bea0db3190c6dab950719bfa18e
ortho 343a8e908d440c7030b067e906078d734bf5ccd14d00daa6331040f8ad947c12
```

Zakres API i ograniczenia Classic pozostały takie jak w opisie zmian V25.
Sama zmiana kompilatora nie implementuje brakujących funkcji GL.
