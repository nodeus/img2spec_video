# Image Spectrumizer 5.6 — Описание

GUI-утилита для конвертации изображений в формат ZX Spectrum и других ретро-платформ, с **обработкой видео**, **интерполяцией ключевых кадров**, **CLI-режимом пайпов** и **дизерингом на libdither** (19 ядер error diffusion, 43 ordered-матрицы, 10 режимов цветовой дистанции, моно-дизеринг).

Оригинальный проект: [Jari Komppa](https://github.com/jarikomppa/img2spec). Расширение (видео, ключевые кадры, CLI, экспорт): [nodeus](https://nodeus.ru).

### Скриншоты

| Главное окно | Ключевые кадры | Экспорт |
|-------------|---------------|---------|
| ![Главное](img/main-window.png) | ![Ключевые кадры](img/keyframes-ui.png) | ![Экспорт](img/export-window.png) |

---

## Возможности

### Конвертация изображений

Преобразование изображений в форматы ретро-платформ с интерактивным предпросмотром в реальном времени:

| Устройство | Разрешение | Описание |
|-----------|-----------|----------|
| **ZX Spectrum** | 256x192 | Стандартный экран (ячейки 8x8, 2 цвета на ячейку) |
| **ZX 3x64** | 256x192 | Три набора атрибутов, мерцание даёт ~192 цвета на CRT |
| **C64 HiRes** | 320x200 | Commodore 64, high-res режим |
| **C64 Multicolor** | 160x200 | Commodore 64, мультицвет (4 цвета на ячейку 8x8) |

### Модификаторы (15 штук, стековые, real-time)

ScalePos, Quantize, Ordered Dither, Error Diffusion Dither, Mono Dither, Edge, Blur, Min/Max, HSV, YIQ, RGB, Contrast, Curve, Noise, SuperBlack

### Дизеринг (libdither)

Дизеринг работает на вендорном [libdither](https://github.com/robertkist/libdither) (`src/libdither/`, см. `VENDOR.txt`):

- **Error Diffusion Dither** — 19 ядер: Floyd-Steinberg, Jarvis-Judice-Ninke, Stucki, Burkes, Sierra3, Sierra2, Sierra Lite, Diagonal, ShiauFan 1/2/3, Diffusion 1D/2D, Fake Floyd-Steinberg, Atkinson, Steve Pigeon, Robert Kist, Stevenson-Arce, Xot. Настройки: направление (4 режима, вкл. serpentine), джиттер (sigma + seed), цветовая дистанция
- **Ordered Dither** — 43 матрицы: Bayer 2x2–32x32, Blue Noise 128x128, Dispersed/Void dots, Non-Rectangular, Ulichney, Clustered Dot 1–11, Central/Balanced/Diagonal points, Magic Circle/45°/standard, Variable 2x2/4x4 (step), Interleaved Gradient. Настройки: сдвиги X/Y, джиттер, цветовая дистанция
- **Mono Dither** (новый) — 11 яркостных семейств: Threshold (+Auto), Grid, Pattern, Dot Diffusion, Dot Lippens, Variable Error Diffusion (Ostromoukhov/Zhou Fang), DBS, Kacker-Allebach, Riemersma (8 кривых), моно Error Diffusion, моно Ordered. Переключатель маски: Modulate (сохраняет оттенок) / Replace B/W, плюс Invert и linear-gamma luma
- **Цветовая дистанция** (10 режимов для цветных дизеров): Luminance, sRGB, Linear, HSV, LAB76, LAB94, LAB2000, sRGB CCIR, Linear CCIR, Tetrapal

### Форматы экспорта

PNG, бинарный SCR (`.scr`), C-заголовок (`.h`), ассемблер-include (`.inc`)

### Видео-режим

- Загрузка видеофайлов (MP4, MOV, AVI и др.) через ffmpeg
- Таймлайн с перемоткой по кадрам
- Воспроизведение/пауза, кнопки пропуска кадров
- Все модификаторы применяются к каждому кадру в реальном времени
- Экспорт из GUI на Windows: NVIDIA NVENC (HEVC), AMD AMF (HEVC) или x264 (H.264)
- Настройка качества (CRF/QP) и множителя масштаба (1x-32x)
- Аудио мультиплексируется из источника после экспорта

Экспорт из GUI, его прогресс и отмена пока реализованы только на Windows. На macOS используйте [пайп в Terminal](#macos): кнопка **Start export** в GUI не запускает экспорт.

### Ключевые кадры видео

- Сохранение полных снимков модификаторов + устройства на конкретных кадрах
- Автозахват: изменение параметров на текущем кадре автоматически создаёт/обновляет ключевой кадр
- Hold-семантика: настройки действуют от ключевого кадра до следующего
- **Интерполяция**: плавные переходы параметров между ключевыми кадрами (чекбокс + флаг `--interpolate`)
- Маркеры на таймлайне (красные ромбы)
- Навигация: `|< key`, `< key`, `> key`, `>| key`
- Хранение: `<video>.keyframes.json` рядом с видеофайлом

### CLI и режим пайпа

```
img2spec_video input.png workspace.isw -p output.png
```

| Флаг | Описание |
|------|----------|
| `-p <файл>` | Сохранить PNG |
| `-h <файл>` | Сохранить C-заголовок |
| `-i <файл>` | Сохранить ассемблер-include |
| `-s <файл>` | Сохранить SCR |
| `--pipe --width W --height H` | Чтение RAW RGB24 кадров из stdin и запись RGBA кадров в stdout |
| `--interpolate` | Включить интерполяцию ключевых кадров в режиме пайпа |
| `--keys <файл>` | Загрузить ключевые кадры для переключения параметров |

`-p` записывает PNG, а не видео. Флаги `--batch-stdin` и `--headless` не реализованы. Режим `--pipe` обрабатывает кадры без открытия GUI. Полный [пример для macOS](#macos) приведён ниже.

### Пайп экспорта видео

```
ffmpeg (декодирование) -> img2spec_video --pipe (обработка) -> ffmpeg (кодирование + масштабирование)
```

- Кадры передаются через анонимные пайпы (без записи на диск)
- img2spec_video обрабатывает кадры в разрешении устройства (256x192 для ZX Spectrum по умолчанию)
- ffmpeg масштабирует выход до `разрешение_устройства x множитель`
- GUI на Windows добавляет аудио после кодирования видео; пример для macOS добавляет его во время кодирования
- Для загрузки видео нужны ffmpeg и ffprobe в PATH; на Windows их также можно разместить в папке программы

---

## Сборка

### CMake (кроссплатформенная)

```bash
mkdir build && cd build
cmake ..
make
```

Зависимости: SDL2, OpenGL. На Linux: GTK3. На macOS: AppKit. Вендорные C-исходники libdither требуют C11 или новее (в CMakeLists задан `C_STANDARD 11`; легаси-тулсет v120 их не соберёт — используйте решение, сгенерированное CMake).

### macOS

При необходимости установите Xcode Command Line Tools (`xcode-select --install`). Если Homebrew уже установлен, выполните из корня репозитория:

```bash
brew install cmake sdl2 ffmpeg
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos -j 4
./build-macos/img2spec_video
```

Запускайте программу из Terminal, чтобы она получила PATH с ffmpeg и ffprobe из Homebrew. OpenGL и AppKit входят в macOS SDK. Каталог `build-macos/` исключён из Git.

Этот пример для Terminal изменяет размер входных кадров до 256x192 и частоту до 25 кадров/с, конвертирует их в стандартный ZX Spectrum и увеличивает результат до 512x384. Для кодирования используется CPU x264; первая аудиодорожка источника добавляется, если она есть. Выполните команды из корня репозитория в zsh или bash, заменив `input.mp4` на путь к видео:

```bash
set -o pipefail
input="input.mp4"

ffmpeg -nostdin -i "$input" -map 0:v:0 \
  -vf "fps=25,scale=256:192" -f rawvideo -pix_fmt rgb24 - |
./build-macos/img2spec_video --pipe --width 256 --height 192 |
ffmpeg -nostdin -f rawvideo -pix_fmt rgba \
  -s 256x192 -framerate 25 -i - -i "$input" \
  -map 0:v:0 -map '1:a:0?' \
  -vf "scale=512:384:flags=neighbor" \
  -c:v libx264 -crf 17 -pix_fmt yuv420p \
  -c:a aac -shortest output.mp4
```

Для сохранённых модификаторов добавьте `workspace.isw` перед `--pipe`. Значения `--width` и `--height` должны совпадать с размером кадров на выходе декодера, а `-s` у последнего ffmpeg — с разрешением устройства из workspace. Чтобы воспроизвести настройки для исходного видео, уберите `-vf "fps=25,scale=256:192"` у декодера, укажите исходные размеры декодированных кадров и задайте исходную частоту в `-framerate` (например, `24000/1001`). Это также сохраняет нумерацию кадров для `--keys "input.mp4.keyframes.json"`; для интерполяции добавьте `--interpolate`. Разрешение устройства должно оставаться постоянным на протяжении экспорта.

### Visual Studio

Открыть `img2spectrum.vcxproj`. Тулсет v120 (VS2013). Конфигурации Win32 и x64.

---

## Библиотеки и лицензии

| Библиотека | Лицензия | URL |
|-----------|---------|-----|
| **img2spec** | zlib/libpng | https://github.com/jarikomppa/img2spec |
| **SDL2** | zlib | https://www.libsdl.org/ |
| **Dear ImGui** | MIT | https://github.com/ocornut/imgui |
| **Parson** | MIT | https://github.com/kgabis/parson |
| **stb libraries** | Public Domain | https://github.com/nothings/stb |
| **libdither** | MIT | https://github.com/robertkist/libdither |
| **kdtree** (через libdither) | MIT-style, нужна атрибуция | https://github.com/jtsiomb/kdtree |
| **uthash** (через libdither) | BSD-style, нужна атрибуция | https://github.com/troydhanson/uthash |
| **tetrapal** (через libdither) | MIT | в составе libdither |
| **ffmpeg** | GPL/LGPL | https://ffmpeg.org/ |

---

## Ссылки

- Оригинальный проект: https://github.com/jarikomppa/img2spec
- Форк (видео + ключевые кадры + CLI): https://github.com/nodeus/img2spec_video
- Автор: https://nodeus.ru
