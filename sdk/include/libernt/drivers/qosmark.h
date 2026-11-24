/*
 * PROJECT:     LiberNT qWAVE
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-only requests that mark a socket's traffic for a QoS flow
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _LIBERNT_QOSMARK_H_
#define _LIBERNT_QOSMARK_H_

/* The DSCP value of a socket that no flow marks */
#define QOS_MARK_UNMARKED               0xFFFFFFFF
#define QOS_MARK_DSCP_LAST              63

/* IOCTL_TCP_SET_INFORMATION_EX ids, IN: ULONG DSCP or QOS_MARK_UNMARKED; tcpip refuses them from user mode */
#define AO_OPTION_QOS_DSCP              0x00010000
#define TCP_SOCKET_QOS_DSCP             0x00010000

/* IRP_MJ_INTERNAL_DEVICE_CONTROL on an AFD socket, IN: ULONG DSCP or QOS_MARK_UNMARKED */
#define IOCTL_AFD_SET_QOS_DSCP \
    CTL_CODE(FILE_DEVICE_NETWORK, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

#endif /* _LIBERNT_QOSMARK_H_ */
