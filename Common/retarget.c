/* printf 重定向到 USART1(Keil MicroLIB)
 * 需在 Keil: Options for Target → Target → 勾选 "Use MicroLIB"
 */
#include <stdio.h>
#include "stm32f1xx_hal.h"

extern UART_HandleTypeDef huart1;

int fputc(int ch, FILE *f)
{
    (void)f;
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);
    return ch;
}
