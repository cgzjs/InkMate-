# 第三方内容与致谢

InkMate 使用以下词库、软件库和机械参考资料。

## 词库

- 文件：`firmware/CyberFortune_CZ04_2Buttons/data/vocab_cet6.tsv`、`vocab_ielts.tsv`。
- 来源：https://github.com/grhliu/wordtyper-vocabularies
- 词表派生自 WordTyper 词库项目，其数据来源包括 ECDICT。
- WordTyper 上游许可：MIT，Copyright (c) 2025 Linwei。
- 上游 MIT 正文保存在同目录 `LICENSE-WordTyper.txt`。

## 软件依赖

Arduino ESP32 Core、ESP-IDF、Adafruit GFX、Adafruit BusIO、U8g2 及 U8g2 for Adafruit GFX 等依赖通过开发环境安装，各自的来源与许可随上游项目提供。

## 显示驱动和资源

`EPaper_213_Driver.*`、`EPD.*`、`gui.*`、`global.h`、`icons.h`、`weather.h` 沿用设备工程中的墨水屏驱动、绘图代码和图形资源。

## 硬件及机械参考

- `hardware/reference/BB2V1.step`：维护者提供的去排针 PCB 机械参考。
- `enclosure/v3/source/reference_BBV3.step`：维护者提供的卡扣参考。
- `enclosure/v3/source/reference_v2/`：用于 v3 与 v2 的几何比较。

## 外壳生成工具

使用 CadQuery / OpenCascade、trimesh、NumPy、Matplotlib、VTK 和 NetworkX。刻字使用系统安装的 Arial Bold 字体，字体文件不随仓库提供。
