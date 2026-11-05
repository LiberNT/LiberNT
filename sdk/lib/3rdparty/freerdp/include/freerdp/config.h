#ifndef FREERDP_CONFIG_H
#define FREERDP_CONFIG_H

#include <winpr/config.h>

/* Include files */
/* #undef FREERDP_HAVE_VALGRIND_MEMCHECK_H */

/* Features */
/* #undef SWRESAMPLE_FOUND */
/* #undef AVRESAMPLE_FOUND */

/* Options */
/** If set the rdpSettings struct is opaque and internals can only be accessed
 *  through getters/setters
 *
 *  \since version 3.0.0
 */
/* #undef WITH_OPAQUE_SETTINGS */

/* #undef WITH_ADD_PLUGIN_TO_RPATH */
/* #undef WITH_PROFILER */
/* #undef WITH_GPROF */
/* #undef WITH_SIMD */
/* #undef WITH_AVX2 */
/* #undef WITH_CUPS */
/* #undef WITH_JPEG */
/* #undef WITH_WIN8 */
/* #undef WITH_AAD */
/* #undef WITH_CAIRO */
/* #undef WITH_SWSCALE */
/* #undef WITH_SWSCALE_LOADING */
/* #undef WITH_RDPSND_DSOUND */

/* #undef WITH_WINMM */
/* #undef WITH_MACAUDIO */
/* #undef WITH_OSS */
/* #undef WITH_ALSA */
/* #undef WITH_PULSE */
/* #undef WITH_IOSAUDIO */
/* #undef WITH_OPENSLES */
/* #undef WITH_GSM */
/* #undef WITH_LAME */
/** If defined Opus codec support is available.
 *
 *  \since version 3.0.0
 */
/* #undef WITH_OPUS */
/* #undef WITH_FAAD2 */
/* #undef WITH_FAAC */
/* #undef WITH_SOXR */
/* #undef WITH_GFX_H264 */
/* #undef WITH_OPENH264 */
/* #undef WITH_OPENH264_LOADING */
/* #undef WITH_VIDEO_FFMPEG */
/* #undef WITH_DSP_EXPERIMENTAL */
/* #undef WITH_DSP_FFMPEG */
/* #undef WITH_OPENCL */
/* #undef WITH_MEDIA_FOUNDATION */
/* #undef WITH_MEDIACODEC */
/* #undef WITH_GFX_AV1 */

/* #undef WITH_FFMPEG_HWACCEL */

#define WITH_CHANNELS
/* #undef WITH_CLIENT_CHANNELS */
#define WITH_SERVER_CHANNELS

/* #undef WITH_CHANNEL_GFXREDIR */
/* #undef WITH_CHANNEL_RDPAPPLIST */

/* Plugins */
/* #undef WITH_RDPDR */

/* Channels */
/* #undef CHANNEL_AINPUT */
/* #undef CHANNEL_AINPUT_CLIENT */
/* #undef CHANNEL_AINPUT_SERVER */
/* #undef CHANNEL_AUDIN */
/* #undef CHANNEL_AUDIN_CLIENT */
/* #undef CHANNEL_AUDIN_SERVER */
/* #undef CHANNEL_CLIPRDR */
/* #undef CHANNEL_CLIPRDR_CLIENT */
/* #undef CHANNEL_CLIPRDR_SERVER */
#define CHANNEL_DISP
/* #undef CHANNEL_DISP_CLIENT */
#define CHANNEL_DISP_SERVER
#define CHANNEL_DRDYNVC
#define CHANNEL_DRDYNVC_CLIENT
#define CHANNEL_DRDYNVC_SERVER
/* #undef CHANNEL_DRIVE */
/* #undef CHANNEL_DRIVE_CLIENT */
/* #undef CHANNEL_DRIVE_SERVER */

/* #undef CHANNEL_ECHO */
/* #undef CHANNEL_ECHO_CLIENT */
/* #undef CHANNEL_ECHO_SERVER */
/* #undef CHANNEL_ENCOMSP */
/* #undef CHANNEL_ENCOMSP_CLIENT */
/* #undef CHANNEL_ENCOMSP_SERVER */
/* #undef CHANNEL_GEOMETRY */
/* #undef CHANNEL_GEOMETRY_CLIENT */
/* #undef CHANNEL_GEOMETRY_SERVER */
/* #undef CHANNEL_GFXREDIR */
/* #undef CHANNEL_GFXREDIR_CLIENT */
/* #undef CHANNEL_GFXREDIR_SERVER */
/** If defined location channel support is available.
 *
 *  \since version 3.0.0
 */
/* #undef CHANNEL_LOCATION */
/** If defined location client side channel support is available.
 *
 *  \since version 3.0.0
 */
/* #undef CHANNEL_LOCATION_CLIENT */
/** If defined location server side channel support is available.
 *
 *  \since version 3.0.0
 */
/* #undef CHANNEL_LOCATION_SERVER */
/* #undef CHANNEL_PARALLEL */
/* #undef CHANNEL_PARALLEL_CLIENT */
/* #undef CHANNEL_PARALLEL_SERVER */
/* #undef CHANNEL_PRINTER */
/* #undef CHANNEL_PRINTER_CLIENT */
/* #undef CHANNEL_PRINTER_SERVER */
/* #undef CHANNEL_RAIL */
/* #undef CHANNEL_RAIL_CLIENT */
/* #undef CHANNEL_RAIL_SERVER */
/* #undef CHANNEL_RDPAPPLIST */
/* #undef CHANNEL_RDPAPPLIST_CLIENT */
/* #undef CHANNEL_RDPAPPLIST_SERVER */
#define CHANNEL_RDPDR
#define CHANNEL_RDPDR_CLIENT
#define CHANNEL_RDPDR_SERVER
/* #undef CHANNEL_RDPECAM */
/* #undef CHANNEL_RDPECAM_CLIENT */
/* #undef CHANNEL_RDPECAM_SERVER */
/* #undef CHANNEL_RDPEAR */
/* #undef CHANNEL_RDPEAR_CLIENT */
/* #undef CHANNEL_RDPEAR_SERVER */
/* #undef CHANNEL_RDPEWA */
/* #undef CHANNEL_RDPEWA_CLIENT */
/* #undef CHANNEL_RDPEWA_SERVER */
/* #undef CHANNEL_RDPEI */
/* #undef CHANNEL_RDPEI_CLIENT */
/* #undef CHANNEL_RDPEI_SERVER */
/* #undef CHANNEL_RDPGFX */
/* #undef CHANNEL_RDPGFX_CLIENT */
/* #undef CHANNEL_RDPGFX_SERVER */
/** If defined mouse cursor channel support is available.
 *
 *  \since version 3.0.0
 */
/* #undef CHANNEL_RDPEMSC */

/** If defined mouse cursor channel support is available.
 *
 *  \since version 3.0.0
 */
/* #undef CHANNEL_RDPEMSC_CLIENT */

/** If defined mouse cursor channel support is available.
 *
 *  \since version 3.0.0
 */
/* #undef CHANNEL_RDPEMSC_SERVER */
/* #undef CHANNEL_RDPSND */
/* #undef CHANNEL_RDPSND_CLIENT */
/* #undef CHANNEL_RDPSND_SERVER */
/* #undef CHANNEL_REMDESK */
/* #undef CHANNEL_REMDESK_CLIENT */
/* #undef CHANNEL_REMDESK_SERVER */
/* #undef CHANNEL_SERIAL */
/* #undef CHANNEL_SERIAL_CLIENT */
/* #undef CHANNEL_SERIAL_SERVER */
/* #undef CHANNEL_SMARTCARD */
/* #undef CHANNEL_SMARTCARD_CLIENT */
/* #undef CHANNEL_SMARTCARD_SERVER */
/* #undef CHANNEL_SSHAGENT */
/* #undef CHANNEL_SSHAGENT_CLIENT */
/* #undef CHANNEL_SSHAGENT_SERVER */
/* #undef CHANNEL_TELEMETRY */
/* #undef CHANNEL_TELEMETRY_CLIENT */
/* #undef CHANNEL_TELEMETRY_SERVER */
/* #undef CHANNEL_TSMF */
/* #undef CHANNEL_TSMF_CLIENT */
/* #undef CHANNEL_TSMF_SERVER */
/* #undef CHANNEL_URBDRC */
/* #undef CHANNEL_URBDRC_CLIENT */
/* #undef CHANNEL_URBDRC_SERVER */
/* #undef CHANNEL_VIDEO */
/* #undef CHANNEL_VIDEO_CLIENT */
/* #undef CHANNEL_VIDEO_SERVER */

/* Debug */
/* #undef WITH_DEBUG_CERTIFICATE */
/* #undef WITH_DEBUG_CAPABILITIES */
/* #undef WITH_DEBUG_CHANNELS */
/* #undef WITH_DEBUG_CLIPRDR */
/* #undef WITH_DEBUG_CODECS */
/* #undef WITH_DEBUG_RDPGFX */
/* #undef WITH_DEBUG_DVC */
/* #undef WITH_DEBUG_TSMF */
/* #undef WITH_DEBUG_KBD */
/* #undef WITH_DEBUG_LICENSE */
/* #undef WITH_DEBUG_NEGO */
/* #undef WITH_DEBUG_NLA */
/* #undef WITH_DEBUG_TSG */
/* #undef WITH_DEBUG_RAIL */
/* #undef WITH_DEBUG_RDP */
/* #undef WITH_DEBUG_REDIR */
/* #undef WITH_DEBUG_RDPDR */
/* #undef WITH_DEBUG_RFX */
/* #undef WITH_DEBUG_SCARD */
/* #undef WITH_DEBUG_SND */
/* #undef WITH_DEBUG_SVC */
/* #undef WITH_DEBUG_RDPEI */
/* #undef WITH_DEBUG_TIMEZONE */
/* #undef WITH_DEBUG_URBDRC */
/* #undef WITH_DEBUG_TRANSPORT */
/* #undef WITH_DEBUG_WND */
/* #undef WITH_DEBUG_RINGBUFFER */

/* Proxy */
/* #undef WITH_PROXY_MODULES */
/* #undef WITH_PROXY_EMULATE_SMARTCARD */

/** If defined linux/vm_sockets.h support is available.
 *
 *  \since version 3.0.0
 */
/* #undef HAVE_AF_VSOCK_H */

/** If library is build without these do permanently hide symbols
 *
 * \since version 3.17.2
 */
#if !defined(WITHOUT_FREERDP_3x_DEPRECATED)
/* #undef WITHOUT_FREERDP_3x_DEPRECATED */
#endif

/** Build FILE_DIRECTORY_INFORMATION::FileName with type WCHAR instead of char
 *
 * @since version 3.20.0
 */
/* #undef WITH_WCHAR_FILE_DIRECTORY_INFORMATION */

/** Enforce the TLS AEAD cryptographic data limit (RFC 8446 §5.5): on a TLS 1.3
 *  connection the traffic keys are rekeyed in place once a per-direction byte
 *  threshold is reached; if the rekey fails the connection is cut. On non-TLS-1.3
 *  connections a rekey is not possible and the connection is left as is. When
 *  undefined the whole feature is compiled out.
 * @since version 3.31.0
 */
/* #undef WITH_TLS_DATA_LIMIT */

#endif /* FREERDP_CONFIG_H */
