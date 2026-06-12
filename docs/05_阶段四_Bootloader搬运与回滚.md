# 阶段④ 操作指南:Bootloader 校验、备份、搬运、回滚

> 前置:阶段③已验收(固件能完整推送到 W25Q64 下载区)。
> 新增代码(已写好):[Common/stm_flash.c|h](../Common/stm_flash.c)(内部 Flash 擦写)、[Common/ota_boot.c|h](../Common/ota_boot.c)(Boot 端升级/回滚)、`ota.h` 扩展(备份/试运行标志)、`ota_recv.c` 新增 `OTA_ConfirmBoot()`。
> 两个工程的 main.c USER CODE 区已由 Claude 改好,无需手动贴代码。

## 1. 完整升级流程(代码已实现,理解即可)

```
App 收完固件 → 参数区写 [升级请求] → 用户复位
Boot 启动:
  ├─ 有 [试运行标志]?→ 上次新固件没确认 → 从备份区回滚 → 跳转旧固件
  ├─ 有 [升级请求]?
  │    ├─ 校验下载区 CRC,坏 → 作废请求,跳转旧固件
  │    ├─ 备份当前 App 区 → W25Q64 备份区(断电重来凭 session 跳过)
  │    ├─ 擦 App 区 → 从下载区搬运 → 整段 CRC 校验
  │    ├─ 成功 → 清请求、写 [试运行标志] → 跳转新固件
  │    └─ 失败 → 立即回滚
  └─ 都没有 → 直接跳转
App 启动成功 → OTA_ConfirmBoot() 擦除 [试运行标志] → 升级正式生效
```

参数区三条记录各占一个 4K 扇区:升级请求(0x000000)、备份记录(0x001000)、试运行标志(0x002000)。

## 2. Keil:Bootloader 工程添加文件

`Common` 组 → Add Existing Files,加入(共 3 个):

- `..\..\Common\w25q64.c`
- `..\..\Common\stm_flash.c`
- `..\..\Common\ota_boot.c`

App 工程**不需要加任何新文件**(`OTA_ConfirmBoot` 就在已有的 ota_recv.c 里)。

## 3. 编译注意:Bootloader 体积

Boot 现在塞进了 W25Q64 驱动 + CRC32 + 升级逻辑,编译后看 Build Output 的
`Program Size: Code=xxxx RO-data=xxxx`,**Code+RO 必须 < 16384**。
若超了:Options for Target → C/C++ → Optimization 改为 **-O2** 或 **-Oz image size**。

## 4. 演示一:正常升级 v1.1 → v2.0

1. 用 ST-Link 重新烧一次 **Bootloader**(它变了);
2. App 工程改 `Core\Inc\app_version.h` 里的 `APP_VERSION_MAJOR/MINOR`(版本号唯一来源,
   串口打印和上位机读到的都出自它;想直观点可再把 LED 周期 500ms 改成 200ms),
   编译——**不要用 ST-Link 烧**,要的就是 bin;
3. 关串口助手,推送(版本号由上位机从 bin 内自动读取,无需 --version):
   ```powershell
   python ota_host.py COM7 ..\App\MDK-ARM\App\App.bin
   ```
4. 打开串口助手,按复位,应看到:
   ```
   [Boot] Bootloader v1.0 @0x08000000
   [Boot] Update request: v2.0, xxxxx bytes
   [Boot] Backing up current App ...
   [Boot] Flashing new firmware ...
   [Boot] Update OK, trial boot. App must confirm.
   [Boot] Jump to App @0x08004000 ...
   [App] Firmware v2.0 running.
   [App] Trial boot confirmed (v2.0).
   [App] OTA receiver ready.
   ```
   板子上的固件已经被它自己换掉了——这就是 OTA。再按一次复位,Boot 不再打升级日志,直接跳 v2.0(标志已消费)。

## 5. 演示二:坏固件自动回滚(项目最大亮点)

"坏固件" = 能启动但从不确认(模拟上电即跑飞的固件):

1. App 工程**临时注释掉** `OTA_ConfirmBoot();` 这一行,`app_version.h` 改成 9.9,编译出 bin;
2. 推送:`python ota_host.py COM7 ..\App\MDK-ARM\App\App.bin`,复位 → Boot 正常升级并跳转,
   串口出现 `[App] Firmware v9.9 running.`,但没有 confirmed 一行(试运行标志还在);
3. **再按一次复位**(模拟坏固件崩溃重启),应看到:
   ```
   [Boot] Last FW v9.9 was NOT confirmed.
   [Boot] Rolling back from backup ...
   [Boot] Rollback OK.
   [Boot] Jump to App @0x08004000 ...
   [App] Firmware v2.0 running.
   ```
   v9.9 被自动换回了 v2.0——零干预回滚;
4. 演示完把 `OTA_ConfirmBoot();` 的注释**恢复**。

## 6. 验收标准

- [ ] 演示一:一键推送 → 复位 → 自动升级 → 新版本运行并 confirmed;
- [ ] 升级后再复位一次:Boot 无升级动作,直接跳新固件;
- [ ] 演示二:坏固件复位两次后自动回滚到上一版本;
- [ ] 按住 PB0 上电:任何时候都能停留 Boot(救砖)。

全部勾上,**OTA 项目四个阶段完结**。

## 7. 常见坑

| 现象 | 原因 |
|---|---|
| 链接报 L6406E/L6220E 空间不够 | Boot 超 16KB,见第 3 节调优化等级 |
| Boot 打印 Update OK 但 App 没跑起来 | bin 不是按 0x08004000 链接的(检查 App 工程 IROM1) |
| 每次复位都重复升级 | 试运行/请求标志没被消费,检查 App 是否调用了 OTA_ConfirmBoot |
| 回滚后还是坏固件 | 坏固件推送了两次,把备份区也污染了(备份的是上一个坏版本) |
| Backup failed | W25Q64 接线松了,Boot 阶段也要用它 |

## 8. 面试讲法(这个项目的灵魂三问)

- **为什么要"试运行 + App 确认"而不是 Boot 验证完就完事?** CRC 只能证明"搬运没出错",不能证明"固件逻辑是好的"。让新固件自己证明自己能跑到业务初始化完成,才敢把它转正——这是 A/B 升级里 health check 的极简实现。
- **断电安全怎么保证的?** 任何时刻断电,重启后状态机都能收敛:请求标志在 → 重做升级(备份凭 session 去重,不会被半擦除的 App 污染);试运行标志在 → 回滚;什么都不在 → 正常跳转。Flash 写入顺序刻意安排为"先做事,后改标志"。
- **救砖的底线是什么?** Bootloader 本身永不自改写,且按键强制驻留 Boot 的通道独立于一切标志位——最坏情况下重新串口推送即可,physically 救不回来的只有 Boot 区被 ST-Link 误擦这一种人祸。
