# План действий №3: Video Mode в img2spec — полный отказ от video-spectrumizer

## Цель

Перенести всю функциональность video-spectrumizer (Go) непосредственно в img2spec (C++).
После реализации img2spec сможет загружать видео, проматывать покадрово, подбирать
параметры конвертации и экспортировать готовое видео — без внешних инструментов
(кроме ffmpeg).

---

## Архитектура Video Mode

```
┌─────────────────────────────────────────────────────────────┐
│                     img2spec (SDL2 + ImGui)                 │
│                                                             │
│  ┌──────────────┐  ┌────────────┐  ┌────────────────────┐   │
│  │  Image Mode   │  │ Video Mode │  │   Options / Export │   │
│  │  (существует) │  │  (новое)   │  │   (новое)          │   │
│  └──────────────┘  └────────────┘  └────────────────────┘   │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  Original    │   Modified    │    Result             │    │
│  │  (кадр из    │   (после      │    (после Device)     │    │
│  │   видео)     │   модифик.)   │                       │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  [=====o===================================]  00:12 │    │
│  │  ◁◁  ▷||  ▷▷   Кадр 312 / 15000  25 FPS         │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  Export: [NVIDIA ■ H.265 ■ Quality 17 ■ Scale ×8] │    │
│  │  [██████████████████░░░░░░░░░░░░░] 47%             │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

---

## Изменения в `src/main.cpp`

### 1. Новый тип источника: `gSourceType`

```cpp
enum SourceType { SRC_NONE, SRC_IMAGE, SRC_VIDEO };
SourceType gSourceType = SRC_NONE;
```

Все проверки `if (gSourceImageData)` — работают как есть.
Дополнительные проверки `if (gSourceType == SRC_VIDEO)` — для видео-специфичных
функций (скраббер, таймлайн, экспорт).

### 2. Структура видео-данных

```cpp
struct VideoInfo {
    char filename[1024];
    double duration;        // секунды
    int totalFrames;
    double fps;
    int currentFrame;
    int width;              // после ресайза
    int height;

    // ffmpeg pipe handles
    FILE *ffmpegPipe;       // для извлечения кадра
    char pipeBuffer[1024];  // временный буфер

    // Кольцевой буфер кадров (для скраббинга)
    struct CachedFrame {
        int frameNum;
        unsigned int data[1024 * 512]; // gBitmapOrig
    };
    CachedFrame *frameCache;
    int cacheSize;
    int cacheCount;
};

VideoInfo gVideo;
```

### 3. Добавить #include для ffmpeg pipe

```cpp
#include <cstdio>
#include <cstdlib>
// ffmpeg запускается через popen()/_popen()
// popen уже доступен через <cstdio>
```

**На Windows: `_popen()` / `_pclose()`** вместо `popen()`/`pclose()`.

### 4. Новые глобальные флаги

```cpp
int gOptExportScale = 8;           // Scale ×N для выходного видео
int gOptExportEncoder = 0;         // 0=NVIDIA, 1=AMD, 2=CPU
int gOptExportQuality = 17;        // CRF/QP
char gOptExportFilename[1024] = ""; // путь выходного видео
int gVideoCurrentFrame = 0;
int gVideoTotalFrames = 0;
double gVideoDuration = 0;
double gVideoFps = 25.0;
bool gWindowExport = false;
bool gVideoExportActive = false;
float gVideoExportProgress = 0.0f;
```

### 5. Новая функция: `loadVideo()`

```cpp
void loadVideo(char *aFilename = nullptr)
{
    const char *FileName;
    // ... openDialog для .mp4, .avi, .mkv и т.д.

    if (!FileName) return;

    // Шаг 1: ffprobe — получить метаданные
    // Команда: ffprobe -v error -show_entries format=duration
    //          -show_entries stream=codec_type,r_frame_rate,width,height
    //          -of csv=p=0 input.mp4
    //
    // Парсинг stdout ffprobe:
    //   duration=123.456
    //   video,25/1,256,192
    //   audio,... (пропускаем)

    gVideoFps = ...;
    gVideoDuration = ...;
    gVideoTotalFrames = (int)(gVideoDuration * gVideoFps);
    gVideoCurrentFrame = 0;
    gSourceType = SRC_VIDEO;
    strcpy(gVideo.filename, FileName);

    // Шаг 2: ресайз видео (если нужно) или используем ffmpeg
    // на лету в getVideoFrame

    // Шаг 3: загрузить первый кадр
    getVideoFrame(0);
}
```

```cpp
void getVideoFrame(int frameNum)
{
    // Проверка кольцевого буфера
    for (int i = 0; i < gVideo.cacheCount; i++)
        if (gVideo.frameCache[i].frameNum == frameNum)
        {
            memcpy(gBitmapOrig, gVideo.frameCache[i].data,
                   gDevice->mXRes * gDevice->mYRes * 4);
            goto done;
        }

    // Извлечение кадра через ffmpeg pipe
    // ffmpeg -ss {time} -i input.mp4 -vframes 1
    //        -f rawvideo -pix_fmt rgb24 -v quiet - | ... read ...

    // Время для frameNum: time = frameNum / fps

    // Команда Windows:
    // sprintf(cmd, "ffmpeg -ss %.3f -i \"%s\" -vframes 1 "
    //         "-f rawvideo -pix_fmt rgb24 -v quiet -",
    //         time, gVideo.filename);
    // gVideo.ffmpegPipe = _popen(cmd, "rb");

    // Читаем RGB24 → gBitmapOrig (как в pipe-режиме PLAN_2)
    // _pclose

    // Сохраняем в кольцевой буфер (замещая самый старый)
    gVideo.frameCache[gVideo.cacheCount % gVideo.cacheSize] = {frameNum, ...};
    gVideo.cacheCount++;

    gVideoCurrentFrame = frameNum;
    gDirty = 1;
    gDirtyPic = 0;
}
```

### 6. Новый пункт меню: File → Load Video

```cpp
if (ImGui::BeginMenu("File"))
{
    if (ImGui::MenuItem("Load image")) { loadimg(); }
    if (ImGui::MenuItem("Load video")) { loadVideo(); }
    // ...
}
```

### 7. Новый блок UI: Video Timeline

Вставить в главное окно (после трёх картинок, перед кнопками Original/Modified/Result):

```cpp
if (gSourceType == SRC_VIDEO)
{
    ImGui::Separator();
    ImGui::Text("Видео: %s", gVideo.filename);

    // Таймлайн
    int frame = gVideoCurrentFrame;
    if (ImGui::SliderInt("##timeline", &frame, 0, gVideoTotalFrames - 1, "Кадр %d"))
        if (frame != gVideoCurrentFrame)
            getVideoFrame(frame);

    // Кнопки управления
    ImGui::SameLine();
    if (ImGui::Button("◀◀")) getVideoFrame(0);
    ImGui::SameLine();
    if (ImGui::Button("◀")) getVideoFrame(max(0, gVideoCurrentFrame - 1));
    ImGui::SameLine();
    if (ImGui::Button("▶|")) getVideoFrame(min(gVideoTotalFrames - 1, gVideoCurrentFrame + 1));
    ImGui::SameLine();
    if (ImGui::Button("▶▶")) getVideoFrame(gVideoTotalFrames - 1);
    ImGui::SameLine();

    // Текущая позиция
    double sec = (double)gVideoCurrentFrame / gVideoFps;
    int min = (int)(sec / 60);
    int s = (int)(sec) % 60;
    ImGui::Text("%02d:%02d  /  %02d:%02d  (%d FPS)",
        min, s,
        (int)(gVideoDuration / 60), (int)gVideoDuration % 60,
        (int)gVideoFps);

    // Панель экспорта
    ImGui::Separator();
    if (ImGui::Button("Export video..."))
        gWindowExport = !gWindowExport;
}
```

### 8. Новое окно: Export Settings

```cpp
if (gWindowExport && gSourceType == SRC_VIDEO)
{
    if (ImGui::Begin("Export video", &gWindowExport,
                     ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Output file", gOptExportFilename, 1024);

        ImGui::Combo("Encoder", &gOptExportEncoder,
                     "NVIDIA NVENC\0AMD AMF\0CPU x264\0");

        ImGui::SliderInt("Quality (CRF/QP)", &gOptExportQuality, 0, 51);
        ImGui::SliderInt("Scale ×N", &gOptExportScale, 1, 32);

        ImGui::Text("Output: %s", gOptExportFilename);
        ImGui::Text("Settings: %s | ×%d | Q%d",
            (const char*[]){"NVENC","AMF","x264"}[gOptExportEncoder],
            gOptExportScale, gOptExportQuality);

        if (gVideoExportActive)
        {
            ImGui::ProgressBar(gVideoExportProgress, ImVec2(-1, 0));
            if (ImGui::Button("Cancel"))
                gVideoExportActive = false;
        }
        else
        {
            if (ImGui::Button("Start export"))
                startVideoExport();
        }
    }
    ImGui::End();
}
```

### 9. Экспорт видео: `startVideoExport()`

```cpp
void startVideoExport()
{
    gVideoExportActive = true;
    gVideoExportProgress = 0.0f;

    // Формируем тройной pipe (см. PLAN_2.md):
    //
    // ffmpeg -loglevel error -i resized.mp4
    //   -f rawvideo -pix_fmt rgb24 - |
    //
    // img2spec conv.isw --pipe --width W --height H |
    //
    // ffmpeg -loglevel error -y
    //   -f rawvideo -pix_fmt rgba
    //   -s WxH
    //   -framerate FPS
    //   -i -
    //   -vf scale=iw*N:ih*N:flags=neighbor
    //   [codec args]
    //   output.mp4

    // Запускается в _popen / CreateProcess с redirect stdout/stdin
    // Поток читает прогресс из stderr ffmpeg (парсинг "time=")
    // Прогресс обновляется в gVideoExportProgress
    //
    // Т.к. img2spec однопоточный, экспорт идёт через фоновый процесс.
    // Главный цикл проверяет флаг gVideoExportActive и обновляет UI.
    //
    // Используем CreateProcess с redirect пайпов (Windows)
    // или popen (Linux/macOS)
}
```

**Важно:** экспорт не блокирует UI. Запускается через `CreateProcess` /
`_popen` с редиректом stdout. Главный цикл ImGui проверяет статус
процесса и обновляет прогресс-бар.

### 10. Аудиодорожка

В простейшем случае: после экспорта видео, отдельный ffmpeg-вызов для
добавления аудио из исходника:

```
ffmpeg -i output_noaudio.mp4 -i input.mp4 -c copy -map 0:v:0 -map 1:a:0 output.mp4
```

Это можно запустить как post-processing шаг после завершения экспорта видео.

---

## Структура кода: новые функции

| Функция | Где | Описание |
|---|---|---|
| `loadVideo()` | main.cpp | Загрузка видео, ffprobe, первый кадр |
| `getVideoFrame(n)` | main.cpp | Извлечение кадра через ffmpeg pipe + кэш |
| `VideoTimelineUI()` | main.cpp | Скраббер + кнопки управления |
| `ExportSettingsUI()` | main.cpp | Окно настроек экспорта |
| `startVideoExport()` | main.cpp | Запуск тройного pipe в фоне |
| `pollVideoExport()` | main.cpp | Проверка статуса (из главного цикла) |
| `abortVideoExport()` | main.cpp | Принудительная остановка экспорта |

Все функции размещаются в `src/main.cpp`. Код ffmpeg-пайпов изолирован
за `#ifdef _WIN32` / `#else`.

---

## Изменения в главном цикле

```cpp
// Вставляется в while (!done) после обработки dirty:

if (gVideoExportActive)
    pollVideoExport();

// И в render section — кнопка отмены поверх всего
// (если экспорт активен)
```

### pollVideoExport()

```cpp
void pollVideoExport()
{
    // Проверка: завершился ли процесс экспорта?
    // Если да — gVideoExportActive = false, показать сообщение
    // Если нет — обновить gVideoExportProgress из stderr ffmpeg

    // Парсинг строки: frame= 1234 fps=... time=00:01:23.45 ...
    // -> gVideoExportProgress = parsed_time / gVideoDuration
}
```

---

## Файлы ffmpeg: как обеспечить наличие

img2spec НЕ включает ffmpeg. Он ожидает его в PATH, как и
video-spectrumizer. При запуске экспорта — проверка:

```cpp
if (system("ffmpeg -version > nul 2>&1") != 0)
{
    // Показать ImGui::Text("FFmpeg not found in PATH");
    return;
}
```

---

## Что не меняется

- **Модификаторы** — полностью без изменений.
- **Device** — без изменений.
- **PNG-режим (Image Mode)** — без изменений.
- **GUI-окна** — Original / Modified / Result работают идентично.
  Original показывает текущий кадр видео, Modified — после модификаторов,
  Result — после Device.
- **Workspace (conv.isw)** — загружается и сохраняется как раньше.
  Video Mode использует те же настройки.

---

## Что меняется в `AGENTS.md`

Добавить:

```markdown
- **Video Mode** загружает видео через ffmpeg. Кадры извлекаются
  через `ffmpeg -ss time -i file -vframes 1 -f rawvideo -pix_fmt rgb24 -`.
- Экспорт видео — тройной pipe ffmpeg → img2spec --pipe → ffmpeg.
- ffmpeg должен быть в PATH.
```

---

## Оценка объёма работ

| Компонент | Строк C++ | Время |
|---|---|---|
| `loadVideo()` + ffprobe | ~60 | 1 час |
| `getVideoFrame()` + pipe + кэш | ~100 | 2 часа |
| Timeline UI + скраббер | ~80 | 1 час |
| Export Settings UI | ~150 | 1.5 часа |
| `startVideoExport()` + тройной pipe | ~120 | 2 часа |
| `pollVideoExport()` + progress | ~60 | 1 час |
| Audio mux post-processing | ~40 | 0.5 часа |
| Интеграция в main(), меню, проверки | ~80 | 1 час |
| **Итого** | **~690** | **~10 часов** |

---

## Порядок реализации

1. `loadVideo()` — загрузка видео, ffprobe, метаданные
2. `getVideoFrame()` — извлечение кадра, кольцевой буфер
3. Timeline UI — скраббер, кнопки, позиция
4. Интеграция в главное окно (меню File, отображение)
5. Export Settings UI — окно настроек
6. `startVideoExport()` — тройной pipe, фоновый процесс
7. `pollVideoExport()` — прогресс, отмена
8. Audio mux — post-processing аудиодорожки
9. Очистка, тестирование
