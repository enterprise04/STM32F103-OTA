# STM32F103 串口 IAP/OTA 固件升级系统

基于 STM32F103C8T6 最小系统板实现的完整 OTA 固件升级方案:自定义 Bootloader + 外部 SPI Flash 暂存 + 整包 CRC 校验 + **试运行确认与自动回滚**。上位机用 Python 模拟云端服务器,通过串口推送固件,板子自己完成"备份 → 搬运 → 校验 → 转正/回滚"全流程,无需 ST-Link 介入。

## 功能特性

- **串口 IAP 升级**:App 运行中接收新固件,写入 W25Q64 下载区,复位后由 Bootloader 完成搬运
- **失败安全(Fail-Safe)**:逐帧 CRC32 + 整包 CRC32 双重校验;升级前自动备份旧固件;任意时刻断电,重启后状态机自动收敛(凭 session 标识防止重复备份污染)
- **试运行 + 自动回滚**:新固件必须在启动后主动"确认",否则下次复位 Bootloader 自动从备份区回滚——CRC 只能保证"搬运没错",确认机制才能保证"固件能跑"
- **救砖入口**:上电按住按键强制驻留 Bootloader,独立于一切标志位
- **版本单一来源**:版本号只在 `app_version.h` 定义一次,编译进固件信息块(双魔数),上位机从 bin 中自动扫描读取,杜绝"打印版本与实际固件不一致"

## 硬件

| 部件 | 说明 |
|---|---|
| STM32F103C8T6 | 64KB Flash / 20KB RAM,最小系统板 |
| W25Q64 | 8MB SPI Flash(SPI1,PA4 片选):下载区 / 备份区 / 参数区 |
| USB 转串口 | USART1(PA9/PA10)115200,固件传输 + 日志共用 |
| 按键 PB0 | 上电按住 → 强制停留 Bootloader |
| LED PC13 | 状态指示(Boot 与 App 闪烁频率不同,传输时逐包翻转) |

## 架构

```
PC 上位机(Host/ota_host.py,模拟云端)
        │ 帧协议:AA 55 | CMD | SEQ | LEN | DATA | CRC32,停等 ACK + 超时重传
        ▼
STM32F103C8T6
   ├─ Bootloader @0x08000000(16KB):校验 → 备份 → 搬运 → 试运行标志 → 跳转 / 回滚
   ├─ App        @0x08004000(48KB):业务 + 固件接收(DMA 空闲中断)+ 启动确认
   └─ W25Q64:参数区 0x000000 | 下载区 0x010000 | 备份区 0x020000
```

升级时序:

```
App 收完固件并校验 → 写[升级请求] → 复位
Boot:校验下载区 → 备份当前 App → 擦写 App 区 → 整包校验 → 写[试运行] → 跳转
新 App 启动成功 → OTA_ConfirmBoot() 擦除[试运行] → 升级生效
新 App 跑飞复位 → Boot 发现[试运行]仍在 → 从备份区回滚旧版本
```

## 目录结构

```
├── Bootloader/   CubeMX + Keil 工程(SPI1 + USART1)
├── App/          CubeMX + Keil 工程(另有 I2C1、USART1 RX DMA)
├── Common/       两工程共享:跳转、W25Q64/内部Flash 驱动、CRC32、协议、收发逻辑
├── Host/         Python 上位机(pyserial)
└── docs/         五份分阶段设计/操作文档(含踩坑记录)
```

## 使用

```powershell
# 1. Keil 分别编译 Bootloader 与 App,ST-Link 烧录 Bootloader(仅此一次)
# 2. 改 App/Core/Inc/app_version.h 的版本宏,编译出 App.bin(After Build 自动 fromelf)
# 3. 推送(版本号自动从 bin 读取):
pip install pyserial
cd Host
python ota_host.py COM7 ..\App\MDK-ARM\App\App.bin
# 4. 复位板子,串口观察 Boot 备份/搬运/跳转日志,新版本运行并自动确认
```

回滚演示:注释 App 的 `OTA_ConfirmBoot()` 模拟"能启动但跑飞"的坏固件,推送后复位两次,Bootloader 自动回滚到上一版本。

## 技术要点

- 中断向量表重定位(`SCB->VTOR`)、MSP 切换与跳转前现场清理
- USART DMA + 空闲中断收帧(关闭半满中断),耗时擦写移出 ISR
- NOR Flash 特性:先擦后写、页编程边界、BUSY 轮询;内部 Flash 页擦除/半字编程
- 与 `zlib.crc32` 一致的 CRC32 实现,板端/上位机互验
- `volatile` 的三种实战用法:中断共享变量、外设寄存器、防止固件信息块被链接器裁剪

详细设计与每一阶段的踩坑记录见 [docs/](docs/)。
