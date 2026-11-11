/*
 * PROJECT:     LiberNT Apple ISP Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Registers, boot exchange and command layouts of the Apple silicon ISP firmware
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define ISP_COPROC_FABRIC_0             0x00000738
#define ISP_COPROC_FABRIC_1             0x00000798
#define ISP_COPROC_FABRIC_2             0x000007f8
#define ISP_COPROC_FABRIC_3             0x00000858
#define ISP_COPROC_BUSY_0               0x00000818
#define ISP_COPROC_BUSY_1               0x0000081c
#define ISP_COPROC_EDPRCR               0x01010310
#define ISP_COPROC_CONTROL              0x01400044
#define ISP_COPROC_STATUS               0x01400048
#define ISP_COPROC_IRQ_MASK_0           0x01400a00
#define ISP_COPROC_IRQ_MASK_COUNT       6
#define ISP_COPROC_VERSION              0x01800000

#define ISP_COPROC_WINDOW_LOW           0x00000000
#define ISP_COPROC_WINDOW_DEBUG         0x01010000
#define ISP_COPROC_WINDOW_CONTROL       0x01400000
#define ISP_COPROC_WINDOW_VERSION       0x01800000
#define ISP_COPROC_WINDOW_SIZE          0x1000

#define ISP_COPROC_FABRIC_VALUE         0x00ff00ff
#define ISP_COPROC_EDPRCR_VALUE         0x2
#define ISP_COPROC_CONTROL_RUN          0x10
#define ISP_COPROC_STATUS_WFI           0x3

#define ISP_MBOX_IRQ_INTERRUPT          0x00
#define ISP_MBOX2_IRQ_DOORBELL          0x00
#define ISP_MBOX2_IRQ_ACK               0x0c
#define ISP_MBOX_IRQ_ENABLE_ALL         0xf

#define ISP_GPIO_0                      0x00
#define ISP_GPIO_1                      0x04
#define ISP_GPIO_3                      0x0c
#define ISP_GPIO_7                      0x1c
#define ISP_GPIO_COUNT                  8
#define ISP_GPIO_CLOCK_EN               0x20

#define ISP_MAGIC_READY                 0x08042006
#define ISP_MAGIC_ARGS                  0xf7fbdff9
#define ISP_MAGIC_IRQ_OFF               0xfeedbabe

#define ISP_IPC_SIZE                    0x1c000
#define ISP_DATA_SIZE                   0x28000
#define ISP_IPC_MESSAGE_SIZE            64
#define ISP_IPC_FLAG_ACK                0x1
#define ISP_IPC_FLAG_TERMINAL_ACK       0x3
#define ISP_IPC_ARGS_GAP                0x40
#define ISP_IPC_CHAN_COMMAND            0
#define ISP_IPC_CHAN_REPLY              1
#define ISP_IPC_CHAN_REPORT             2

#define ISP_BUFFER_EXCHANGE_SIZE        0x280
#define ISP_BUFFER_FLAG_RENDER          0x10000000
#define ISP_BUFFER_FLAG_COMMAND         0x30000000
#define ISP_BUFFER_FLAG_ACK             0x80000000
#define ISP_BUFFER_PLANE_FLAG           0x40000000
#define ISP_BUFFER_LIST_TYPE            1

#define ISP_SURFACE_TYPE_LOG            0x004c4f47
#define ISP_SURFACE_TYPE_MISC           0x4d495343
#define ISP_SURFACE_BUFFER_LIST_SIZE    0xc000

#define ISP_OPCODE(Code)                (((ULONG64)(Code)) << 32)

#define CISP_CMD_START                              0x0000
#define CISP_CMD_CONFIG_GET                         0x0003
#define CISP_CMD_PRINT_ENABLE                       0x0004
#define CISP_CMD_SET_ISP_PMU_BASE                   0x0011
#define CISP_CMD_PMP_CTRL_SET                       0x001c
#define CISP_CMD_SUSPEND                            0x0021
#define CISP_CMD_FLICKER_SENSOR_SET                 0x0024
#define CISP_CMD_CH_START                           0x0100
#define CISP_CMD_CH_STOP                            0x0101
#define CISP_CMD_CH_BUFFER_RETURN                   0x0104
#define CISP_CMD_CH_CAMERA_CONFIG_GET               0x0106
#define CISP_CMD_CH_CAMERA_CONFIG_SELECT            0x0107
#define CISP_CMD_CH_INFO_GET                        0x010d
#define CISP_CMD_CH_BUFFER_RECYCLE_MODE_SET         0x010e
#define CISP_CMD_CH_BUFFER_RECYCLE_START            0x010f
#define CISP_CMD_CH_SET_FILE_LOAD                   0x0111
#define CISP_CMD_CH_SIF_PIXEL_FORMAT_SET            0x0115
#define CISP_CMD_CH_BUFFER_POOL_CONFIG_SET          0x0117
#define CISP_CMD_CH_SBS_ENABLE                      0x013b
#define CISP_CMD_CH_AE_FRAME_RATE_MAX_SET           0x0208
#define CISP_CMD_CH_AE_FRAME_RATE_MIN_SET           0x020a
#define CISP_CMD_CH_AE_STABILITY_SET                0x021a
#define CISP_CMD_CH_AE_STABILITY_TO_STABLE_SET      0x0229
#define CISP_CMD_CH_CROP_SCL1_SET                   0x080c
#define CISP_CMD_CH_CNR_START                       0x0a2f
#define CISP_CMD_CH_MBNR_ENABLE                     0x0a3a
#define CISP_CMD_CH_OUTPUT_CONFIG_SCL1_SET          0x0b09
#define CISP_CMD_CH_PREVIEW_STREAM_SET              0x0b0d
#define CISP_CMD_SET_DSID_CLR_REG_BASE              0x3205
#define CISP_CMD_APPLE_CH_AE_METERING_MODE_SET      0x8206
#define CISP_CMD_APPLE_CH_AE_FD_SCENE_METERING_SET  0x820e
#define CISP_CMD_APPLE_CH_TEMPORAL_FILTER_START     0xc100
#define CISP_CMD_APPLE_CH_MOTION_HISTORY_START      0xc102
#define CISP_CMD_APPLE_CH_TEMPORAL_FILTER_ENABLE    0xc113

#define CISP_POOL_TYPE_META                         0x0
#define CISP_POOL_TYPE_META_CAPTURE                 0x8
#define CISP_POOL_TYPE_RENDERED_SCL1                0x9
#define CISP_COLORSPACE_REC709                      0x1
#define CISP_OUTPUT_FORMAT_YUV_2PLANE               0x0
#define CISP_BUFFER_RECYCLE_MODE_EMPTY_ONLY         0x1
#define CISP_MBNR_MODE_ENABLE                       0x1
#define CISP_AE_METERING_MODE                       3
#define CISP_AE_STABILITY                           32
#define CISP_AE_STABILITY_TO_STABLE                 20
#define CISP_FRAME_RATE_MAX                         7680
#define CISP_FRAME_RATE_MIN                         3840
#define CISP_SIF_FORMAT                             3
#define CISP_SIF_TYPE                               1

typedef struct _ISP_MESSAGE
{
    ULONG64 Argument[8];
} ISP_MESSAGE, *PISP_MESSAGE;

#pragma pack(push, 1)

typedef struct _ISP_BOOT_ARGUMENTS
{
    ULONG Reserved00[2];
    ULONG64 IpcIova;
    ULONG64 SharedBase;
    ULONG64 SharedSize;
    ULONG64 ExtraIova;
    ULONG64 ExtraSize;
    ULONG PlatformId;
    ULONG Reserved34;
    ULONG64 LogBufferIova;
    ULONG64 LogBufferSize;
    ULONG64 LogEntrySize;
    ULONG IpcSize;
    ULONG Reserved54[5];
    ULONG Value68;
    ULONG Reserved6C[13];
    ULONG ReservedA0;
    ULONG ValueA4;
    ULONG ReservedA8[5];
    ULONG ValueBC;
    ULONG ReservedC0[47];
    ULONG Value17C;
} ISP_BOOT_ARGUMENTS, *PISP_BOOT_ARGUMENTS;

#define ISP_BOOT_VALUE_68               0x40
#define ISP_BOOT_VALUE_A4               0x1
#define ISP_BOOT_VALUE_17C              0x3
#define ISP_BOOT_VALUE_BC_BACK          0xc

typedef struct _ISP_CHANNEL_DESCRIPTOR
{
    CHAR Name[64];
    ULONG Type;
    ULONG Source;
    ULONG Count;
    ULONG Reserved4C;
    ULONG64 Iova;
    ULONG Reserved58[42];
} ISP_CHANNEL_DESCRIPTOR, *PISP_CHANNEL_DESCRIPTOR;

typedef struct _ISP_CMD_OPCODE
{
    ULONG64 Opcode;
} ISP_CMD_OPCODE, *PISP_CMD_OPCODE;

typedef struct _ISP_CMD_VALUE
{
    ULONG64 Opcode;
    ULONG Value;
} ISP_CMD_VALUE, *PISP_CMD_VALUE;

typedef struct _ISP_CMD_CHANNEL
{
    ULONG64 Opcode;
    ULONG Channel;
} ISP_CMD_CHANNEL, *PISP_CMD_CHANNEL;

typedef struct _ISP_CMD_CHANNEL_VALUE
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG Value;
} ISP_CMD_CHANNEL_VALUE, *PISP_CMD_CHANNEL_VALUE;

typedef struct _ISP_CMD_CONFIG
{
    ULONG64 Opcode;
    ULONG TimestampFrequency;
    ULONG ChannelCount;
    ULONG Reserved10[3];
} ISP_CMD_CONFIG, *PISP_CMD_CONFIG;

typedef struct _ISP_CMD_ADDRESS
{
    ULONG64 Opcode;
    ULONG64 Address;
} ISP_CMD_ADDRESS, *PISP_CMD_ADDRESS;

typedef struct _ISP_CMD_DSID_CLEAR
{
    ULONG64 Opcode;
    ULONG64 Base;
    ULONG Range;
} ISP_CMD_DSID_CLEAR, *PISP_CMD_DSID_CLEAR;

typedef struct _ISP_CMD_PMP_CONTROL
{
    ULONG64 Opcode;
    ULONG64 ClockScratch;
    ULONG64 ClockBase;
    UCHAR ClockBit;
    UCHAR ClockSize;
    USHORT ClockPad;
    ULONG64 BandwidthScratch;
    ULONG64 BandwidthBase;
    UCHAR BandwidthBit;
    UCHAR BandwidthSize;
    USHORT BandwidthPad;
} ISP_CMD_PMP_CONTROL, *PISP_CMD_PMP_CONTROL;

typedef struct _ISP_CMD_CHANNEL_INFO
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG Reserved0C[5];
    ULONG Version;
    ULONG Reserved24[15];
    ULONG PresetCount;
    ULONG Reserved64[13];
    ULONG TimestampFrequency;
    USHORT Reserved9C;
    CHAR ModuleSerial[20];
    USHORT ReservedB2;
    ULONG ReservedB4[25];
} ISP_CMD_CHANNEL_INFO, *PISP_CMD_CHANNEL_INFO;

typedef struct _ISP_CMD_CAMERA_CONFIG
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG Preset;
    USHORT InputWidth;
    USHORT InputHeight;
    USHORT OutputWidth;
    USHORT OutputHeight;
    ULONG Reserved18[18];
    ULONG SensorClock;
    ULONG Reserved64[4];
    ULONG TimestampFrequency;
    ULONG Reserved78[25];
} ISP_CMD_CAMERA_CONFIG, *PISP_CMD_CAMERA_CONFIG;

typedef struct _ISP_CMD_CAMERA_SELECT
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG Preset;
} ISP_CMD_CAMERA_SELECT, *PISP_CMD_CAMERA_SELECT;

typedef struct _ISP_CMD_FILE_LOAD
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG64 Address;
    ULONG Size;
} ISP_CMD_FILE_LOAD, *PISP_CMD_FILE_LOAD;

typedef struct _ISP_CMD_CROP
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG X;
    ULONG Y;
    ULONG Width;
    ULONG Height;
} ISP_CMD_CROP, *PISP_CMD_CROP;

typedef struct _ISP_CMD_OUTPUT_CONFIG
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG Width;
    ULONG Height;
    ULONG Colorspace;
    ULONG Format;
    ULONG Stride[3];
    ULONG PaddingRows;
    ULONG HeightAgain;
    ULONG Compress;
    ULONG WidthAgain;
} ISP_CMD_OUTPUT_CONFIG, *PISP_CMD_OUTPUT_CONFIG;

typedef struct _ISP_CMD_MBNR
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG UseCase;
    ULONG Mode;
    ULONG EnableChroma;
} ISP_CMD_MBNR, *PISP_CMD_MBNR;

typedef struct _ISP_CMD_SIF_FORMAT
{
    ULONG64 Opcode;
    ULONG Channel;
    UCHAR Format;
    UCHAR Type;
    USHORT Compress;
    ULONG Reserved10;
} ISP_CMD_SIF_FORMAT, *PISP_CMD_SIF_FORMAT;

typedef struct _ISP_CMD_FD_SCENE_METERING
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG Value[6];
} ISP_CMD_FD_SCENE_METERING, *PISP_CMD_FD_SCENE_METERING;

typedef struct _ISP_CMD_TEMPORAL_FILTER
{
    ULONG64 Opcode;
    ULONG Channel;
    ULONG Enable;
    ULONG Filter;
} ISP_CMD_TEMPORAL_FILTER, *PISP_CMD_TEMPORAL_FILTER;

typedef struct _ISP_CMD_POOL_CONFIG
{
    ULONG64 Opcode;
    ULONG Channel;
    USHORT Type;
    USHORT Count;
    ULONG MetaSize0;
    ULONG MetaSize1;
    ULONG64 Reserved18[3];
    ULONG Reserved30[25];
    ULONG DataBlocks;
    ULONG Compress;
} ISP_CMD_POOL_CONFIG, *PISP_CMD_POOL_CONFIG;

typedef struct _ISP_BUFFER_ENTRY
{
    ULONG64 Iova[4];
    ULONG Flags[4];
    ULONG PlaneCount;
    ULONG PoolType;
    ULONG Tag;
    ULONG Reserved3C;
} ISP_BUFFER_ENTRY, *PISP_BUFFER_ENTRY;

typedef struct _ISP_BUFFER_LIST
{
    ULONG64 Type;
    ULONG64 Count;
    ISP_BUFFER_ENTRY Entry[ANYSIZE_ARRAY];
} ISP_BUFFER_LIST, *PISP_BUFFER_LIST;

#pragma pack(pop)

#define ISP_FD_SCENE_METERING_VALUES    { 0xb8, 0x2000200, 0x280800, 0xe10028, 0xa0399, 0x3cc02cc }

C_ASSERT(sizeof(ISP_MESSAGE) == ISP_IPC_MESSAGE_SIZE);
C_ASSERT(sizeof(ISP_BOOT_ARGUMENTS) == 0x180);
C_ASSERT(FIELD_OFFSET(ISP_BOOT_ARGUMENTS, IpcIova) == 0x08);
C_ASSERT(FIELD_OFFSET(ISP_BOOT_ARGUMENTS, PlatformId) == 0x30);
C_ASSERT(FIELD_OFFSET(ISP_BOOT_ARGUMENTS, IpcSize) == 0x50);
C_ASSERT(FIELD_OFFSET(ISP_BOOT_ARGUMENTS, Value68) == 0x68);
C_ASSERT(FIELD_OFFSET(ISP_BOOT_ARGUMENTS, ValueA4) == 0xa4);
C_ASSERT(FIELD_OFFSET(ISP_BOOT_ARGUMENTS, ValueBC) == 0xbc);
C_ASSERT(FIELD_OFFSET(ISP_BOOT_ARGUMENTS, Value17C) == 0x17c);
C_ASSERT(sizeof(ISP_CHANNEL_DESCRIPTOR) == 0x100);
C_ASSERT(FIELD_OFFSET(ISP_CHANNEL_DESCRIPTOR, Iova) == 0x50);
C_ASSERT(sizeof(ISP_CMD_OPCODE) == 0x8);
C_ASSERT(sizeof(ISP_CMD_VALUE) == 0xc);
C_ASSERT(sizeof(ISP_CMD_CHANNEL) == 0xc);
C_ASSERT(sizeof(ISP_CMD_CHANNEL_VALUE) == 0x10);
C_ASSERT(sizeof(ISP_CMD_CONFIG) == 0x1c);
C_ASSERT(sizeof(ISP_CMD_ADDRESS) == 0x10);
C_ASSERT(sizeof(ISP_CMD_DSID_CLEAR) == 0x14);
C_ASSERT(sizeof(ISP_CMD_PMP_CONTROL) == 0x30);
C_ASSERT(sizeof(ISP_CMD_CHANNEL_INFO) == 0x118);
C_ASSERT(FIELD_OFFSET(ISP_CMD_CHANNEL_INFO, Version) == 0x20);
C_ASSERT(FIELD_OFFSET(ISP_CMD_CHANNEL_INFO, PresetCount) == 0x60);
C_ASSERT(FIELD_OFFSET(ISP_CMD_CHANNEL_INFO, TimestampFrequency) == 0x98);
C_ASSERT(FIELD_OFFSET(ISP_CMD_CHANNEL_INFO, ModuleSerial) == 0x9e);
C_ASSERT(sizeof(ISP_CMD_CAMERA_CONFIG) == 0xdc);
C_ASSERT(FIELD_OFFSET(ISP_CMD_CAMERA_CONFIG, InputWidth) == 0x10);
C_ASSERT(FIELD_OFFSET(ISP_CMD_CAMERA_CONFIG, SensorClock) == 0x60);
C_ASSERT(FIELD_OFFSET(ISP_CMD_CAMERA_CONFIG, TimestampFrequency) == 0x74);
C_ASSERT(sizeof(ISP_CMD_CAMERA_SELECT) == 0x10);
C_ASSERT(sizeof(ISP_CMD_FILE_LOAD) == 0x18);
C_ASSERT(sizeof(ISP_CMD_CROP) == 0x1c);
C_ASSERT(sizeof(ISP_CMD_OUTPUT_CONFIG) == 0x38);
C_ASSERT(sizeof(ISP_CMD_MBNR) == 0x18);
C_ASSERT(sizeof(ISP_CMD_SIF_FORMAT) == 0x14);
C_ASSERT(sizeof(ISP_CMD_FD_SCENE_METERING) == 0x24);
C_ASSERT(sizeof(ISP_CMD_TEMPORAL_FILTER) == 0x14);
C_ASSERT(sizeof(ISP_CMD_POOL_CONFIG) == 0x9c);
C_ASSERT(FIELD_OFFSET(ISP_CMD_POOL_CONFIG, DataBlocks) == 0x94);
C_ASSERT(sizeof(ISP_BUFFER_ENTRY) == 0x40);
C_ASSERT(FIELD_OFFSET(ISP_BUFFER_LIST, Entry) == 0x10);
