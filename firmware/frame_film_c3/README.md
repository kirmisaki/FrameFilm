# FrameFilm C3：独立机型，实验性移植

独立目标 `frame_film_c3`，广播名 `FRAMEFILMC3`，面板 `0x08` JD79665AA 768×552 四色。
不是 STD/Pro/Max 配置，不修改三个既有固件目录；直接编译复用经典固件的 BLE、OTA、参数、
文件显示调度、休眠和 JD79665AA 行式驱动，替换 C3 的 HAL 和文件存储。

## 硬件边界

当前是 ESP32-C3 4MB Flash、无 PSRAM、无 TF 卡的开发板配置，不是已验收的 PCB9 配置。
默认 MOSI=3、SCK=4、CS=5、DC=6、RST=7、BUSY=10（低有效）；可在 menuconfig 的 FrameFilm C3 菜单修改。
与帧扣已有开发板源码的引脚一致，但接线、电压、A1 显示方向及波形仍需实板确认。

裸开发板未假定有 RGB LED、按键、电池分压或电源保持电路：电量回报 255 表示未知，
LED/按键 API 不驱动不存在的外设。默认休眠使用上游断连空闲策略及 deep sleep；无按键唤醒时使用复位，
可沿用上游定时唤醒设置。深睡眠不等于 PCB 硬件断电；PCB 电源/电池/LED/按键需另加经验证的板级配置。

## 存储与传输

- 继续使用官方 0x55 BLE 协议及 0x42 五字节面板参数；不增加客户端私有协议。
- 当前只保存最后一幅单帧 v1 `.film`：32 字节头 + 211968 字节 4bpp，合计 212000 字节。
- 一个逻辑文件（ID=0），两个 Flash bank，分块写入后校验文件头、色码、CRC32 和读回数据，最后提交 NVS 指针。
- 错误/不完整上传不替换旧图；静默标志只保存、不刷新。加载使用 Flash 映射指针，没有 PSRAM 整帧拷贝。
- 原有文件显示服务和屏驱动继续完成 800×600 物理帧、x=32 可见区补边及 4bpp→2bpp 打包。
- 图像缓冲访问/Flash 写入与显示串行；显示期间再次上传可能等待，需在实机验证客户端超时和队列行为。
- OTA 双分区已配置，复用上游 OTA 实现，尚未进行 OTA 真机验收。
- Wi-Fi 下载仍沿用上游依赖 PSRAM 的路径，当前 C3 无 PSRAM，不能视为已支持；默认关闭 Wi-Fi。
- 动画、Ark app/遥控及 Dock USB/HID 不属于该目标的能力，不会伪装支持。

## 构建

已在 Windows ESP-IDF 5.3.1 / GCC 13.2 上构建；上游常用 5.5.2 尚未验证。

```
cd firmware/frame_film_c3
idf.py build
```

C3 专用组件在编译时强制使用本目录板级头，既有 S3 宏、默认 sdkconfig、引脚与构建结果不改变。
只对复用的旧 C 源码放宽 format 警告为 warning：RISC-V uint32_t 与其旧 `%d` 日志存在类型提示；
没有关闭其他编译错误，也未为消除日志提示修改旧机型源码。

首次迁移使用本目标的 bootloader、分区表、otadata 与 APP（见构建生成的 flash_args）；
不能把它作为旧开发板分区布局下的 APP 单独写入。新分区布局与原工程不同，迁移前保留旧固件和 Flash 备份。
本次没有烧录设备、擦除 Flash 或更改原帧扣工程。

## 主机测试

在 Visual Studio x64 Native Tools shell 中运行：

```
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host_tests.ps1
```

直接编译本目标实际 `service_file_c3.c`，用模拟 Flash/NVS 验证分包、重新初始化、
不完整/非法上传、提交失败、写失败、静默保存、文件名与删除恢复。
模拟测试不能证明 SPI 时序、Flash cache/MMU 行为、手机 BLE、实际功耗或实体屏显示已通过。

提交应作为 Draft，待真实 C3 连接→上传→显示→重连及断电恢复验收后再转 Ready。
