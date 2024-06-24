/*  SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  FM-DX Tuner
 *  Copyright (C) 2024  Konrad Kosmatka
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 3
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifdef ARDUINO_ARCH_STM32
#include <Arduino.h>
#include <tusb.h>
#include <stm32f0xx.h>
#include "../Comm.hpp"

#define I2S_PIN_WS GPIO_PIN_4
#define I2S_PIN_CK GPIO_PIN_5
#define I2S_PIN_SD GPIO_PIN_7

I2S_HandleTypeDef hi2s1;
DMA_HandleTypeDef hdma_spi1_rx;

#define AUDIO_BUFFER_SIZE 192
static uint16_t buffer[AUDIO_BUFFER_SIZE];

void
I2sDriver_Start(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Channel2_3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = I2S_PIN_WS | I2S_PIN_CK | I2S_PIN_SD;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF0_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    hdma_spi1_rx.Instance = DMA1_Channel2;
    hdma_spi1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_spi1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_spi1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_spi1_rx.Init.Mode = DMA_CIRCULAR;
    hdma_spi1_rx.Init.Priority = DMA_PRIORITY_LOW;
    HAL_DMA_Init(&hdma_spi1_rx);

    __HAL_LINKDMA(&hi2s1, hdmarx, hdma_spi1_rx);

    HAL_NVIC_SetPriority(SPI1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);

    hi2s1.Instance = SPI1;
    hi2s1.Init.Mode = I2S_MODE_SLAVE_RX;
    hi2s1.Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s1.Init.DataFormat = I2S_DATAFORMAT_16B;
    hi2s1.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
    hi2s1.Init.AudioFreq = I2S_AUDIOFREQ_48K;
    hi2s1.Init.CPOL = I2S_CPOL_HIGH;

    __disable_irq();
    /* TODO: Add timeout */
    while ((GPIOA->IDR & I2S_PIN_WS) == 1);
    while ((GPIOA->IDR & I2S_PIN_WS) == 0);
    /* STM32F072 Errata 2.13.4: Enable I2S when the WS is high */
    HAL_I2S_Init(&hi2s1);
    HAL_I2S_Receive_DMA(&hi2s1, buffer, AUDIO_BUFFER_SIZE);
    __enable_irq();
}

void
I2sDriver_Stop(void)
{
    HAL_I2S_DMAStop(&hi2s1);
    HAL_I2S_DeInit(&hi2s1);
    __HAL_RCC_SPI1_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
    HAL_DMA_DeInit(hi2s1.hdmarx);
    HAL_GPIO_DeInit(GPIOA, I2S_PIN_WS | I2S_PIN_CK | I2S_PIN_SD);
    HAL_NVIC_DisableIRQ(DMA1_Channel2_3_IRQn);
}

extern "C" void
DMA1_Channel2_3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_spi1_rx);
}

extern "C" void
SPI1_IRQHandler(void)
{
    HAL_I2S_IRQHandler(&hi2s1);
}

extern "C" void
HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    tud_audio_write(buffer, AUDIO_BUFFER_SIZE);
}

extern "C" void
HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    tud_audio_write(buffer + (AUDIO_BUFFER_SIZE / 2), AUDIO_BUFFER_SIZE);
}

#endif /* ARDUINO_ARCH_STM32 */
