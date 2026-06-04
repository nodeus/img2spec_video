# План действий №2: Pipe-режим — ноль дисковых операций для видео

## Анализ производительности текущего pipeline

### Текущий pipeline (3 прохода через диск)

```
Входное видео
    ↓
ffmpeg extract → temp/frames/*.png          ✗ запись на диск
    ↓
img2spec --batch-stdin → temp/processed/*.png  ✗ запись на диск
    ↓
ffmpeg encode → выходное видео
```

Для 15 000 кадров: **2× запись, 2× чтение** временных PNG. Это главный тормоз.

### Стоит ли объединять img2spec и video-spectrumizer?

**Нет.** У них разные ответственности:

| img2spec (C++) | video-spectrumizer (Go) |
|---|---|
| Попиксельная обработка изображений | Оркестровка ffmpeg-пайплайна |
| Модификаторы, устройства, конвертация форматов | Извлечение аудио, резка/склейка, кодирование |
| Один вход → один выход | Видео → кадры → видео |

Смешивать ffmpeg-оркестровку в C++ код — тащить libav* зависимости или `exec()` вызовы. Go справляется с этим на порядок элегантнее.

---

## Решение: pipe-режим (0 проходов через диск)

### Схема

```
ffmpeg -i input.mp4 -f rawvideo -pix_fmt rgb24 -
    │ stdout (RGB24 пиксели, 3 байта/пиксель)
    ▼
img2spec conv.isw --pipe --width 256 --height 192
    │ обрабатывает каждый кадр в памяти
    │ stdout (BGRA пиксели, 4 байта/пиксель)
    ▼
ffmpeg -f rawvideo -pix_fmt bgra -s 256x192 -i - ... output.mp4
```

**Дисковые операции:** только входное и выходное видео. Промежуточные кадры — ноль.

---

## Изменения в img2spec (`src/main.cpp`)

### 1. Глобальные флаги

```cpp
int gPipeWidth = 0;   // --width N (только с --pipe)
int gPipeHeight = 0;  // --height N
```

### 2. Флаг `--pipe` в pre-scan и batch-парсере

Добавить в pre-scan:

```cpp
if (strcmp(aParams[i], "--pipe") == 0)
{
    gHeadless = true;
    pipe_mode = 1;
}
```

Парсить `--width` и `--height` в основном цикле.

### 3. Режим `--pipe` после batch-парсера

Когда `pipe_mode`:

```cpp
if (pipe_mode)
{
    int frame_size = gPipeWidth * gPipeHeight;
    unsigned char *buf = new unsigned char[frame_size * 3]; // RGB24

    while (fread(buf, 1, frame_size * 3, stdin) == (size_t)(frame_size * 3))
    {
        // Упаковка RGB24 → gBitmapOrig (RGBA 0xAARRGGBB)
        for (int i = 0; i < frame_size; i++)
        {
            int r = buf[i * 3 + 0];
            int g = buf[i * 3 + 1];
            int b = buf[i * 3 + 2];
            gBitmapOrig[i] = 0xff000000 | (r << 16) | (g << 8) | b;
        }

        process_image();
        gDevice->filter();

        // Распаковка gBitmapSpec → stdout (BGRA для ffmpeg)
        for (int i = 0; i < frame_size; i++)
        {
            unsigned int c = gBitmapSpec[i];
            unsigned char rgba[4] = {
                (c >> 16) & 0xff,  // R
                (c >> 8) & 0xff,   // G
                c & 0xff,          // B
                0xff               // A
            };
            fwrite(rgba, 1, 4, stdout);
        }
        fflush(stdout);
    }

    delete[] buf;
    return 0;
}
```

**Примечание:** если `--width`/`--height` не указаны — использовать `gDevice->mXRes` и `gDevice->mYRes` (стандарт 256×192 или из workspace).

### 4. Вывод в правильном формате

ffmpeg ожидает `bgra` (blue, green, red, alpha). В памяти x86 little-endian `0xAABBGGRR` хранится как `R, G, B, A`. Для bgra нужно `B, G, R, A`. Выше используется RGB — можно выводить как `bgr0` или `rgba` с указанием правильного pix_fmt.

Упрощённо: выводим RGBA (4 байта: R, G, B, A), ffmpeg запускаем с `-pix_fmt rgba`.

---

## Калибровка: подбор параметров конвертации по нескольким кадрам видео

### Проблема

Пользователь не может подобрать настройки конвертера вслепую. Нужно увидеть,
как выглядит результат на реальных кадрах из видео, прежде чем запускать
полную конвертацию.

### Полный workflow с калибровкой

```
Шаг 1 — Распаковка аудио и ресайз видео (без изменений)
  ffmpeg -i input.mp4 → temp/sound.wav
  ffmpeg -i input.mp4 → temp/resized.mp4

Шаг 2 — Извлечение калибровочных кадров (NEW)
  ffprobe temp/resized.mp4 → получаем длительность video_duration
  Делим video_duration на N равных интервалов (N = 5–9 по умолчанию)
  ffmpeg извлекает по одному кадру из каждого интервала:
    temp/calibration/frame_000001.png  (0%)
    temp/calibration/frame_000002.png  (20%)
    temp/calibration/frame_000003.png  (40%)
    ...
  Выводит пользователю:
    "Извлечено N кадров для калибровки в temp/calibration/"
    "Открой любой кадр в img2spectrum.exe, настрой, сохрани workspace."
    "Настрой конвертацию и нажми Enter для продолжения..."

Шаг 3 — Настройка (пользователь работает в GUI)
  Пользователь открывает в img2spectrum.exe:
    img2spectrum.exe temp/calibration/frame_000003.png conv.isw
  Или переключается между разными кадрами для проверки.
  → Меняет модификаторы, устройство, опции
  → File → Save workspace → conv.isw (перезаписывает файл)
  → Закрывает img2spectrum.exe

Шаг 4 — Полная конвертация (pipe, 0 дисковых операций)
  Пользователь нажимает Enter в консоли video-spectrumizer.
  Тройной pipe:
    ffmpeg temp/resized.mp4 → rawvideo →
      img2spec conv.isw --pipe --width W --height H →
        rawvideo → ffmpeg → output.mp4
  Все настройки из conv.isw применяются к каждому кадру.
```

### Почему калибровочные кадры — а не один

- **Разные сцены** — то, что хорошо работает на тёмной сцене, может
  выглядеть плохо на светлой. Несколько кадров из разных таймкодов
  дают более полную картину.
- **Итеративная настройка** — пользователь может открыть один кадр,
  подкрутить, потом другой кадр, проверить, поправить ещё.

### Количество кадров

- По умолчанию: **7 кадров** (0%, 16%, 33%, 50%, 66%, 83%, 100%)
- Можно переопределить через новый флаг `-calibration-frames N`
- Для коротких видео (< 10 сек) — 3 кадра

---

## Изменения в video-spectrumizer (`main.go`)

### Новая структура main()

```
1. checkAudio → extractAudio
2. resizeVideo
3. extractCalibrationFrames  ← NEW
4. if pause → ждать Enter    ← изменено (ждёт после калибровки)
5. pipeVideo                  ← NEW (заменяет extractFrames + processFrames + encodeVideo)
6. if cleanup → удалить temp
```

### Новая функция: extractCalibrationFrames()

```go
func extractCalibrationFrames(video, calDir string, n int, config *Config) {
    createDir(calDir)

    // Получаем длительность видео
    dur := getVideoDuration(video) // через ffprobe

    // Равномерная сетка: n кадров от 0 до dur
    for i := 0; i < n; i++ {
        seek := float64(i) * dur / float64(n-1)
        out := filepath.Join(calDir, fmt.Sprintf("frame_%06d.png", i))

        exec.Command("ffmpeg",
            "-loglevel", "error",
            "-ss", fmt.Sprintf("%.3f", seek),
            "-i", video,
            "-vframes", "1",
            "-y", out).Run()
    }

    log.Printf("Извлечено %d кадров для калибровки: %s", n, calDir)
}
```

### Новая функция: pipeVideo()

Заменить весь блок extractFrames + processFrames + encodeVideo одним pipe:

```go
func pipeVideo(input, output string, config *Config) {
    w := strconv.Itoa(config.ResizeWidth)
    h := strconv.Itoa(config.ResizeHeight)

    extract := exec.Command("ffmpeg",
        "-loglevel", "error",
        "-i", input,
        "-f", "rawvideo",
        "-pix_fmt", "rgb24",
        "-")

    convert := exec.Command(config.ImgConverter,
        config.ConfigFile,
        "--pipe",
        "--width", w,
        "--height", h)

    encodeArgs := []string{
        "-loglevel", "error",
        "-y",
        "-f", "rawvideo",
        "-pix_fmt", "rgba",
        "-s", fmt.Sprintf("%dx%d", config.ResizeWidth, config.ResizeHeight),
        "-framerate", fmt.Sprintf("%.2f", config.Framerate),
        "-i", "-",
        "-vf", fmt.Sprintf("scale=iw*%d:ih*%d:flags=neighbor",
            config.ScaleFactor, config.ScaleFactor),
    }
    // + codec args (nvidia/amd/cpu) + output

    encode := exec.Command("ffmpeg", encodeArgs...)

    extract.Stdout = convert.Stdin
    convert.Stdout = encode.Stdin
    encode.Stderr = os.Stderr

    extract.Start()
    convert.Start()
    encode.Start()
    encode.Wait()
    convert.Wait()
    extract.Wait()
}
```

### Что уходит

- `extractFrames()` — больше не нужен
- `processFrames()` — заменён pipeVideo
- `--batch-stdin` в img2spec больше не используется video-spectrumizer
  (но остаётся в img2spec для пакетной обработки PNG из других сценариев)

### Что остаётся / изменяется

- `checkAudio()` + `extractAudio()` — без изменений
- `resizeVideo()` — без изменений
- `encodeVideo()` — заменён на encode внутри pipeVideo
- `createDir()` — без изменений
- Параметр `-pause` теперь переименовать в `-calibrate`
  (семантически точнее: пауза теперь не просто "жди",
  а "извлеки кадры и жди настройки")

---

```go
func pipeVideo(input, output string, config *Config) {
    w := strconv.Itoa(config.ResizeWidth)
    h := strconv.Itoa(config.ResizeHeight)

    extract := exec.Command("ffmpeg",
        "-loglevel", "error",
        "-i", input,
        "-f", "rawvideo",
        "-pix_fmt", "rgb24",
        "-")

    convert := exec.Command(config.ImgConverter,
        config.ConfigFile,
        "--pipe",
        "--width", w,
        "--height", h)

    encode := exec.Command("ffmpeg",
        "-loglevel", "error",
        "-y",
        "-f", "rawvideo",
        "-pix_fmt", "rgba",
        "-s", fmt.Sprintf("%dx%d", config.ResizeWidth, config.ResizeHeight),
        "-framerate", fmt.Sprintf("%.2f", config.Framerate),
        "-i", "-",
        "-vf", fmt.Sprintf("scale=iw*%d:ih*%d:flags=neighbor", config.ScaleFactor, config.ScaleFactor),
        ...codecArgs,
        output)

    // Соединяем pipe:
    extract.Stdout = convert.Stdin
    convert.Stdout = encode.Stdin
    encode.Stderr = os.Stderr

    // Запускаем
    if err := extract.Start(); err != nil { ... }
    if err := convert.Start(); err != nil { ... }
    if err := encode.Start(); err != nil { ... }

    // Ждём завершения
    encode.Wait()
    convert.Wait()
    extract.Wait()
}
```

### Что уходит

- `extractFrames()` — больше не нужен (кадры не пишутся на диск)
- `processFrames()` — заменён прямым pipe
- `--batch-stdin` в img2spec больше не нужен для видео (но остаётся для пакетной обработки PNG)

### Что остаётся

- `checkAudio()` — ffprobe
- `extractAudio()` — отдельный wav-файл для аудиодорожки
- `encodeVideo()` — заменён на encode в тройном pipe

---

## Сравнение

| Фактор | --batch-stdin (сейчас) | --pipe (предлагается) |
|---|---|---|
| Запись на диск | 2× (frames + processed) | 0 |
| Чтение с диска | 2× (frames + processed) | 0 |
| Временные файлы | Тысячи PNG | Нет |
| Потребление памяти | Низкое (1 кадр) | Низкое (1 кадр) |
| Сложность реализации | Сделано | ~50 строк C++, ~100 строк Go |
| Совместимость | --batch-stdin остаётся | Новый флаг, не ломает существующее |

---

## Порядок реализации

### img2spec
1. Добавить `--pipe`, `--width`, `--height` в pre-scan
2. Добавить pipe-цикл (read stdin → process → write stdout)
3. Закоммитить

### video-spectrumizer
4. Добавить `extractCalibrationFrames()` — извлечение N кадров по сетке
5. Добавить `pipeVideo()` — тройной pipe вместо извлечения/обработки/сборки
6. Заменить `-pause` на `-calibrate`, обновить Config
7. Удалить `extractFrames()`, `processFrames()`, `encodeVideo()`, параметр `-threads` (уже удалён)
8. Обновить main() под новый pipeline
9. Обновить readme, CHANGELOG, build.bat
10. Собрать и протестировать
