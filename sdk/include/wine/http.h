/*
 * Copyright 2019 Zebediah Figura
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

#ifndef __WINE_WINE_HTTP_H
#define __WINE_WINE_HTTP_H

#include <windef.h>
#include <http.h>
#ifndef CTL_CODE
#include <winioctl.h>
#endif

#define IOCTL_HTTP_ADD_URL          CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, 0)
#define IOCTL_HTTP_REMOVE_URL       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, 0)
#define IOCTL_HTTP_RECEIVE_REQUEST  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, 0)
#define IOCTL_HTTP_SEND_RESPONSE    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, 0)
#define IOCTL_HTTP_RECEIVE_BODY     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, 0)
#define IOCTL_HTTP_SET_QUEUE_PROPERTY   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, 0)
#define IOCTL_HTTP_QUERY_QUEUE_PROPERTY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, 0)

#define HTTP_VERSION_EA_NAME "HttpApiVersion"

struct http_version_ea
{
    ULONG next_entry_offset;
    UCHAR flags;
    UCHAR name_length;
    USHORT value_length;
    char name[sizeof(HTTP_VERSION_EA_NAME)];
    char value[sizeof(HTTPAPI_VERSION)];
};

struct http_add_url_params
{
    HTTP_URL_CONTEXT context;
    char url[1];
};

struct http_receive_request_params
{
    ULONGLONG addr; /* user-mode buffer address */
    HTTP_REQUEST_ID id;
    ULONG flags;
    ULONG bits;
};

struct http_response
{
    HTTP_REQUEST_ID id;
    ULONG response_flags;
    int len;
    ULONG sent;
    BOOL reset;
    char buffer[1];
};

struct http_queue_property
{
    ULONG property;
    ULONG value;
};

struct http_receive_body_params
{
    HTTP_REQUEST_ID id;
    ULONG bits;
    ULONG flags;
};

#endif
