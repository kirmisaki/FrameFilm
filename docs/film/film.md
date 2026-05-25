# .film 文件格式详细规范

## 1. 概述

`.film` 文件是 FrameFilm 项目用于存储电子纸显示屏帧数据的专用二进制文件格式。该格式针对墨水屏（e-Paper）特性优化，采用 4bit 颜色编码以高效利用存储空间，支持 2 色到 6 色的多色显示屏。

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
| 0x08 | ColorCount   | 1 B  | uint8   | 屏幕颜色数量，有效值范围：2-6      |
| 0x09 | Reserved     | 7 B  | byte\[] | 保留字段，用于未来扩展           |
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
- **取值范围**：2 - 6
- **含义**：表示显示屏支持的颜色数量
  - `2`：双色屏（如黑白屏）
  - `3`：三色屏（如黑白红）
  - `4`：四色屏
  - `5`：五色屏
  - `6`：六色屏

#### 3.1.5 Reserved（保留字段）

- **偏移**：0x09
- **长度**：7 字节
- **类型**：字节数组
- **填充值**：建议填充 `0x00`
- **含义**：保留用于未来功能扩展，当前版本不使用

#### 3.1.6 ColorTable（文件颜色表）

- **偏移**：0x10
- **长度**：16 字节
- **类型**：字节数组
- **含义**：将实际颜色值映射为 0-15 的颜色编码
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

## 4. 文件主体详细定义

### 4.1 数据结构

文件主体是连续的 **颜色编码数据流**，总字节数由文件头的 `FileSize` 字段指定。

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

## 5. 文件头示例

假设配置如下：

- 屏幕尺寸：800 x 600
- 颜色数量：4 色
- 颜色表定义如 3.1.6 所示

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
| Reserved     | 0x09 | 7 B  | `00 00 00 00 00 00 00` | 保留字段              |
| ColorTable   | 0x10 | 16 B | `00 01 FF 03 ...`      | 颜色编码映射表           |

## 6. 读取流程

1. 读取文件头前 4 字节，获取 `FileSize` 值 N
2. 读取文件头偏移 0x04-0x05 的 2 字节，获取 `ScreenWidth`
3. 读取文件头偏移 0x06-0x07 的 2 字节，获取 `ScreenHeight`
4. 读取文件头偏移 0x08 的 1 字节，获取 `ColorCount`
5. 读取文件头偏移 0x10-0x1F 的 16 字节，获取 `ColorTable`
6. 根据 `FileSize` 读取文件主体数据
7. 按每字节拆分为两个 4bit 颜色编码进行解析

## 7. 注意事项

1. **字节序**：所有多字节整数采用小端序（Little-Endian）存储
2. **对齐**：文件主体数据无需 4 字节对齐
3. **颜色编码**：实际显示颜色需通过 `ColorTable` 转换
4. **扩展性**：`Reserved` 字段保留用于未来功能扩展
5. **校验**：建议在读取时验证 `FileSize` 与实际文件大小是否匹配

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

