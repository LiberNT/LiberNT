/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 frame offsets shared by the assembly and C code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define KI_VFP_FPSCR        0x004
#define KI_VFP_D            0x010
#define KI_VFP_SIZE         0x110

#define KI_EF_R4            0x014
#define KI_EF_R11           0x030
#define KI_EF_RETURN        0x034
#define KI_EF_SIZE          0x038

#define KI_TF_FAULT_STATUS  0x004
#define KI_TF_FAULT_ADDRESS 0x008
#define KI_TF_FLAGS         0x010
#define KI_TF_PREVIOUS_MODE 0x013
#define KI_TF_VFP_STATE     0x014
#define KI_TF_R0            0x060
#define KI_TF_R2            0x068
#define KI_TF_R12           0x070
#define KI_TF_SP            0x074
#define KI_TF_LR            0x078
#define KI_TF_R11           0x07C
#define KI_TF_PC            0x080
#define KI_TF_CPSR          0x084
#define KI_TF_SIZE          0x088

#define KI_REC_EF           KI_VFP_SIZE
#define KI_REC_TF           (KI_VFP_SIZE + KI_EF_SIZE)
#define KI_REC_SIZE         (KI_REC_TF + KI_TF_SIZE)

#define KI_VECTOR_UNDEFINED 1
#define KI_VECTOR_SVC       2
#define KI_VECTOR_PABORT    3
#define KI_VECTOR_DABORT    4
#define KI_VECTOR_IRQ       6
#define KI_VECTOR_FIQ       7

/* Switch frame: d8-d15, pad, r4-r11, lr. */
#define KI_SWITCH_FRAME_SIZE (8 * 8 + 4 + 9 * 4)
