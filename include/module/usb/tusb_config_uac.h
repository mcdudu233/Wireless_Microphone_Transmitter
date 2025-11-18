/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "usb_descriptors.h"

#define CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE 192000

// How many formats are used, need to adjust USB descriptor if changed
#define CFG_TUD_AUDIO_FUNC_1_N_FORMATS 3

// 16bit in 16bit slots
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX 2
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX 16

// 24bit in 24bit slots
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_2_N_BYTES_PER_SAMPLE_TX 3
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_2_RESOLUTION_RX 24

// 32bit in 32bit slots
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_3_N_BYTES_PER_SAMPLE_TX 4
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_3_RESOLUTION_RX 32

// Have a look into audio_device.h for all configurations
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN TUD_AUDIO_MIC_TWO_CH_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT 1     // Number of Standard AS Interface Descriptors (4.9.1) defined per audio function - this is required to be able to remember the current alternate settings of these interfaces - We restrict us here to have a constant number for all audio functions (which means this has to be the maximum number of AS interfaces an audio function has and a second audio function with less AS interfaces just wastes a few bytes)
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ 64 // Size of control request buffer

#define CFG_TUD_AUDIO_ENABLE_EP_IN 1
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX 2 // Driver gets this info from the descriptors - we define it here to use it to setup the descriptors and to do calculations with it below - be aware: for different number of channels you need another descriptor!

  // #define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)
  // #define CFG_TUD_AUDIO_FUNC_1_FORMAT_2_EP_SZ_IN TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_FORMAT_2_N_BYTES_PER_SAMPLE_TX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)
  // #define CFG_TUD_AUDIO_FUNC_1_FORMAT_3_EP_SZ_IN TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_3_MAX_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_FORMAT_3_N_BYTES_PER_SAMPLE_TX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)

// TODO 无法显示所有音频频率
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN (1000)
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_2_EP_SZ_IN (1000)
#define CFG_TUD_AUDIO_FUNC_1_FORMAT_3_EP_SZ_IN (1000)

#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX TU_MAX(TU_MAX(CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN, CFG_TUD_AUDIO_FUNC_1_FORMAT_2_EP_SZ_IN), CFG_TUD_AUDIO_FUNC_1_FORMAT_3_EP_SZ_IN) // Maximum EP IN size for all AS alternate settings used
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX

#ifdef __cplusplus
}
#endif
