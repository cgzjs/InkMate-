# 通过 GitHub 网页上传

## 创建仓库时

- 仓库名：`InkMate`。
- README：可以保持关闭，本目录已有完整 README。
- .gitignore：可以保持 No .gitignore，本目录已有配置。
- License：保持 No license，项目使用说明写“请勿用于商业项目”。

推荐简介（不再使用“开源”描述）：

> 墨伴 InkMate，一款基于 ESP32 的墨水屏随身挂件，内置 24 项功能，包含电子书、六级与雅思单词学习、间隔复习、时钟、番茄钟、待办、喝水记录和趣味工具。支持网页内容管理、图片上传、自定义菜单与 OTA 更新，兼容 4MB / 16MB Flash，附固件源码、PCB 机械参考及轻薄 3D 打印外壳。四键操作，USB-C 充电，锂电池供电。请勿用于商业项目。

## 上传文件

进入仓库 Add file → Upload files。把 InkMate 文件夹**里面的文件和目录**拖入页面，根目录直接显示 README.md、firmware、hardware 等内容。旁边的 InkMate_Archive 保留在本地。

网页上传**不会替你按本地 .gitignore 过滤文件**，所以仍要检查选择的内容。本次目录未放个人备份、编译缓存、旧固件或 Python 环境，单个文件小于 25 MiB。可按目录分批上传并提交。

本地 `archive` 不应进入仓库。将来项目历史由 Git 提交与 Releases 记录，不需要把每次完整源码 ZIP 累积在 main 分支。

## 发布下载包

源码目录不存放固件 `.bin`，这些已在原下载目录的 `FortunePager_v2.8.0.zip`。外壳打印包为 `BB2V1_v3_replacement_prints.zip`，完整外壳包为 `BB2V1_Thin_Enclosure_v3.zip`。

这些下载包沿用 FortunePager / BB2V1 名称，分别对应固件和外壳。

创建 Release 时使用 `v2.8.0` 标签，在说明中注明对应外壳为 v3，并区分“完整烧录”“OTA 更新”“仅替换打印件”。个人 `backup_*.bin` 永远不要当作完整固件发布。
