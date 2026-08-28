# Music test files / 音乐测试文件

This directory contains the two Ogg Opus files used by the EasyInput V2.0
offline music hardware demonstration. They are not required by the default
online-music build:

- `track0-dao-xiang.opus.ogg` — 《稻香》 / Jay Chou (周杰伦)
- `track1-flower.opus.ogg` — “Flower” / Johnny Stimson

These are uploaded at the project owner's request for development-board
testing. They are not project-owned recordings and this repository does not
claim any song, composition, master recording, performer, publisher, or
platform rights. The uploader and every redistributor are responsible for
obtaining the permissions required in their jurisdiction and on their target
platform. Without a valid redistribution license, remove the files and do not
use them for commercial distribution, public rehosting, or any use outside the
authorized test scope.

构建系统要求音乐输入位于源码树之外。若从本仓库构建，请先把经过授权的
Ogg 文件复制到源码目录外，再按根目录 README 的绝对路径参数运行 `idf.py`。
