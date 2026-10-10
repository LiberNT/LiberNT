/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/checksum.c
 * PURPOSE:     Checksum routines
 * NOTES:       The checksum routine is from RFC 1071
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"


ULONG ChecksumFold(
  ULONG Sum)
{
  /* Fold 32-bit sum to 16 bits */
  while (Sum >> 16)
    {
      Sum = (Sum & 0xFFFF) + (Sum >> 16);
    }

  return Sum;
}

ULONG ChecksumCompute(
  PVOID Data,
  UINT Count,
  ULONG Seed)
/*
 * FUNCTION: Calculate checksum of a buffer
 * ARGUMENTS:
 *     Data  = Pointer to buffer with data
 *     Count = Number of bytes in buffer
 *     Seed  = Previously calculated checksum (if any)
 * RETURNS:
 *     Checksum of buffer
 */
{
  return Seed + tcpip_chksum(Data, (int)Count);
}

ULONG
UDPv4ChecksumCalculate(
  PIPv4_HEADER IPHeader,
  PUCHAR PacketBuffer,
  ULONG DataLength)
{
  ULONG Sum;

  Sum = tcpip_chksum(PacketBuffer, (int)DataLength);
  Sum += tcpip_chksum(&IPHeader->SrcAddr, sizeof(IPv4_RAW_ADDRESS));
  Sum += tcpip_chksum(&IPHeader->DstAddr, sizeof(IPv4_RAW_ADDRESS));
  Sum += WH2N(IPPROTO_UDP);
  Sum += WH2N((USHORT)DataLength);

  return ~(ULONG)WN2H((USHORT)ChecksumFold(Sum));
}

