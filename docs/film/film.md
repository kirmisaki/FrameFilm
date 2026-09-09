# .film 文件格式详细规范

## 1. 概述

`.film` 文件是 FrameFilm 项目用于存储电子纸显示屏帧数据的专用二进制文件格式。该格式针对墨水屏（e-Paper）特性优化，文件头固定为 32 字节，通过 `Reserved[0]`（现定义为 `Format` 判别码）区分编码方式与版本。

当前协议包含两个版本：

- **v1（Format `0x00`）**：4bit 颜色编码，每字节存储 2 个像素，`ColorCount` 取值 2-6，`ColorTable` 自描述颜色映射。
- **v2（Format `0x01`-`0x03`）**：面向 48 色 spectra 与黑白快刷的扩展格式，包括 1bpp 黑白（MonoFast）、8bpp 索引 ColorQual（3 相刷新）与 ColorFast（2 相刷新），详见第 10 节。

## 2. 文件结构

```
+------------------+
|   文件头 (32B)   |
+------------------+
|   文件主体 (N B) |
+------------------+
```

## 3. 文件头详细定义

文件头占用固定的 **32 字节**，包含元数据信息：

| 偏移量  | 字段名          | 长度   | 类型      | 描述                    |
| ---- | ------------ | ---- | ------- | --------------------- |
| 0x00 | FileSize     | 4 B  | uint32  | 文件主体数据大小（不含文件头，单位：字节） |
| 0x04 | ScreenWidth  | 2 B  | uint16  | 屏幕宽度（像素），仅定义字段，暂不使用   |
| 0x06 | ScreenHeight | 2 B  | uint16  | 屏幕高度（像素），仅定义字段，暂不使用   |
| 0x08 | ColorCount   | 1 B  | uint8   | 屏幕颜色数量，v1 有效值范围：2-6      |
| 0x09 | Format       | 1 B  | uint8   | 格式判别码，区分 v1/v2 编码方式       |
| 0x0A | FrameCount   | 2 B  | uint16  | 帧数（多帧动画），0 或 1 表示单帧      |
| 0x0C | Reserved     | 4 B  | byte\[] | 保留字段，用于未来扩展           |
| 0x10 | ColorTable   | 16 B | byte\[] | 文件颜色表，定义 16 种颜色的编码映射  |

### 3.1 字段详细说明

#### 3.1.1 FileSize（文件大小）

- **偏移**：0x00
- **长度**：4 字节
- **类型**：无符号 32 位整数（小端序）
- **含义**：表示文件主体部分的字节数，不包含 32 字节的文件头
- **示例**：若文件主体包含 10000 字节，则该字段值为 `0x2710`（小端序存储为 `10 27 00 00`）

#### 3.1.2 ScreenWidth（屏幕宽度）

- **偏移**：0x04
- **长度**：2 字节
- **类型**：无符号 16 位整数（小端序）
- **含义**：定义显示屏的宽度（像素），仅作为元数据使用，当前实现中未实际使用
- **示例**：800 像素宽度表示为 `0x0320`（小端序存储为 `20 03`）

#### 3.1.3 ScreenHeight（屏幕高度）

- **偏移**：0x06
- **长度**：2 字节
- **类型**：无符号 16 位整数（小端序）
- **含义**：定义显示屏的高度（像素），仅作为元数据使用，当前实现中未实际使用
- **示例**：600 像素高度表示为 `0x0258`（小端序存储为 `58 02`）

#### 3.1.4 ColorCount（屏幕颜色数量）

- **偏移**：0x08
- **长度**：1 字节
- **类型**：无符号 8 位整数
- **取值范围**：v1 有效值为 2 - 6；v2 格式中此字段保留，按 `Format` 决定实际编码
- **含义**：表示 v1 显示屏支持的颜色数量
  - `2`：双色屏（如黑白屏）
  - `3`：三色屏（如黑白红）
  - `4`：四色屏
  - `5`：五色屏
  - `6`：六色屏

#### 3.1.5 Format（格式判别码）

- **偏移**：0x09
- **长度**：1 字节
- **类型**：无符号 8 位整数
- **含义**：区分文件的编码方式与版本，决定主体如何解析

| 值    | 名称      | 每像素位数 | 单帧主体大小 | 说明                       |
| ---- | ------- | ----- | ------ | ------------------------ |
| 0x00 | v1 4bpp  | 4 bit | W×H/2  | v1 兼容，`ColorCount` 2-6，用 `ColorTable` |
| 0x01 | MonoFast | 1 bit | W×H/8  | 黑白快刷（差分），详见第 10 节         |
| 0x02 | ColorQual | 8 bit | W×H   | 48 色高质量，3 相刷新，详见第 10 节      |
| 0x03 | ColorFast | 8 bit | W×H   | 48 色快刷，2 相刷新，详见第 10 节       |

> 未列出的值（`0x04`-`0xFF`）保留，读取方应视为未知格式并拒绝解析。

#### 3.1.6 FrameCount（帧数）

- **偏移**：0x0A
- **长度**：2 字节
- **类型**：无符号 16 位整数（小端序）
- **含义**：文件包含的帧数，用于多帧动画。`0` 或 `1` 表示单帧；大于 1 时主体按帧顺序连续拼接。
- **关系**：`FileSize` = 单帧主体大小 × 帧数（帧数 ≤ 1 时按 1 计算）。

#### 3.1.7 Reserved（保留字段）

- **偏移**：0x0C
- **长度**：4 字节
- **类型**：字节数组
- **填充值**：建议填充 `0x00`
- **含义**：保留用于未来功能扩展，当前版本不使用

#### 3.1.8 ColorTable（文件颜色表）

- **偏移**：0x10
- **长度**：16 字节
- **类型**：字节数组
- **含义**：将实际颜色值映射为 0-15 的颜色编码，仅 v1（Format `0x00`）使用；v2 的 8bpp 格式使用固件内置色板，本字段填充 `0x00`
- **映射规则**：
  - 每个字节表示一种颜色的编码（范围 0-15）
  - 字节位置对应颜色编码索引（0-15）
  - 字节值表示该编码对应的实际颜色
- **示例**：
  ```
  ColorTable[0] = 0x00  // 编码 0 -> 实际颜色 0x00（通常是白色或背景色）
  ColorTable[1] = 0x01  // 编码 1 -> 实际颜色 0x01
  ColorTable[2] = 0xFF  // 编码 2 -> 实际颜色 0xFF（黑色）
  ColorTable[3] = 0x03  // 编码 3 -> 实际颜色 0x03
  ...（其余字节根据实际颜色定义填充）
  ```

## 4. 文件主体详细定义（v1）

### 4.1 数据结构

以下描述仅适用于 `Format == 0x00` 的 v1 格式。文件主体是连续的 **颜色编码数据流**，总字节数由文件头的 `FileSize` 字段指定。v2 格式的主体结构见第 10 节。

### 4.2 颜色编码规则

- 每个像素使用 **4 bit** 表示颜色编码，范围 0-15
- 每个字节可存储 **2 个像素**的颜色编码
  - **高 4 位**：第一个像素的颜色编码
  - **低 4 位**：第二个像素的颜色编码

### 4.3 编码格式示意

```
字节结构：
+--------+--------+
| 高 4 位 | 低 4 位 |
+--------+--------+
  像素 1   像素 2

颜色编码范围：0x0 - 0xF（0-15）
```

### 4.4 数据排列

- 像素按从左到右、从上到下的顺序排列
- 每个字节包含两个相邻像素（像素 1 在左，像素 2 在右）
- 每一行的最后一个像素若位于字节低 4 位，需要用填充值补齐到下一个字节

### 4.5 示例

假设屏幕宽度为 10 像素，第一行像素的颜色编码序列为：

```
[3, 5, 7, 2, 9, 1, 4, 6, 8, 0]
```

编码过程：

```
像素 0-1: 3(0011), 5(0101) -> 字节 = 0x35
像素 2-3: 7(0111), 2(0010) -> 字节 = 0x72
像素 4-5: 9(1001), 1(0001) -> 字节 = 0x91
像素 6-7: 4(0100), 6(0110) -> 字节 = 0x46
像素 8-9: 8(1000), 0(0000) -> 字节 = 0x80
```

结果字节序列：`0x35 0x72 0x91 0x46 0x80`

## 5. 文件头示例（v1）

假设配置如下：

- 屏幕尺寸：800 x 600
- 颜色数量：4 色
- 颜色表定义如 3.1.8 所示

文件头字节序列（十六进制）：

```
Offset  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F
----------------------------------------------------------------------------------------------------------------
0x00    10 27 00 00 20 03 58 02 04 00 00 00 00 00 00 00 00 00 01 FF 03 02 00 00 00 00 00 00 00 00 00 00
```

字段解析：

| 字段           | 偏移   | 长度   | 值                      | 说明                |
| ------------ | ---- | ---- | ---------------------- | ----------------- |
| FileSize     | 0x00 | 4 B  | `0x00 0x27 0x10 0x00`  | 文件主体大小 = 10000 字节 |
| ScreenWidth  | 0x04 | 2 B  | `0x20 0x03`            | 屏幕宽度 = 800 像素     |
| ScreenHeight | 0x06 | 2 B  | `0x58 0x02`            | 屏幕高度 = 600 像素     |
| ColorCount   | 0x08 | 1 B  | `0x04`                 | 4 色屏              |
| Format       | 0x09 | 1 B  | `0x00`                 | v1 六色编码           |
| FrameCount   | 0x0A | 2 B  | `0x00 0x00`            | 单帧                |
| Reserved     | 0x0C | 4 B  | `00 00 00 00`          | 保留字段              |
| ColorTable   | 0x10 | 16 B | `00 01 FF 03 ...`      | 颜色编码映射表           |

## 6. 读取流程

1. 读取文件头偏移 0x00-0x03 的 4 字节，获取 `FileSize`
2. 读取文件头偏移 0x04-0x05 的 2 字节，获取 `ScreenWidth`
3. 读取文件头偏移 0x06-0x07 的 2 字节，获取 `ScreenHeight`
4. 读取文件头偏移 0x08 的 1 字节，获取 `ColorCount`
5. 读取文件头偏移 0x09 的 1 字节，获取 `Format`
6. 读取文件头偏移 0x0A-0x0B 的 2 字节，获取 `FrameCount`
7. 若 `Format == 0x00`，读取文件头偏移 0x10-0x1F 的 16 字节，获取 `ColorTable`
8. 根据 `FileSize` 读取文件主体数据
9. 按 `Format` 解析主体（详见第 4、10 节）：
   - `0x00`：每字节拆分为两个 4bit 颜色编码
   - `0x01`：每字节 8 个像素的 1bpp 位图
   - `0x02` / `0x03`：每字节一个 8bpp 颜色索引

## 7. 注意事项

1. **字节序**：所有多字节整数采用小端序（Little-Endian）存储
2. **对齐**：文件主体数据无需 4 字节对齐
3. **颜色编码**：v1 实际显示颜色需通过 `ColorTable` 转换；v2 的 8bpp 格式使用固件内置色板
4. **扩展性**：`Format` 字段是版本与编码方式的唯一判别依据，读取方必须先读取 `Format` 再决定解析逻辑；`0x0C` 起的 4 字节 `Reserved` 保留用于未来扩展
5. **向后兼容**：v1 文件 `Format == 0x00`，原 v1 读取器可照常工作；读取方对未知 `Format`（`0x04`-`0xFF`）应拒绝解析而非猜测
6. **校验**：建议在读取时验证 `FileSize` 与实际文件大小是否匹配

## 8. C 语言读取示例

### 8.1 文件头结构体定义

```c
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define FILM_HEADER_SIZE  32
#define FILM_COLOR_TABLE_SIZE 16

typedef struct {
    uint32_t fileSize;
    uint16_t screenWidth;
    uint16_t screenHeight;
    uint8_t  colorCount;
    uint8_t  reserved[7];
    uint8_t  colorTable[FILM_COLOR_TABLE_SIZE];
} __attribute__((packed)) FilmHeader;

typedef struct {
    FilmHeader header;
    uint8_t*   pixelData;
} FilmFile;
```

> 上例为 v1 视图（`reserved[7]` 未细分）。v2 读取时，`reserved[0]` 为 `Format`，`reserved[1..2]`（小端）为 `FrameCount`，`reserved[3..6]` 为保留。可扩展结构体：
>
> ```c
> typedef struct {
>     uint32_t fileSize;
>     uint16_t screenWidth;
>     uint16_t screenHeight;
>     uint8_t  colorCount;
>     uint8_t  format;        // 0x09
>     uint16_t frameCount;    // 0x0A（小端）
>     uint8_t  reserved[4];   // 0x0C
>     uint8_t  colorTable[FILM_COLOR_TABLE_SIZE];
> } __attribute__((packed)) FilmHeaderV2;
> ```

### 8.2 直接内存映射读取（仅适用小端序系统）

在 ESP32、STM32 等小端序 MCU 上，可直接通过指针转换读取：

```c
FilmFile* film_load(const char* filepath) {
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        return NULL;
    }

    FilmFile* film = (FilmFile*)malloc(sizeof(FilmFile));
    if (!film) {
        fclose(fp);
        return NULL;
    }

    uint8_t headerBuffer[FILM_HEADER_SIZE];
    if (fread(headerBuffer, 1, FILM_HEADER_SIZE, fp) != FILM_HEADER_SIZE) {
        free(film);
        fclose(fp);
        return NULL;
    }

    film->header.fileSize     = *(uint32_t*)&headerBuffer[0x00];
    film->header.screenWidth  = *(uint16_t*)&headerBuffer[0x04];
    film->header.screenHeight = *(uint16_t*)&headerBuffer[0x06];
    film->header.colorCount   = *(uint8_t*) &headerBuffer[0x08];
    memcpy(film->header.reserved, &headerBuffer[0x09], 7);
    memcpy(film->header.colorTable, &headerBuffer[0x10], FILM_COLOR_TABLE_SIZE);

    film->pixelData = (uint8_t*)malloc(film->header.fileSize);
    if (!film->pixelData) {
        free(film);
        fclose(fp);
        return NULL;
    }

    fread(film->pixelData, 1, film->header.fileSize, fp);
    fclose(fp);

    return film;
}
```

### 8.3 安全跨平台读取（推荐方式）

使用 `memcpy` 保证字节序正确：

```c
FilmFile* film_load_safe(const char* filepath) {
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        return NULL;
    }

    FilmFile* film = (FilmFile*)malloc(sizeof(FilmFile));
    if (!film) {
        fclose(fp);
        return NULL;
    }
    memset(film, 0, sizeof(FilmFile));

    uint8_t headerBuffer[FILM_HEADER_SIZE];
    if (fread(headerBuffer, 1, FILM_HEADER_SIZE, fp) != FILM_HEADER_SIZE) {
        free(film);
        fclose(fp);
        return NULL;
    }

    memcpy(&film->header.fileSize,     &headerBuffer[0x00], 4);
    memcpy(&film->header.screenWidth,  &headerBuffer[0x04], 2);
    memcpy(&film->header.screenHeight, &headerBuffer[0x06], 2);
    memcpy(&film->header.colorCount,   &headerBuffer[0x08], 1);
    memcpy(film->header.colorTable,    &headerBuffer[0x10], FILM_COLOR_TABLE_SIZE);

    film->pixelData = (uint8_t*)malloc(film->header.fileSize);
    if (!film->pixelData) {
        free(film);
        fclose(fp);
        return NULL;
    }

    fread(film->pixelData, 1, film->header.fileSize, fp);
    fclose(fp);

    return film;
}
```

### 8.4 手动字节序转换

手动拼接字节，兼容任意字节序：

```c
static inline uint32_t read_uint32_le(const uint8_t* buffer) {
    return buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
}

static inline uint16_t read_uint16_le(const uint8_t* buffer) {
    return buffer[0] | (buffer[1] << 8);
}

FilmFile* film_load_manual(const char* filepath) {
    FILE* fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    FilmFile* film = (FilmFile*)malloc(sizeof(FilmFile));
    if (!film) {
        fclose(fp);
        return NULL;
    }

    uint8_t headerBuffer[FILM_HEADER_SIZE];
    if (fread(headerBuffer, 1, FILM_HEADER_SIZE, fp) != FILM_HEADER_SIZE) {
        free(film);
        fclose(fp);
        return NULL;
    }

    film->header.fileSize     = read_uint32_le(&headerBuffer[0x00]);
    film->header.screenWidth  = read_uint16_le(&headerBuffer[0x04]);
    film->header.screenHeight = read_uint16_le(&headerBuffer[0x06]);
    film->header.colorCount   = headerBuffer[0x08];
    memcpy(film->header.colorTable, &headerBuffer[0x10], FILM_COLOR_TABLE_SIZE);

    film->pixelData = (uint8_t*)malloc(film->header.fileSize);
    fread(film->pixelData, 1, film->header.fileSize, fp);
    fclose(fp);

    return film;
}
```

### 8.5 像素数据解码

将 4bit 编码转换为实际像素颜色：

```c
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 600

typedef struct {
    uint8_t* frameBuffer;
    uint16_t width;
    uint16_t height;
    uint8_t  colorCount;
    uint8_t  colorTable[FILM_COLOR_TABLE_SIZE];
} FrameBuffer;

static inline uint8_t get_high_nibble(uint8_t byte) {
    return (byte >> 4) & 0x0F;
}

static inline uint8_t get_low_nibble(uint8_t byte) {
    return byte & 0x0F;
}

FrameBuffer* film_decode_pixels(FilmFile* film) {
    FrameBuffer* fb = (FrameBuffer*)malloc(sizeof(FrameBuffer));
    if (!fb) return NULL;

    fb->width = film->header.screenWidth;
    fb->height = film->header.screenHeight;
    fb->colorCount = film->header.colorCount;
    memcpy(fb->colorTable, film->header.colorTable, FILM_COLOR_TABLE_SIZE);

    fb->frameBuffer = (uint8_t*)malloc(fb->width * fb->height);
    if (!fb->frameBuffer) {
        free(fb);
        return NULL;
    }

    uint32_t pixelIndex = 0;
    uint32_t dataIndex = 0;
    uint32_t totalPixels = fb->width * fb->height;

    while (pixelIndex < totalPixels && dataIndex < film->header.fileSize) {
        uint8_t byte = film->pixelData[dataIndex++];

        fb->frameBuffer[pixelIndex++] = film->header.colorTable[get_high_nibble(byte)];
        if (pixelIndex >= totalPixels) break;

        fb->frameBuffer[pixelIndex++] = film->header.colorTable[get_low_nibble(byte)];
    }

    return fb;
}
```

### 8.6 按行获取像素数据

适用于墨水屏逐行刷新场景：

```c
void film_get_row_pixels(FilmFile* film, uint16_t rowIndex, uint8_t* rowBuffer, uint16_t bufferSize) {
    uint16_t width = film->header.screenWidth;
    if (rowIndex >= film->header.screenHeight || bufferSize < (width + 7) / 8) {
        return;
    }

    uint32_t startByte = rowIndex * ((width + 1) / 2);
    uint32_t byteIndex = 0;
    uint16_t pixelIndex = 0;

    while (pixelIndex < width && (startByte + byteIndex) < film->header.fileSize) {
        uint8_t byte = film->pixelData[startByte + byteIndex++];

        rowBuffer[pixelIndex++] = film->header.colorTable[get_high_nibble(byte)];
        if (pixelIndex >= width) break;

        rowBuffer[pixelIndex++] = film->header.colorTable[get_low_nibble(byte)];
    }
}
```

### 8.7 资源释放

```c
void film_free(FilmFile* film) {
    if (!film) return;
    if (film->pixelData) {
        free(film->pixelData);
    }
    free(film);
}

void framebuffer_free(FrameBuffer* fb) {
    if (!fb) return;
    if (fb->frameBuffer) {
        free(fb->frameBuffer);
    }
    free(fb);
}
```

### 8.8 完整使用示例

```c
int main(void) {
    FilmFile* film = film_load("example.film");
    if (!film) {
        printf("Failed to load film file\n");
        return -1;
    }

    printf("Film Info:\n");
    printf("  File Size: %u bytes\n", film->header.fileSize);
    printf("  Screen: %ux%u\n", film->header.screenWidth, film->header.screenHeight);
    printf("  Colors: %u\n", film->header.colorCount);
    printf("  Color Table: ");
    for (int i = 0; i < 16; i++) {
        printf("%02X ", film->header.colorTable[i]);
    }
    printf("\n");

    FrameBuffer* fb = film_decode_pixels(film);
    if (fb) {
        printf("Decoded %u pixels\n", fb->width * fb->height);
        framebuffer_free(fb);
    }

    film_free(film);
    return 0;
}
```

## 9. 版本历史

| 版本  | 日期         | 描述     |
| --- | ---------- | ------ |
| 1.0 | 2026-05-25 | 初始版本定义 |
| 2.0 | 2026-09-09 | 新增 v2：MonoFast / ColorQual / ColorFast 三种编码格式 |

## 10. v2 格式规范

v2 复用 32 字节文件头，通过 `Format`（偏移 0x09）区分编码方式。设计目标是把固件中已实现的三种 spectra 显示能力（`hal_epd_display_mono` / `hal_epd_display_pic48`）封装为可直接存储、传输的 `.film` 文件。

### 10.1 格式总览

| Format | 名称 | 每像素位数 | 单帧主体大小 | 刷新相数 | 颜色来源 |
| --- | --- | ---: | ---: | ---: | --- |
| `0x01` | MonoFast | 1 bit | W×H/8 | 1（差分） | 黑白 |
| `0x02` | ColorQual | 8 bit | W×H | 3 | 固件内置 `rgb_qual` 色板 |
| `0x03` | ColorFast | 8 bit | W×H | 2 | 固件内置 `rgb_fast` 色板 |

三种 v2 格式均面向 E6 3.70"（720×480，JD7601）48 色 spectra 面板。像素排列与 v1 一致：从左到右、从上到下；多字节整数均小端序。

### 10.2 与 v1 的差异

- v2 主体不再是 4bpp 颜色编码，而是直接对应驱动输入缓冲（1bpp 位图或 8bpp 索引）。
- v2 的 `ColorTable`（偏移 0x10）不使用，填充 `0x00`；颜色映射由固件内置色板决定。
- v2 的 `ColorCount`（偏移 0x08）不使用，可填 `0x00`。
- `FrameCount`（偏移 0x0A）用于多帧动画，`0` 或 `1` 表示单帧。

### 10.3 MonoFast（Format `0x01`）

黑白快刷格式，1 个像素占 1 bit。

- **位序**：每字节存储 8 个像素，**MSB 在前**——行内最左像素对应字节的 bit 7，最右像素对应 bit 0。
- **取值**：`1` 为黑，`0` 为白。
- **主体大小**：`W×H/8` 字节（要求 W 为 8 的倍数）。
- **刷新原理**：驱动维护上一帧位图缓存 `m_mono_prev`，对每个像素按新旧值编码 2bit 跳变码：

  ```
  transition = 2 * oldBit + newBit
  ```

  再配合 `mono_fast` 波形，只驱动发生变化的像素，实现快速、无闪烁的差分刷新。端序、位序与上一帧状态必须一致，否则会出现花屏或伪影。

示例：720×480 的黑白帧主体为 `720×480/8 = 43200` 字节。

### 10.4 ColorQual（Format `0x02`）

48 色高质量格式，1 个像素占 1 字节的 8bpp 颜色索引。

- **索引范围**：有效索引为 `0`-`63`；索引 `64`-`255` 为固件色板零初始化尾部（`#000000`），不应作为颜色使用。
- **色板**：使用固件内置 `rgb_qual`（ColorQual V3）色板。有效候选色 46 项，索引分布不连续（完整对照见 10.6）；索引 `0`-`2` 均为白色 `#FFFFFF`，用作背景/白底但不计入候选色数。
- **主体大小**：`W×H` 字节，每字节对应一个像素。
- **刷新**：ColorQual 采用 3 相刷新（qual_phase1 → qual_phase2 → qual_phase3）。

驱动内部将相邻两个 8bpp 索引拆成高、低两个 3bit 平面（`index >> 3` 与 `index & 7`），每个平面按 4bpp 打包成 `W×H/2` 字节后再发送：

```
high = ((index0 >> 3) << 4) | (index1 >> 3);
low  = ((index0 & 7) << 4) | (index1 & 7);
```

文件主体存储的是原始 8bpp 索引（每像素 1 字节），平面拆分由驱动完成，无需文件层预打包。

### 10.5 ColorFast（Format `0x03`）

48 色快刷格式，编码方式与 ColorQual 相同，差异在于色板与刷新相数。

- **索引范围**：有效索引 `0`-`63`，`64`-`255` 为黑色占位。
- **色板**：使用固件内置 `rgb_fast` 色板。有效候选色 55 项，索引分布不连续（完整对照见 10.6）；索引 `0`-`2` 均为白色 `#FFFFFF`，用作背景/白底但不计入候选色数。
- **主体大小**：`W×H` 字节。
- **刷新**：ColorFast 采用 2 相刷新（fast_color_phase1 → fast_color_phase2），速度更快但色准略低于 ColorQual。

### 10.6 色板参考

48 色 spectra 采用 8bpp 索引，实际 RGB 由固件内置色板决定。下表为 256 项色板中索引 `0`-`63` 的完整对照（字节序 **R, G, B**，8bit/通道）；索引 `64`-`255` 全为 `#000000`（零初始化尾部，非有效色）。

| 色板 | 有效候选色数 |
| --- | ---: |
| `rgb_fast`（ColorFast，Format `0x03`） | 55 |
| `rgb_qual`（ColorQual，Format `0x02`） | 46 |

#### 10.6.1 rgb_fast（ColorFast）

| index | R | G | B | HEX | 有效 |
| ---: | ---: | ---: | ---: | --- | --- |
| 0 | 255 | 255 | 255 | `#FFFFFF` |  |
| 1 | 255 | 255 | 255 | `#FFFFFF` |  |
| 2 | 255 | 255 | 255 | `#FFFFFF` |  |
| 3 | 30 | 25 | 47 | `#1E192F` | Y |
| 4 | 62 | 37 | 56 | `#3E2538` | Y |
| 5 | 108 | 62 | 72 | `#6C3E48` | Y |
| 6 | 117 | 60 | 49 | `#753C31` | Y |
| 7 | 172 | 97 | 58 | `#AC613A` | Y |
| 8 | 41 | 41 | 79 | `#29294F` | Y |
| 9 | 58 | 62 | 91 | `#3A3E5B` | Y |
| 10 | 69 | 70 | 100 | `#454664` | Y |
| 11 | 78 | 78 | 102 | `#4E4E66` | Y |
| 12 | 121 | 114 | 122 | `#79727A` | Y |
| 13 | 150 | 131 | 127 | `#96837F` | Y |
| 14 | 171 | 145 | 120 | `#AB9178` | Y |
| 15 | 198 | 170 | 130 | `#C6AA82` | Y |
| 16 | 45 | 56 | 112 | `#2D3870` | Y |
| 17 | 110 | 114 | 139 | `#6E728B` | Y |
| 18 | 163 | 164 | 169 | `#A3A4A9` | Y |
| 19 | 197 | 197 | 187 | `#C5C5BB` | Y |
| 20 | 255 | 255 | 255 | `#FFFFFF` |  |
| 21 | 201 | 201 | 191 | `#C9C9BF` | Y |
| 22 | 255 | 255 | 255 | `#FFFFFF` |  |
| 23 | 255 | 255 | 255 | `#FFFFFF` |  |
| 24 | 34 | 34 | 84 | `#222254` | Y |
| 25 | 37 | 37 | 87 | `#252557` | Y |
| 26 | 38 | 38 | 92 | `#26265C` | Y |
| 27 | 40 | 39 | 96 | `#282760` | Y |
| 28 | 66 | 63 | 110 | `#423F6E` | Y |
| 29 | 107 | 86 | 117 | `#6B5675` | Y |
| 30 | 133 | 108 | 112 | `#856C70` | Y |
| 31 | 182 | 146 | 132 | `#B69284` | Y |
| 32 | 21 | 60 | 135 | `#153C87` | Y |
| 33 | 32 | 84 | 144 | `#205490` | Y |
| 34 | 38 | 90 | 147 | `#265A93` | Y |
| 35 | 42 | 95 | 149 | `#2A5F95` | Y |
| 36 | 70 | 124 | 158 | `#467C9E` | Y |
| 37 | 105 | 147 | 163 | `#6993A3` | Y |
| 38 | 129 | 162 | 153 | `#81A299` | Y |
| 39 | 173 | 184 | 152 | `#ADB898` | Y |
| 40 | 63 | 24 | 29 | `#3F181D` | Y |
| 41 | 90 | 24 | 28 | `#5A181C` | Y |
| 42 | 94 | 23 | 27 | `#5E171B` | Y |
| 43 | 97 | 25 | 29 | `#61191D` | Y |
| 44 | 128 | 30 | 31 | `#801E1F` | Y |
| 45 | 149 | 37 | 35 | `#952523` | Y |
| 46 | 157 | 45 | 43 | `#9D2D2B` | Y |
| 47 | 180 | 71 | 32 | `#B44720` | Y |
| 48 | 38 | 55 | 83 | `#263753` | Y |
| 49 | 65 | 101 | 101 | `#416565` | Y |
| 50 | 81 | 117 | 105 | `#517569` | Y |
| 51 | 94 | 127 | 106 | `#5E7F6A` | Y |
| 52 | 123 | 149 | 112 | `#7B9570` | Y |
| 53 | 145 | 162 | 110 | `#91A26E` | Y |
| 54 | 169 | 170 | 100 | `#A9AA64` | Y |
| 55 | 194 | 187 | 96 | `#C2BB60` | Y |
| 56 | 62 | 55 | 73 | `#3E3749` | Y |
| 57 | 145 | 132 | 77 | `#91844D` | Y |
| 58 | 187 | 167 | 70 | `#BBA746` | Y |
| 59 | 211 | 185 | 62 | `#D3B93E` | Y |
| 60 | 255 | 255 | 255 | `#FFFFFF` |  |
| 61 | 255 | 255 | 255 | `#FFFFFF` |  |
| 62 | 255 | 255 | 255 | `#FFFFFF` |  |
| 63 | 219 | 198 | 57 | `#DBC639` | Y |

#### 10.6.2 rgb_qual（ColorQual）

| index | R | G | B | HEX | 有效 |
| ---: | ---: | ---: | ---: | --- | --- |
| 0 | 255 | 255 | 255 | `#FFFFFF` |  |
| 1 | 255 | 255 | 255 | `#FFFFFF` |  |
| 2 | 255 | 255 | 255 | `#FFFFFF` |  |
| 3 | 31 | 25 | 43 | `#1F192B` | Y |
| 4 | 46 | 33 | 55 | `#2E2137` | Y |
| 5 | 64 | 46 | 66 | `#402E42` | Y |
| 6 | 84 | 51 | 55 | `#543337` | Y |
| 7 | 163 | 91 | 58 | `#A35B3A` | Y |
| 8 | 46 | 97 | 188 | `#2E61BC` | Y |
| 9 | 131 | 170 | 206 | `#83AACE` | Y |
| 10 | 191 | 211 | 221 | `#BFD3DD` | Y |
| 11 | 215 | 227 | 227 | `#D7E3E3` | Y |
| 12 | 255 | 255 | 255 | `#FFFFFF` |  |
| 13 | 255 | 255 | 255 | `#FFFFFF` |  |
| 14 | 255 | 255 | 255 | `#FFFFFF` |  |
| 15 | 255 | 255 | 255 | `#FFFFFF` |  |
| 16 | 27 | 52 | 146 | `#1B3492` |  |
| 17 | 25 | 69 | 170 | `#1945AA` |  |
| 18 | 27 | 73 | 178 | `#1B49B2` |  |
| 19 | 30 | 81 | 182 | `#1E51B6` |  |
| 20 | 66 | 118 | 191 | `#4276BF` |  |
| 21 | 105 | 146 | 196 | `#6992C4` |  |
| 22 | 133 | 166 | 190 | `#85A6BE` |  |
| 23 | 181 | 196 | 193 | `#B5C4C1` |  |
| 24 | 24 | 69 | 145 | `#184591` | Y |
| 25 | 34 | 91 | 151 | `#225B97` | Y |
| 26 | 39 | 97 | 152 | `#276198` | Y |
| 27 | 45 | 106 | 155 | `#2D6A9B` | Y |
| 28 | 72 | 131 | 163 | `#4883A3` | Y |
| 29 | 100 | 151 | 166 | `#6497A6` | Y |
| 30 | 128 | 169 | 152 | `#80A998` | Y |
| 31 | 170 | 191 | 143 | `#AABF8F` | Y |
| 32 | 103 | 25 | 30 | `#67191E` | Y |
| 33 | 130 | 19 | 27 | `#82131B` | Y |
| 34 | 133 | 19 | 27 | `#85131B` | Y |
| 35 | 136 | 21 | 30 | `#88151E` | Y |
| 36 | 157 | 21 | 28 | `#9D151C` | Y |
| 37 | 170 | 24 | 33 | `#AA1821` | Y |
| 38 | 175 | 24 | 30 | `#AF181E` | Y |
| 39 | 196 | 46 | 33 | `#C42E21` | Y |
| 40 | 55 | 34 | 61 | `#37223D` | Y |
| 41 | 67 | 37 | 64 | `#432540` | Y |
| 42 | 72 | 39 | 66 | `#482742` | Y |
| 43 | 76 | 42 | 69 | `#4C2A45` | Y |
| 44 | 115 | 57 | 79 | `#73394F` | Y |
| 45 | 148 | 73 | 90 | `#94495A` | Y |
| 46 | 166 | 88 | 96 | `#A65860` | Y |
| 47 | 197 | 136 | 124 | `#C5887C` | Y |
| 48 | 28 | 67 | 69 | `#1C4345` | Y |
| 49 | 54 | 103 | 63 | `#36673F` | Y |
| 50 | 72 | 121 | 66 | `#487942` | Y |
| 51 | 87 | 133 | 67 | `#578543` | Y |
| 52 | 108 | 145 | 55 | `#6C9137` | Y |
| 53 | 124 | 154 | 46 | `#7C9A2E` | Y |
| 54 | 154 | 169 | 34 | `#9AA922` | Y |
| 55 | 188 | 187 | 19 | `#BCBB13` | Y |
| 56 | 58 | 70 | 78 | `#3A464E` | Y |
| 57 | 151 | 155 | 48 | `#979B30` | Y |
| 58 | 205 | 185 | 15 | `#CDB90F` | Y |
| 59 | 240 | 211 | 7 | `#F0D307` | Y |
| 60 | 243 | 214 | 7 | `#F3D607` | Y |
| 61 | 255 | 255 | 255 | `#FFFFFF` |  |
| 62 | 255 | 255 | 255 | `#FFFFFF` |  |
| 63 | 255 | 255 | 255 | `#FFFFFF` |  |

> 索引 `64`-`255` 全为 `#000000`。上表"有效"列标注为 `Y` 的索引才是 `active_fast` / `active_qual` 中的候选色。

### 10.7 多帧

当 `FrameCount > 1` 时，文件主体按帧顺序连续拼接，每帧大小由 `Format` 决定：

```
FileSize = 单帧主体大小 × FrameCount
```

播放时按帧顺序依次提交给显示驱动；MonoFast 帧之间保留上一帧状态，天然支持差分局刷动画。

### 10.8 v2 读取示例

```c
#include <stdint.h>

typedef struct {
    uint32_t fileSize;    // 0x00，小端
    uint16_t screenWidth; // 0x04
    uint16_t screenHeight;// 0x06
    uint8_t  colorCount;  // 0x08
    uint8_t  format;      // 0x09
    uint16_t frameCount;  // 0x0A，小端
    uint8_t  reserved[4]; // 0x0C
    uint8_t  colorTable[16]; // 0x10
} __attribute__((packed)) FilmHeaderV2;

// 根据 format 计算单帧主体字节数
static uint32_t film_frame_size(const FilmHeaderV2 *h) {
    uint32_t px = (uint32_t)h->screenWidth * h->screenHeight;
    switch (h->format) {
        case 0x00: return px / 2;   // v1 4bpp
        case 0x01: return px / 8;   // MonoFast 1bpp
        case 0x02:                  // ColorQual 8bpp
        case 0x03: return px;       // ColorFast 8bpp
        default:   return 0;        // 未知格式
    }
}

// 解析并显示单帧；film 指向完整文件缓冲（含 32 字节头），body = film + 32
static void film_display_frame(const uint8_t *film, uint8_t format) {
    const uint8_t *body = film + 32;
    switch (format) {
        case 0x00:
            hal_epd_display_film(film);         // v1：传入含文件头的完整缓冲
            break;
        case 0x01:
            hal_epd_display_mono(body);         // 1bpp 位图
            break;
        case 0x02:
            hal_epd_display_pic48(body);        // 8bpp 索引（ColorQual）
            break;
        case 0x03:
            /* ColorFast：固件当前接口默认 ColorQual，
               需要时扩展一个 fast 参数或专用接口 */
            break;
        default:
            break;
    }
}
```

> 注：ColorFast（`0x03`）的显示入口当前固件尚未单独暴露（`hal_epd_display_pic48` 固定走 ColorQual），实现 `0x03` 时需为 `epd_spectra_display_color` 增加 `mode=0` 的公开接口。


