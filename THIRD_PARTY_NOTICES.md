# 第三方内容、来源及授权状态

本文件是当前整理记录，不是完成的法律合规审计。项目未给原创软件添加统一许可；不能用这一点覆盖已有的第三方许可。

## 词库

- 文件：`firmware/CyberFortune_CZ04_2Buttons/data/vocab_cet6.tsv`、`vocab_ielts.tsv`。
- 来源：https://github.com/grhliu/wordtyper-vocabularies
- 本地原说明记录其词表派生自 ECDICT；数据来源链应继续保留和核查。
- WordTyper 上游许可：MIT，Copyright (c) 2025 Linwei。
- 上游 MIT 正文已保存在同目录 `LICENSE-WordTyper.txt`，未修改为非商用许可。
- CET / IELTS 只说明词表分类，不代表考试机构认证。

## 软件依赖

Arduino ESP32 Core、ESP-IDF、Adafruit GFX、Adafruit BusIO、U8g2 及 U8g2 for Adafruit GFX 等依赖由开发环境安装，本仓库没有复制这些完整库。不同组件和字体可能使用不同许可，应以实际使用版本的上游文件为准。

本仓库“不提供原创软件许可”不改变这些依赖的权利。若发布包含依赖的编译镜像，还必须分别履行上游分发条件；不能仅靠这份列表代替必要的许可证、源码或其他义务。

## 历史代码和资源：尚需核查

`EPaper_213_Driver.*`、`EPD.*`、`gui.*`、`global.h`、`icons.h`、`weather.h` 来自现有工程，并非本次整理新写的全部原创内容。检查这些文件时没有发现足以确认完整授权链的独立许可信息。

在确认原始来源之前，不将其直接标记为维护者原创，不施加新非商用许可，也不声称已获任意再分发授权。对外发布前请补齐原作者、来源和许可；若不能确认，应取得授权或移除、替换相关内容。仅保留源码可见状态不能弥补缺失的分发授权。

## 硬件及机械参考

- `hardware/reference/BB2V1.step`：维护者提供的去排针 PCB 机械参考。
- `enclosure/v3/source/reference_BBV3.step`：维护者提供的卡扣参考。
- `enclosure/v3/source/reference_v2/`：仅用于 v3 与 v2 的数字比较，不是旧版发布套件。
- STEP 中包含的第三方器件模型、标识和参考结构不因整理而变成新原创，其来源和许可仍需保留、核实。

## 外壳生成工具

使用 CadQuery / OpenCascade、trimesh、NumPy、Matplotlib、VTK 和 NetworkX，完整依赖不随仓库分发。构建脚本可能使用系统安装的 Arial Bold 字体，仓库没有附带该字体文件；用户需要自行具备相应使用权，或更换为许可合适的字体。

新增文档和原创设计的许可范围见 [docs/PERMISSIONS.md](docs/PERMISSIONS.md)。没有新的项目级软件授权，不意味着可删掉这里的第三方说明。
