# 规划:接入 ESP8266,从"串口模拟"升级到"真·远程 OTA"

> 目标:在**完整保留**现有可靠/安全内核(CRC + SHA-256 + ECDSA 签名 + 备份 + 试运行确认 + 回滚 + 看门狗)的前提下,新增一条 WiFi 数据源,实现真正的远程升级。
> 已定选型:ESP8266(USART2,AT 固件)+ 自包含镜像头 + 本地/局域网固件服务器 + emqx 公共 MQTT broker。

## 0. 核心原则:数据源与升级内核解耦

```
  串口(已有,救砖通道保留) ──┐
                            ├──▶  ota_core(校验/暂存/写请求)──▶ Bootloader(0 改动)
  HTTP over ESP8266(新增) ─┘         CRC+SHA+ECDSA 复用              备份/搬运/回滚/看门狗
```

**Bootloader 只认 W25Q64 参数区标志,不关心固件来自串口还是 HTTP。** 所以回滚、看门狗、备份**一行都不改**。本规划只做两件事:① 新增 HTTP 数据源;② 把串口路的校验逻辑抽成数据源无关的 `ota_core`,两条路共用。

## 1. ⚠️ 需要新采购的硬件

套件里**没有 ESP8266**,需单独买:推荐 **ESP-01S**(ESP8266,出厂 AT 固件)。买来先确认是 AT 固件、波特率(多为 115200)。

## 2. 自包含镜像格式(契约)

固件不再是裸 `App.bin`,而是打包成 `xxx.img`:

```
xxx.img = [ 镜像头 128 字节 ] + [ 固件本体 = App.bin ]

镜像头 ota_img_header_t:
  uint32 magic        'OTAH'(0x4854414F 小端)
  uint16 hdr_ver      = 1
  uint16 hdr_len      = 128
  uint32 fw_version   固件版本(同 app_version)
  uint32 fw_size      本体字节数
  uint32 fw_crc32     本体 CRC32
  uint8  fw_sha256[32] 本体 SHA-256
  uint8  signature[64] 对 fw_sha256 的 ECDSA P-256 签名
  uint8  reserved[...] 补齐到 128
```

- 由 PC/云端的打包工具 `Host/pack.py`(基于现有 `keygen.py` 私钥)生成。
- 设备无论从串口还是 HTTP 拿到镜像,都用同一套:**收头→校验头→按 fw_size 收本体(边收边算 CRC/SHA)→比对 CRC/SHA→`uECC_verify` 验签**。
- 签名覆盖 `fw_sha256`,与现有 App 端验签逻辑完全一致 → `micro-ecc`/`sha256` 直接复用。

## 3. 升级内核接口(从 ota_recv.c 抽出)

新增 `Common/ota_core.c`,把现在和串口帧耦合的"写下载区/校验/写请求"抽出来:

```c
int ota_core_begin(const ota_img_header_t *hdr);  // 校验头、擦下载区、初始化增量 CRC/SHA
int ota_core_feed (const uint8_t *data, uint32_t len);  // 写下载区 + 更新 CRC/SHA
int ota_core_finish(void);  // 比对 CRC/SHA → uECC_verify 验签 → 写 OTA_REQ 升级请求
```

- `ota_recv.c`(串口)重构为"帧解析层":解析 AA55 帧后转调 `ota_core_*`。
- `ota_http.c`(新增)从 ESP8266 收字节流后转调 `ota_core_*`。
- **校验/签名/写请求逻辑只存在于 ota_core,一处维护,两路复用。**

## 4. 分阶段实施

| 阶段 | 目标 | 你做 | 我做 | 验收 |
|---|---|---|---|---|
| **A 联网地基** | ESP8266 连 WiFi、AT 收发通 | CubeMX 开 USART2 + 接线 | `esp8266.c`(AT 封装、连 WiFi) | USART1 日志打印 ESP 拿到 IP |
| **B 镜像+内核重构** | 镜像格式落地;`ota_recv` 抽出 `ota_core` | — | `ota_image.h`、`ota_core.c`、`pack.py`;改 `ota_recv.c` | 串口路改走 ota_core 后,原签名升级仍 OK |
| **C HTTP 拉取** | 设备 HTTP GET 镜像→喂 ota_core | PC 跑 `python -m http.server` 放镜像 | `ota_http.c`(**解析 Content-Length 精确收**,不用丢字节 hack) | 设备从本机 HTTP 拉镜像,验签通过,复位升级成功 |
| **D MQTT 通知** | 收到通知切入下载 | 用 emqx 公共 broker | MQTT 通知订阅+解析 | 电脑发一条 MQTT 消息,设备自动开始升级 |
| **E 云端+Web** | 固件仓库+自动打包签名+上传页 | (可选)本机 Nginx | 服务端脚本 | 网页上传 bin→自动签名打包→通知设备 |

## 5. 硬件接线(阶段 A,断电接)

| ESP-01S | STM32 | 备注 |
|---|---|---|
| TX | PA3 (USART2_RX) | 交叉 |
| RX | PA2 (USART2_TX) | 交叉 |
| VCC | **3.3V** | ⚠️ ESP8266 瞬时电流大(300mA+),板载 LDO 可能带不动→连不上/复位。建议独立 3.3V 供电或在 VCC-GND 并 470µF 电容 |
| CH_PD/EN | 3.3V | 必须拉高,否则模块不工作 |
| GND | GND | 共地 |
| RST/GPIO0 | 悬空 | 正常运行模式 |

## 6. CubeMX 配置(阶段 A,只动 App.ioc)

1. Connectivity → **USART2** → Mode: Asynchronous;115200 8N1;PA2=TX/PA3=RX;
2. NVIC Settings 勾 **USART2 global interrupt**;
3. (DMA 阶段 C 拉大流量时再加 USART2_RX/DMA1_Ch6,阶段 A 中断即可);
4. GENERATE CODE。

## 7. 明确不变的部分(你的护城河)

- **Bootloader**:0 改动(备份/搬运/回滚/救砖)。
- **看门狗 IWDG**:不变(HTTP 下载循环里同样要喂狗 `OTA_FEED_WDG()`)。
- **签名验证**:复用,只是元数据来源从"串口 START 帧"变成"镜像头"。
- **串口 OTA**:保留为**救砖通道**——联网刷坏了还能 USB 串口救。

## 8. 绝不照搬参考项目的反模式

- ❌ "遇到 \r\n 就丢字节 + file_size--":二进制固件含大量 0x0D/0x0A,会误删机器码。**我们按 Content-Length 精确收**。
- ❌ "差 16 字节算成功":**我们 CRC+SHA+签名,差一个 bit 都拒绝**。
- ❌ HTTP/1.0 碰运气拿不分块流:**我们解析响应头**(若服务器 chunked 则正确解块)。
- ✅ 可借鉴:控制流(MQTT 通知)/数据流(HTTP 拉取)分离、设备主动 pull。
