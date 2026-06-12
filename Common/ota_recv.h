#ifndef __OTA_RECV_H
#define __OTA_RECV_H

/* App 端固件接收模块(仅 App 工程使用)
 * 依赖:USART1 已配 RX DMA(Normal 模式)、w25q64、crc32
 */
void OTA_RecvInit(void);    /* 启动 DMA 空闲接收,在外设初始化后调用一次 */
void OTA_RecvPoll(void);    /* 在主循环中持续调用,处理收到的帧 */
void OTA_ConfirmBoot(void); /* 初始化成功后调用:擦除试运行标志,告诉 Boot"我没问题"。
                               不调用的固件会在下次复位时被 Boot 回滚 */

#endif /* __OTA_RECV_H */
