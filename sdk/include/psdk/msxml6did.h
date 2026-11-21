/*
 * Copyright (C) 2010 Nikolay Sivov for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __MSXML6DID_H__
#define __MSXML6DID_H__

#include <msxml2did.h>

#ifndef DISPID_XMLDSIG
#define DISPID_XMLDSIG 0x00000001
#endif

#define DISPID_XMLDSIG_CREATEKEYFROMCSP 1
#define DISPID_XMLDSIG_CREATEKEYFROMHMACSECRET 2
#define DISPID_XMLDSIG_CREATEKEYFROMNODE 3
#define DISPID_XMLDSIG_CREATESAXPROXY 4
#define DISPID_XMLDSIG_GETVERIFYINGCERTIFICATE 5
#define DISPID_XMLDSIG_SETREFERENCEDATA 6
#define DISPID_XMLDSIG_SIGN 7
#define DISPID_XMLDSIG_SIGNATURE 8
#define DISPID_XMLDSIG_STORE 9
#define DISPID_XMLDSIG_VERIFY 0xA

#endif /* __MSXML6DID_H__ */
