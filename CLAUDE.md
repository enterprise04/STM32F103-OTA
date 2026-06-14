# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

STM32F103C8T6 串口 IAP/OTA 固件升级系统:自定义 Bootloader + 外部 SPI Flash 暂存 + 整包 CRC/SHA-256 校验 + ECDSA 签名验证 + 试运行确认与自动回滚 + IWDG 看门狗。上位机用 Python 经 USB 转串口模拟云端推送固件。环境为 **Keil MDK(AC5/armcc V5.06)+ STM32CubeMX**。

## 顶层结构

```
Bootloader/   CubeMX+Keil 工程:升级判定、备份、搬运、回滚、救砖(永不自改写)
App/          CubeMX+Keil 工程:业务 + 固件接收(USART1 DMA)+ 验签 + 启动确认
Common/       两工程共享的纯逻辑源码(见下方"共享文件分配")
Host/         Python 上位机(ota_host.py 推送 / keygen.py 生成密钥)
docs/         01~07 分阶段设计与操作文档,含每阶段踩坑记录
```

`Bootloader/` 和 `App/` 是**两个完全独立的工程**,各自有 CubeMX 的 `.ioc`、HAL 库副本和 `Core/`。它们通过 `Common/` 共享同一份逻辑代码,但**各自只编译用得到的子集**。

## 构建 / 烧录 / 运行

- **编译**:Keil GUI 按 F7。无纯 CLI 构建脚本;如需命令行,`UV4.exe -b <工程>.uvprojx -o build_log.txt`(UV4 在 Keil 安装目录,本机非 PATH)。
- **App 的 bin**:由 Keil After Build 的 `fromelf --bin` 自动生成在 `App/MDK-ARM/App/App.bin`。**OTA 只用 bin,不用 hex/axf**(bin 是按 0x08004000 链接的裸机器码,Boot 直接搬运)。
- **首次烧录 Bootloader**:ST-Link。**给 App 下载固件时必须用 Erase Sectors,严禁 Erase Full Chip**(否则擦掉 Bootloader)。
- **推送固件**(关串口助手,COM 口独占):
  ```powershell
  cd Host
  pip install pyserial ecdsa          # 一次性
  python keygen.py                    # 一次性,生成 private_key.pem + ../App/Core/Inc/ecc_pubkey.h
  python ota_host.py COM7 ..\App\MDK-ARM\App\App.bin          # 自动签名+推送
  python ota_host.py COM7 ..\App\MDK-ARM\App\App.bin --tamper # 破坏签名,验证板端拒绝
  ```

## 核心架构:OTA 状态机靠 W25Q64 参数区三个标志驱动

Boot 与 App 不直接通信,而是通过 W25Q64 **参数区的三条记录**(各占一个 4K 扇区,结构与魔数定义在 `Common/ota.h`)交接状态:

| 标志扇区 | 写者 | 含义 |
|---|---|---|
| `OTA_REQ_ADDR` (升级请求) | App 收完固件 | "下载区有待装固件" |
| `OTA_BAK_ADDR` (备份记录) | Boot 备份后 | 备份有效 + `session`(=新固件 CRC,断电去重防污染) |
| `OTA_TRIAL_ADDR` (试运行) | Boot 搬运后 | "新固件试运行中,App 未确认" |

**升级流程**(理解回滚必读 `ota_recv.c` 的 `handle_end`/`OTA_ConfirmBoot` 和 `ota_boot.c` 的 `OTA_Boot`):
```
App 收完固件→校验 CRC+签名→写[REQ]→复位
Boot: 有[TRIAL]? → 上次没确认 = 坏固件 → 从备份区 rollback
      有[REQ]?   → 校验下载区→备份(session 去重)→擦写 App 区→校验→写[TRIAL]→跳转
新 App 启动成功 → OTA_ConfirmBoot() 擦[TRIAL] = "报平安" → 升级生效
新 App 崩溃/卡死复位 → Boot 见[TRIAL]仍在 → 回滚
```
关键设计:**回滚靠"死人开关"**——Boot 留下 TRIAL 标志,期待 App 主动擦除;App 没擦(崩溃/卡死/被注释)就回滚。这是 MCUboot image-OK flag 的简化版。

## Flash 地址布局(改动前务必对齐)

- 内部 Flash:Bootloader `0x08000000`/16KB,App `0x08004000`/48KB(`Common/boot.h`)。App 工程必须 `SCB->VTOR=0x08004000` 重定位向量表 + `__enable_irq()`(Boot 跳转前关了中断)。
- W25Q64:参数区 `0x000000`、下载区 `0x010000`、备份区 `0x020000`(`Common/w25q64.h`)。
- **Bootloader 必须 < 16KB**。它已含 W25Q64 驱动 + CRC32 + 内部 Flash 擦写 + 升级逻辑;若超出,调 Keil 优化等级到 -O2/-Oz,不要随意扩分区(会牵动 App 链接地址与重烧)。

## 共享文件分配(向工程加文件时按此,加错会链接报错或体积超标)

| `Common/` 文件 | Bootloader 工程 | App 工程 |
|---|---|---|
| `crc32.c` `w25q64.c` `retarget.c` | ✅ | ✅ |
| `boot.c` `stm_flash.c` `ota_boot.c` | ✅ | ✗ |
| `ota_recv.c` `sha256.c` `micro-ecc/uECC.c` | ✗ | ✅(验签) |

两工程的头文件搜索路径都含 `..\..\Common`;App 另靠 `..\Core\Inc` 找 `ecc_pubkey.h`。`micro-ecc/uECC.h` 顶部已 vendoring 改为 `uECC_PLATFORM=uECC_arch_other`(纯 C,因 AC5 不兼容其 GCC 内联汇编)+ 仅 secp256r1。

## CubeMX 工程约定(强约束)

- **只在 `USER CODE BEGIN/END` 区写业务代码**;外设配置(DMA/GPIO/时钟/NVIC/IWDG 等)走 CubeMX GUI 重新生成,不手改 CubeMX 管理区,不与生成代码同名。改动前先征得同意。
- 重新生成代码后,USER CODE 区保留,但需确认 `USER CODE` 配对标记完整——历史上出现过标记被破坏导致主循环代码丢失/编译错。
- App 已配:USART1(115200,RX 走 DMA1_Ch5 Normal 模式 + 空闲中断)、SPI1(PA4 软件 CS)、I2C1、IWDG、LSI。

## 关键约定与易错点

- **版本号单一来源**:只改 `App/Core/Inc/app_version.h` 的宏。版本随 `g_fw_info` 信息块(双魔数 'FWVR''INFO')编入 bin,上位机 `ota_host.py` 从 bin 扫描读取,无需 `--version`。`g_fw_info` 声明为 `const volatile`——**勿去掉 volatile**,否则会被链接器当未引用常量裁剪,上位机就读不到版本(AC5 优化所致)。
- **改任何 App 代码后必须重新编译再推送**:bin 不更新就是推旧固件,排查极费时。
- **密钥对必须匹配**:`private_key.pem`(签名)与 `ecc_pubkey.h`(验签)是一对;重新生成密钥后**必须重新编译 App**,否则验签全失败。`*.pem` 已 gitignore,**私钥绝不上传**。
- **IWDG 一旦启动,任何复位都不停**:故 App 启用 IWDG 后,Boot 的备份/搬运循环(`ota_boot.c`)与 Boot 主循环也必须喂狗(`OTA_FEED_WDG()`,`ota.h`),否则升级被看门狗打断、救砖停留被复位。
- **串口 COM 口独占**:`ota_host.py` 与串口助手不能同时占用同一 COM——推送时关串口助手,看日志时关 python。
- **演示开关**:`App/Core/Src/main.c` 的 `#define SIMULATE_HANG`(故意卡死、用于看门狗回滚演示)**提交前确认是注释状态**,勿把坏固件开关提交。
- **串口在固件传输期间不 printf**:协议与日志共用 USART1,传输中打印会污染应答字节流;板端只在传输结束后打印。
- **二进制内容检索**:用 Python `bytes.find` 查 bin(如版本魔数),不要用 grep/rg——遇 NUL 会提前停止。
