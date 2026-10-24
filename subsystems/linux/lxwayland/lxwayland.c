/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Wayland compositor that shows Linux windows as LiberNT windows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxrt.h"
#include "wlproto.h"

typedef uint32_t ULONG;
typedef int32_t LONG;
#include "../include/lxwin.h"

#define LX_DEVICE               "/dev/lxwin"
#define LX_SOCKET_DIR           "/run/lxwayland"
#define LX_SOCKET_PATH          LX_SOCKET_DIR "/wayland-0"
#define LX_CLIPBOARD_LIMIT      (1024 * 1024 - 64)
#define LX_MAX_ARGS             16
#define LX_MAX_FDS              28
#define LX_MAX_MESSAGE          4096
#define LX_MAX_CLIENT_ID        (1u << 20)
#define LX_SERVER_ID_BASE       0xFF000000u
#define LX_MAX_OUTPUT           (16 * 1024 * 1024)
#define LX_MAX_DAMAGE           256
#define LX_MAX_ICON             256
#define LX_FRAME_MS             16
#define LX_DEVICE_READ          (2 * 1024 * 1024)

#define XDG_STATE_MAXIMIZED     1
#define XDG_STATE_FULLSCREEN    2
#define XDG_STATE_RESIZING      3
#define XDG_STATE_ACTIVATED     4
#define XDG_STATE_TILED         ((1u << 5) | (1u << 6) | (1u << 7) | (1u << 8))

#define MOD_SHIFT               0x01
#define MOD_LOCK                0x02
#define MOD_CONTROL             0x04
#define MOD_MOD1                0x08
#define MOD_MOD2                0x10
#define MOD_MOD4                0x40

enum
{
    ROLE_NONE,
    ROLE_TOPLEVEL,
    ROLE_POPUP,
    ROLE_SUBSURFACE,
    ROLE_CURSOR
};

enum
{
    WATCH_LISTENER,
    WATCH_DEVICE,
    WATCH_CLIENT,
    WATCH_CLIPBOARD_READ,
    WATCH_CLIPBOARD_WRITE
};

typedef struct _WATCH
{
    uint32_t Type;
    void *Object;
} WATCH;

typedef struct _WL_CLIENT WL_CLIENT;

typedef struct _WL_RESOURCE
{
    WL_CLIENT *Client;
    uint32_t Id;
    const WL_INTERFACE *Interface;
    uint32_t Version;
    uint32_t Refs;
    bool Alive;
    void *Data;
} WL_RESOURCE;

typedef union _WL_ARG
{
    int32_t I;
    uint32_t U;
    WL_RESOURCE *O;
    const char *S;
    int H;
    struct
    {
        const void *Data;
        uint32_t Size;
    } A;
} WL_ARG;

typedef struct _OUT_FD
{
    int Fd;
    size_t Offset;
} OUT_FD;

struct _WL_CLIENT
{
    WL_CLIENT *Next;
    WATCH Watch;
    int Socket;
    int Pid;
    uint32_t Refs;
    bool Closed;
    bool Failed;
    bool Writing;
    WL_RESOURCE **Objects;
    uint32_t ObjectCapacity;
    WL_RESOURCE **ServerObjects;
    uint32_t ServerCount;
    uint32_t ServerCapacity;
    uint32_t NextServerId;
    WL_RESOURCE *Display;
    uint8_t *In;
    size_t InLength;
    size_t InCapacity;
    int *InFds;
    uint32_t InFdCount;
    uint32_t InFdCapacity;
    uint8_t *Out;
    size_t OutLength;
    size_t OutCapacity;
    OUT_FD *OutFds;
    uint32_t OutFdCount;
    uint32_t OutFdCapacity;
};

typedef struct _RECT32
{
    int32_t X;
    int32_t Y;
    int32_t Width;
    int32_t Height;
} RECT32;

typedef struct _POOL
{
    uint32_t Refs;
    int Fd;
    int32_t Size;
    uint8_t *Map;
} POOL;

typedef struct _BUFFER
{
    POOL *Pool;
    int32_t Offset;
    int32_t Width;
    int32_t Height;
    int32_t Stride;
    uint32_t Format;
} BUFFER;

typedef struct _XDG_SURFACE XDG_SURFACE;
typedef struct _WINDOW WINDOW;

typedef struct _SURFACE
{
    WL_RESOURCE *Resource;
    WL_RESOURCE *PendingBuffer;
    bool PendingAttached;
    RECT32 PendingDamage[LX_MAX_DAMAGE];
    uint32_t PendingDamageCount;
    bool PendingDamageFull;
    WL_RESOURCE **PendingFrames;
    uint32_t PendingFrameCount;
    uint32_t PendingFrameCapacity;
    WL_RESOURCE *Buffer;
    int32_t Width;
    int32_t Height;
    uint32_t Role;
    XDG_SURFACE *Xdg;
    struct _SURFACE *Parent;
    struct _SURFACE **Children;
    uint32_t ChildCount;
    uint32_t ChildCapacity;
    int32_t SubX;
    int32_t SubY;
    bool PendingSubSet;
    int32_t PendingSubX;
    int32_t PendingSubY;
} SURFACE;

typedef struct _ICON_IMAGE
{
    uint32_t Size;
    uint8_t *Pixels;
} ICON_IMAGE;

typedef struct _ICON_SET
{
    uint32_t Count;
    ICON_IMAGE Images[8];
} ICON_SET;

typedef struct _TOPLEVEL
{
    WL_RESOURCE *Resource;
    XDG_SURFACE *Xdg;
    char *Title;
    char *AppId;
    int32_t MinWidth;
    int32_t MinHeight;
    int32_t MaxWidth;
    int32_t MaxHeight;
    WL_RESOURCE *Decoration;
    bool Decorated;
    int32_t Width;
    int32_t Height;
    uint32_t States;
    ICON_SET *Icon;
    ICON_SET *PendingIcon;
    bool IconPending;
} TOPLEVEL;

typedef struct _POSITIONER
{
    int32_t Width;
    int32_t Height;
    RECT32 Anchor;
    uint32_t AnchorEdge;
    uint32_t Gravity;
    int32_t OffsetX;
    int32_t OffsetY;
} POSITIONER;

typedef struct _POPUP
{
    WL_RESOURCE *Resource;
    XDG_SURFACE *Xdg;
    XDG_SURFACE *Parent;
    POSITIONER Positioner;
    bool Grab;
    RECT32 Geometry;
} POPUP;

struct _XDG_SURFACE
{
    WL_RESOURCE *Resource;
    SURFACE *Surface;
    uint32_t RoleType;
    WL_RESOURCE *Role;
    bool HasGeometry;
    RECT32 Geometry;
    bool HasPendingGeometry;
    RECT32 PendingGeometry;
    bool Configured;
    uint32_t Serial;
    WINDOW *Window;
};

struct _WINDOW
{
    WINDOW *Next;
    uint32_t Id;
    XDG_SURFACE *Xdg;
    bool Popup;
    int32_t Width;
    int32_t Height;
    uint8_t *Image;
};

typedef struct _DATA_SOURCE
{
    char **Mimes;
    uint32_t MimeCount;
    uint32_t MimeCapacity;
} DATA_SOURCE;

typedef struct _HOST_SOURCE
{
    uint32_t Refs;
    uint8_t *Text;
    size_t Length;
} HOST_SOURCE;

typedef struct _OFFER
{
    WL_RESOURCE *Source;
    HOST_SOURCE *Host;
} OFFER;

typedef struct _ICON_BUFFER
{
    WL_RESOURCE *Buffer;
    int32_t Scale;
} ICON_BUFFER;

typedef struct _ICON
{
    char *Name;
    ICON_BUFFER Buffers[16];
    uint32_t BufferCount;
    bool Immutable;
} ICON;

typedef struct _CLIPBOARD_READ
{
    WATCH Watch;
    int Fd;
    uint8_t *Data;
    size_t Length;
    size_t Capacity;
    WL_RESOURCE *Source;
} CLIPBOARD_READ;

typedef struct _CLIPBOARD_WRITE
{
    WATCH Watch;
    int Fd;
    uint8_t *Data;
    size_t Length;
    size_t Offset;
} CLIPBOARD_WRITE;

typedef struct _GLOBAL
{
    const WL_INTERFACE *Interface;
    uint32_t Version;
} GLOBAL;

static const GLOBAL Globals[] =
{
    { &wl_compositor_interface, 6 },
    { &wl_subcompositor_interface, 1 },
    { &wl_shm_interface, 1 },
    { &wl_seat_interface, 7 },
    { &wl_output_interface, 4 },
    { &wl_data_device_manager_interface, 3 },
    { &xdg_wm_base_interface, 5 },
    { &zxdg_decoration_manager_v1_interface, 1 },
    { &wp_cursor_shape_manager_v1_interface, 1 },
    { &xdg_toplevel_icon_manager_v1_interface, 1 },
};

static const char *const TextTypes[] =
{
    "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT", "STRING"
};

static const uint8_t CursorShapes[] =
{
    0, 0, 0, 0, 2, 3, 3, 4, 4, 1, 1, 0, 0, 9, 11, 11, 2, 9, 6, 5, 8, 7, 5, 7, 8, 6, 6, 5, 8, 7, 6, 5, 9, 0, 0, 0, 9
};

static const int32_t IconSizes[] = { 16, 24, 32, 48, 256 };

static const char Keymap[] =
    "xkb_keymap {\n"
    "    xkb_keycodes { include \"evdev+aliases(qwerty)\" };\n"
    "    xkb_types { include \"complete\" };\n"
    "    xkb_compat { include \"complete\" };\n"
    "    xkb_symbols { include \"pc+us+inet(evdev)\" };\n"
    "};\n";

static int Epoll = -1;
static int Device = -1;
static WATCH ListenerWatch = { WATCH_LISTENER, NULL };
static WATCH DeviceWatch = { WATCH_DEVICE, NULL };
static WL_CLIENT *Clients;
static uint32_t Serial = 1;
static WINDOW *Windows;
static uint32_t NextWindow = 1;
static WL_RESOURCE **Frames;
static uint32_t FrameCount;
static uint32_t FrameCapacity;
static uint32_t ScreenWidth = 1280;
static uint32_t ScreenHeight = 800;
static bool Host;
static SURFACE *PointerFocus;
static SURFACE *KeyboardFocus;
static WINDOW *FocusedWindow;
static POPUP **Popups;
static uint32_t PopupCount;
static uint32_t PopupCapacity;
static WL_RESOURCE *SelectionSource;
static HOST_SOURCE *SelectionHost;
static bool SelectionSet;
static uint32_t Pressed[256];
static uint32_t PressedCount;
static uint32_t ModsDepressed;
static uint32_t ModsLocked;
static uint32_t ModsUnlockOnRelease;
static uint8_t *HostBuffer;
static size_t HostCapacity;
static uint8_t *DeviceBuffer;

static void ServerDestroyResource(WL_RESOURCE *Resource);
static void Unmap(XDG_SURFACE *Xdg);
static void DrawWindow(WINDOW *Window, const RECT32 *Damage, uint32_t DamageCount);
static void SetSelection(WL_RESOURCE *Source, HOST_SOURCE *Host);
static void SendAppInfo(WINDOW *Window, TOPLEVEL *Toplevel);
static void SendIcon(WINDOW *Window, TOPLEVEL *Toplevel);

static
bool
Grow(void **Items, uint32_t *Capacity, uint32_t Needed, size_t ItemSize)
{
    uint32_t NewCapacity;
    void *NewItems;

    if (Needed <= *Capacity)
        return true;
    NewCapacity = *Capacity != 0 ? *Capacity : 8;
    while (NewCapacity < Needed)
        NewCapacity *= 2;
    NewItems = LxRealloc(*Items, (size_t)NewCapacity * ItemSize);
    if (NewItems == NULL)
        return false;
    memset((uint8_t *)NewItems + (size_t)*Capacity * ItemSize, 0, (size_t)(NewCapacity - *Capacity) * ItemSize);
    *Items = NewItems;
    *Capacity = NewCapacity;
    return true;
}

static
bool
GrowBytes(uint8_t **Data, size_t *Capacity, size_t Needed)
{
    size_t NewCapacity;
    uint8_t *NewData;

    if (Needed <= *Capacity)
        return true;
    NewCapacity = *Capacity != 0 ? *Capacity : 4096;
    while (NewCapacity < Needed)
        NewCapacity *= 2;
    NewData = LxRealloc(*Data, NewCapacity);
    if (NewData == NULL)
        return false;
    *Data = NewData;
    *Capacity = NewCapacity;
    return true;
}

static
bool
PointerArrayRemove(void **Items, uint32_t *Count, void *Item)
{
    uint32_t Index;

    for (Index = 0; Index < *Count; Index++)
    {
        if (Items[Index] == Item)
        {
            memmove(&Items[Index], &Items[Index + 1], (size_t)(*Count - Index - 1) * sizeof(void *));
            (*Count)--;
            return true;
        }
    }
    return false;
}

static
void
CloseFd(int Fd)
{
    if (Fd >= 0)
        LxSyscall(LX_NR_CLOSE, Fd, 0, 0, 0, 0, 0);
}

static
uint32_t
NowMs(void)
{
    return (uint32_t)LxMonotonicMs();
}

static
uint32_t
NextSerial(void)
{
    Serial++;
    return Serial;
}

static
WL_CLIENT *
ClientRef(WL_CLIENT *Client)
{
    Client->Refs++;
    return Client;
}

static
void
ClientUnref(WL_CLIENT *Client)
{
    if (--Client->Refs != 0)
        return;
    LxFree(Client->Objects);
    LxFree(Client->ServerObjects);
    LxFree(Client->In);
    LxFree(Client->InFds);
    LxFree(Client->Out);
    LxFree(Client->OutFds);
    LxFree(Client);
}

static
WL_RESOURCE *
Ref(WL_RESOURCE *Resource)
{
    if (Resource != NULL)
        Resource->Refs++;
    return Resource;
}

static
void
PoolRelease(POOL *Pool)
{
    if (--Pool->Refs != 0)
        return;
    if (Pool->Map != NULL)
        LxSyscall(LX_NR_MUNMAP, (long)Pool->Map, Pool->Size, 0, 0, 0, 0);
    CloseFd(Pool->Fd);
    LxFree(Pool);
}

static
void
HostSourceRelease(HOST_SOURCE *Source)
{
    if (Source == NULL || --Source->Refs != 0)
        return;
    LxFree(Source->Text);
    LxFree(Source);
}

static
void
IconSetFree(ICON_SET *Set)
{
    uint32_t Index;

    if (Set == NULL)
        return;
    for (Index = 0; Index < Set->Count; Index++)
        LxFree(Set->Images[Index].Pixels);
    LxFree(Set);
}

static
void
FreeData(WL_RESOURCE *Resource)
{
    const WL_INTERFACE *Interface = Resource->Interface;
    uint32_t Index;

    if (Resource->Data == NULL)
        return;
    if (Interface == &wl_surface_interface)
    {
        SURFACE *Surface = Resource->Data;

        LxFree(Surface->PendingFrames);
        LxFree(Surface->Children);
    }
    else if (Interface == &wl_shm_pool_interface)
    {
        PoolRelease(Resource->Data);
        return;
    }
    else if (Interface == &wl_buffer_interface)
    {
        PoolRelease(((BUFFER *)Resource->Data)->Pool);
    }
    else if (Interface == &xdg_toplevel_interface)
    {
        TOPLEVEL *Toplevel = Resource->Data;

        LxFree(Toplevel->Title);
        LxFree(Toplevel->AppId);
        IconSetFree(Toplevel->Icon);
        IconSetFree(Toplevel->PendingIcon);
    }
    else if (Interface == &wl_data_source_interface)
    {
        DATA_SOURCE *Source = Resource->Data;

        for (Index = 0; Index < Source->MimeCount; Index++)
            LxFree(Source->Mimes[Index]);
        LxFree(Source->Mimes);
    }
    else if (Interface == &xdg_toplevel_icon_v1_interface)
    {
        LxFree(((ICON *)Resource->Data)->Name);
    }
    else if (Interface == &wl_data_device_interface)
    {
        return;
    }
    LxFree(Resource->Data);
}

static
void
Unref(WL_RESOURCE *Resource)
{
    WL_CLIENT *Client;

    if (Resource == NULL || --Resource->Refs != 0)
        return;
    Client = Resource->Client;
    FreeData(Resource);
    LxFree(Resource);
    ClientUnref(Client);
}

static
void
ClientQueue(WL_CLIENT *Client, const void *Data, size_t Length, const int *Fds, uint32_t FdCount)
{
    uint32_t Index;

    if (Client->OutLength + Length > LX_MAX_OUTPUT ||
        !GrowBytes(&Client->Out, &Client->OutCapacity, Client->OutLength + Length) ||
        !Grow((void **)&Client->OutFds, &Client->OutFdCapacity, Client->OutFdCount + FdCount, sizeof(OUT_FD)))
    {
        LxLog("client %d output overflow", Client->Pid);
        Client->Failed = true;
        for (Index = 0; Index < FdCount; Index++)
            CloseFd(Fds[Index]);
        return;
    }
    for (Index = 0; Index < FdCount; Index++)
    {
        Client->OutFds[Client->OutFdCount].Fd = Fds[Index];
        Client->OutFds[Client->OutFdCount].Offset = Client->OutLength;
        Client->OutFdCount++;
    }
    memcpy(Client->Out + Client->OutLength, Data, Length);
    Client->OutLength += Length;
}

static
void
WlSend(WL_RESOURCE *Resource, uint32_t Opcode, ...)
{
    uint32_t Words[LX_MAX_MESSAGE / 4];
    const WL_MESSAGE *Message;
    WL_CLIENT *Client = Resource->Client;
    const char *Signature;
    size_t Size = 8;
    bool Overflow = false;
    int Fds[4];
    uint32_t FdCount = 0;
    uint32_t Index;
    va_list Args;

    Message = &Resource->Interface->Events[Opcode];
    va_start(Args, Opcode);
    for (Signature = Message->Signature; *Signature != '\0'; Signature++)
    {
        switch (*Signature)
        {
            case 'i':
            case 'u':
            case 'f':
            {
                uint32_t Value = va_arg(Args, uint32_t);

                if (Size + 4 > sizeof(Words))
                    Overflow = true;
                else
                    Words[Size / 4] = Value;
                Size += 4;
                break;
            }
            case 'o':
            case 'n':
            {
                WL_RESOURCE *Object = va_arg(Args, WL_RESOURCE *);

                if (Size + 4 > sizeof(Words))
                    Overflow = true;
                else
                    Words[Size / 4] = Object != NULL ? Object->Id : 0;
                Size += 4;
                break;
            }
            case 's':
            case 'a':
            {
                const void *Data;
                uint32_t Length;

                if (*Signature == 's')
                {
                    Data = va_arg(Args, const char *);
                    Length = Data != NULL ? (uint32_t)strlen(Data) + 1 : 0;
                }
                else
                {
                    Data = va_arg(Args, const void *);
                    Length = va_arg(Args, uint32_t);
                }
                if (Size + 4 + ((Length + 3) & ~3u) > sizeof(Words))
                {
                    Overflow = true;
                }
                else
                {
                    Words[Size / 4] = Length;
                    if (Length != 0)
                    {
                        Words[(Size + 4 + ((Length + 3) & ~3u)) / 4 - 1] = 0;
                        memcpy((uint8_t *)Words + Size + 4, Data, Length);
                    }
                }
                Size += 4 + ((Length + 3) & ~3u);
                break;
            }
            case 'h':
                if (FdCount < 4)
                    Fds[FdCount++] = va_arg(Args, int);
                else
                    CloseFd(va_arg(Args, int));
                break;
            default:
                break;
        }
    }
    va_end(Args);

    if (!Resource->Alive || Client->Closed || Message->Since > Resource->Version || Overflow)
    {
        if (Overflow)
            LxLog("event %s.%s is too large", Resource->Interface->Name, Message->Name);
        for (Index = 0; Index < FdCount; Index++)
            CloseFd(Fds[Index]);
        return;
    }
    Words[0] = Resource->Id;
    Words[1] = ((uint32_t)Size << 16) | Opcode;
    ClientQueue(Client, Words, Size, Fds, FdCount);
}

static
void
WlError(WL_RESOURCE *Resource, uint32_t Code, const char *Text)
{
    WL_CLIENT *Client = Resource->Client;

    if (Client->Failed)
        return;
    LxLog("protocol error on %s@%u: %s", Resource->Interface->Name, Resource->Id, Text);
    WlSend(Client->Display, WL_DISPLAY_EVENT_ERROR, Resource, Code, Text);
    Client->Failed = true;
}

static
WL_RESOURCE *
ClientLookup(WL_CLIENT *Client, uint32_t Id)
{
    uint32_t Index;

    if (Id < LX_SERVER_ID_BASE)
        return Id < Client->ObjectCapacity ? Client->Objects[Id] : NULL;
    for (Index = 0; Index < Client->ServerCount; Index++)
    {
        if (Client->ServerObjects[Index]->Id == Id)
            return Client->ServerObjects[Index];
    }
    return NULL;
}

static
WL_RESOURCE *
ClientInsert(WL_CLIENT *Client, uint32_t Id, const WL_INTERFACE *Interface, uint32_t Version)
{
    WL_RESOURCE *Resource;

    if (Client->Closed || Client->Failed)
        return NULL;
    if (ClientLookup(Client, Id) != NULL)
    {
        WlError(Client->Display, 0, "object already exists");
        return NULL;
    }
    Resource = LxAllocZero(sizeof(*Resource));
    if (Resource == NULL)
    {
        WlError(Client->Display, 2, "out of memory");
        return NULL;
    }
    if (Id < LX_SERVER_ID_BASE)
    {
        if (!Grow((void **)&Client->Objects, &Client->ObjectCapacity, Id + 1, sizeof(WL_RESOURCE *)))
        {
            LxFree(Resource);
            WlError(Client->Display, 2, "out of memory");
            return NULL;
        }
        Client->Objects[Id] = Resource;
    }
    else
    {
        if (!Grow((void **)&Client->ServerObjects, &Client->ServerCapacity, Client->ServerCount + 1,
                  sizeof(WL_RESOURCE *)))
        {
            LxFree(Resource);
            WlError(Client->Display, 2, "out of memory");
            return NULL;
        }
        Client->ServerObjects[Client->ServerCount++] = Resource;
        Client->NextServerId++;
    }
    Resource->Client = ClientRef(Client);
    Resource->Id = Id;
    Resource->Interface = Interface;
    Resource->Version = Version;
    Resource->Refs = 1;
    Resource->Alive = true;
    return Resource;
}

static
WL_RESOURCE *
ClientAdd(WL_CLIENT *Client, uint32_t Id, const WL_INTERFACE *Interface, uint32_t Version)
{
    if (Id == 0 || Id >= LX_MAX_CLIENT_ID)
    {
        if (!Client->Closed)
            WlError(Client->Display, 0, "invalid object id");
        return NULL;
    }
    return ClientInsert(Client, Id, Interface, Version);
}

static
WL_RESOURCE *
ClientAddServer(WL_CLIENT *Client, const WL_INTERFACE *Interface, uint32_t Version)
{
    return ClientInsert(Client, Client->NextServerId, Interface, Version);
}

static
void
ClientRemove(WL_RESOURCE *Resource)
{
    WL_CLIENT *Client = Resource->Client;

    Resource->Alive = false;
    if (Resource->Id < LX_SERVER_ID_BASE)
    {
        if (Resource->Id >= Client->ObjectCapacity || Client->Objects[Resource->Id] != Resource)
            return;
        Client->Objects[Resource->Id] = NULL;
        if (!Client->Closed)
            WlSend(Client->Display, WL_DISPLAY_EVENT_DELETE_ID, Resource->Id);
    }
    else if (!PointerArrayRemove((void **)Client->ServerObjects, &Client->ServerCount, Resource))
    {
        return;
    }
    Unref(Resource);
}

static
void
DestroyResource(WL_RESOURCE *Resource)
{
    if (Resource->Alive)
    {
        Resource->Alive = false;
        ServerDestroyResource(Resource);
    }
    ClientRemove(Resource);
}

static
uint32_t
ClientResources(WL_CLIENT *Client, const WL_INTERFACE *Interface, WL_RESOURCE **Found, uint32_t Capacity)
{
    uint32_t Count = 0;
    uint32_t Index;

    for (Index = 0; Index < Client->ObjectCapacity && Count < Capacity; Index++)
    {
        WL_RESOURCE *Resource = Client->Objects[Index];

        if (Resource != NULL && Resource->Alive && Resource->Interface == Interface)
            Found[Count++] = Resource;
    }
    for (Index = 0; Index < Client->ServerCount && Count < Capacity; Index++)
    {
        WL_RESOURCE *Resource = Client->ServerObjects[Index];

        if (Resource->Alive && Resource->Interface == Interface)
            Found[Count++] = Resource;
    }
    return Count;
}

static
void
HostWrite(size_t Size)
{
    long Result;

    ((LX_WIN_HEADER *)HostBuffer)->Size = (ULONG)Size;
    Result = LxSyscall(LX_NR_WRITE, Device, (long)HostBuffer, (long)Size, 0, 0, 0);
    if (Result == -LX_ENOTCONN)
        Host = false;
    else if (Result < 0)
        LxLog("window host write failed: %d", (int)-Result);
}

static
uint8_t *
HostBegin(uint32_t Type, uint32_t Window, size_t PayloadLength)
{
    LX_WIN_HEADER *Header;

    if (Device < 0 || !Host)
        return NULL;
    if (!GrowBytes(&HostBuffer, &HostCapacity, sizeof(LX_WIN_HEADER) + PayloadLength))
        return NULL;
    Header = (LX_WIN_HEADER *)HostBuffer;
    Header->Type = Type;
    Header->Size = 0;
    Header->Window = Window;
    Header->Reserved = 0;
    return HostBuffer + sizeof(LX_WIN_HEADER);
}

static
void
HostSend(uint32_t Type, uint32_t Window, const void *Payload, size_t Length)
{
    uint8_t *Data = HostBegin(Type, Window, Length);

    if (Data == NULL)
        return;
    if (Length != 0)
        memcpy(Data, Payload, Length);
    HostWrite(sizeof(LX_WIN_HEADER) + Length);
}

static
void
HostSendValues(uint32_t Type, uint32_t Window, uint32_t Count, const uint32_t *Values)
{
    HostSend(Type, Window, Values, Count * sizeof(uint32_t));
}

static
void
SendOutput(WL_RESOURCE *Output)
{
    WlSend(Output, WL_OUTPUT_EVENT_GEOMETRY, 0, 0, (int32_t)(ScreenWidth * 254 / 960),
           (int32_t)(ScreenHeight * 254 / 960), 0, "LiberNT", "Desktop", 0);
    WlSend(Output, WL_OUTPUT_EVENT_MODE, 3, ScreenWidth, ScreenHeight, 60000);
    if (Output->Version >= 2)
        WlSend(Output, WL_OUTPUT_EVENT_SCALE, 1);
    if (Output->Version >= 4)
    {
        WlSend(Output, WL_OUTPUT_EVENT_NAME, "LiberNT-1");
        WlSend(Output, WL_OUTPUT_EVENT_DESCRIPTION, "LiberNT desktop");
    }
    if (Output->Version >= 2)
        WlSend(Output, WL_OUTPUT_EVENT_DONE);
}

static
RECT32
WindowRect(XDG_SURFACE *Xdg)
{
    SURFACE *Surface = Xdg->Surface;
    int32_t SurfaceWidth = Surface != NULL ? Surface->Width : 0;
    int32_t SurfaceHeight = Surface != NULL ? Surface->Height : 0;
    RECT32 Rect;

    if (Xdg->HasGeometry && Xdg->Geometry.Width > 0 && Xdg->Geometry.Height > 0)
    {
        Rect = Xdg->Geometry;
    }
    else
    {
        Rect.X = 0;
        Rect.Y = 0;
        Rect.Width = SurfaceWidth;
        Rect.Height = SurfaceHeight;
    }
    if (Rect.X < 0)
        Rect.X = 0;
    if (Rect.Y < 0)
        Rect.Y = 0;
    if (Rect.Width > SurfaceWidth - Rect.X)
        Rect.Width = SurfaceWidth - Rect.X;
    if (Rect.Height > SurfaceHeight - Rect.Y)
        Rect.Height = SurfaceHeight - Rect.Y;
    if (Rect.Width < 1)
        Rect.Width = 1;
    if (Rect.Height < 1)
        Rect.Height = 1;
    return Rect;
}

static
TOPLEVEL *
XdgToplevel(XDG_SURFACE *Xdg)
{
    if (Xdg == NULL || Xdg->RoleType != ROLE_TOPLEVEL || Xdg->Role == NULL || !Xdg->Role->Alive)
        return NULL;
    return Xdg->Role->Data;
}

static
POPUP *
XdgPopup(XDG_SURFACE *Xdg)
{
    if (Xdg == NULL || Xdg->RoleType != ROLE_POPUP || Xdg->Role == NULL || !Xdg->Role->Alive)
        return NULL;
    return Xdg->Role->Data;
}

static
void
SendSizes(TOPLEVEL *Toplevel)
{
    uint32_t Values[4];
    WINDOW *Window = Toplevel->Xdg != NULL ? Toplevel->Xdg->Window : NULL;

    if (Window == NULL)
        return;
    Values[0] = (uint32_t)Toplevel->MinWidth;
    Values[1] = (uint32_t)Toplevel->MinHeight;
    Values[2] = (uint32_t)Toplevel->MaxWidth;
    Values[3] = (uint32_t)Toplevel->MaxHeight;
    HostSendValues(LX_WIN_SIZES, Window->Id, 4, Values);
}

static
void
SendXdgConfigure(XDG_SURFACE *Xdg)
{
    Xdg->Serial = NextSerial();
    WlSend(Xdg->Resource, XDG_SURFACE_EVENT_CONFIGURE, Xdg->Serial);
    Xdg->Configured = true;
}

static
void
SendToplevelConfigure(TOPLEVEL *Toplevel)
{
    uint32_t States = Toplevel->States;
    uint32_t Array[8];
    uint32_t Count = 0;
    uint32_t State;

    if (Toplevel->Xdg == NULL)
        return;
    if (!Toplevel->Decorated)
        States |= XDG_STATE_TILED;
    if (FocusedWindow != NULL && FocusedWindow->Xdg == Toplevel->Xdg)
        States |= 1u << XDG_STATE_ACTIVATED;
    for (State = 1; State <= 8; State++)
    {
        if ((States & (1u << State)) && (State <= 4 || Toplevel->Resource->Version >= 2))
            Array[Count++] = State;
    }
    WlSend(Toplevel->Resource, XDG_TOPLEVEL_EVENT_CONFIGURE, Toplevel->Width, Toplevel->Height, Array,
           Count * (uint32_t)sizeof(uint32_t));
    SendXdgConfigure(Toplevel->Xdg);
}

static
void
Blit(WINDOW *Window, SURFACE *Surface, int32_t OffsetX, int32_t OffsetY)
{
    BUFFER *Buffer = Surface->Buffer != NULL ? Surface->Buffer->Data : NULL;
    uint32_t Index;

    if (Buffer != NULL && Buffer->Pool != NULL && Buffer->Pool->Map != NULL &&
        (int64_t)Buffer->Offset + (int64_t)Buffer->Stride * Buffer->Height <= Buffer->Pool->Size)
    {
        int32_t X0 = OffsetX > 0 ? OffsetX : 0;
        int32_t Y0 = OffsetY > 0 ? OffsetY : 0;
        int32_t X1 = OffsetX + Buffer->Width < Window->Width ? OffsetX + Buffer->Width : Window->Width;
        int32_t Y1 = OffsetY + Buffer->Height < Window->Height ? OffsetY + Buffer->Height : Window->Height;
        int32_t Row;

        if (X1 > X0 && Y1 > Y0)
        {
            size_t Span = (size_t)(X1 - X0) * 4;

            for (Row = Y0; Row < Y1; Row++)
            {
                const uint8_t *Source = Buffer->Pool->Map + Buffer->Offset + (size_t)(Row - OffsetY) * Buffer->Stride +
                                        (size_t)(X0 - OffsetX) * 4;

                memcpy(Window->Image + ((size_t)Row * Window->Width + X0) * 4, Source, Span);
            }
        }
    }
    for (Index = 0; Index < Surface->ChildCount; Index++)
    {
        SURFACE *Child = Surface->Children[Index];

        Blit(Window, Child, OffsetX + Child->SubX, OffsetY + Child->SubY);
    }
}

static
void
SendPixels(WINDOW *Window, int32_t X, int32_t Y, int32_t Width, int32_t Height)
{
    size_t Span = (size_t)Width * 4;
    uint8_t *Data;
    int32_t Row;

    if (Width <= 0 || Height <= 0)
        return;
    Data = HostBegin(LX_WIN_PIXELS, Window->Id, 16 + Span * Height);
    if (Data == NULL)
        return;
    ((int32_t *)Data)[0] = X;
    ((int32_t *)Data)[1] = Y;
    ((uint32_t *)Data)[2] = (uint32_t)Width;
    ((uint32_t *)Data)[3] = (uint32_t)Height;
    for (Row = 0; Row < Height; Row++)
        memcpy(Data + 16 + Span * Row, Window->Image + ((size_t)(Y + Row) * Window->Width + X) * 4, Span);
    HostWrite(sizeof(LX_WIN_HEADER) + 16 + Span * Height);
}

static
void
DrawWindow(WINDOW *Window, const RECT32 *Damage, uint32_t DamageCount)
{
    XDG_SURFACE *Xdg = Window->Xdg;
    RECT32 Rect = WindowRect(Xdg);
    int32_t X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
    bool Any = false;
    uint32_t Index;

    if (Xdg->Surface == NULL)
        return;
    if (Rect.Width != Window->Width || Rect.Height != Window->Height)
    {
        int32_t Values[4] = { 0, 0, Rect.Width, Rect.Height };
        uint8_t *Image = LxAllocZero((size_t)Rect.Width * Rect.Height * 4);
        POPUP *Popup = XdgPopup(Xdg);

        if (Image == NULL)
            return;
        LxFree(Window->Image);
        Window->Image = Image;
        Window->Width = Rect.Width;
        Window->Height = Rect.Height;
        if (Window->Popup && Popup != NULL)
        {
            Values[0] = Popup->Geometry.X;
            Values[1] = Popup->Geometry.Y;
        }
        HostSend(LX_WIN_GEOMETRY, Window->Id, Values, sizeof(Values));
        DamageCount = 0;
    }
    if (DamageCount == 0)
    {
        X1 = Rect.Width;
        Y1 = Rect.Height;
        Any = true;
    }
    for (Index = 0; Index < DamageCount; Index++)
    {
        int32_t DX0 = Damage[Index].X - Rect.X;
        int32_t DY0 = Damage[Index].Y - Rect.Y;
        int64_t DX1 = (int64_t)DX0 + Damage[Index].Width;
        int64_t DY1 = (int64_t)DY0 + Damage[Index].Height;
        int32_t CX0 = DX0 > 0 ? DX0 : 0;
        int32_t CY0 = DY0 > 0 ? DY0 : 0;
        int32_t CX1 = DX1 < Rect.Width ? (int32_t)DX1 : Rect.Width;
        int32_t CY1 = DY1 < Rect.Height ? (int32_t)DY1 : Rect.Height;

        if (CX1 <= CX0 || CY1 <= CY0)
            continue;
        if (!Any)
        {
            X0 = CX0;
            Y0 = CY0;
            X1 = CX1;
            Y1 = CY1;
            Any = true;
        }
        else
        {
            X0 = CX0 < X0 ? CX0 : X0;
            Y0 = CY0 < Y0 ? CY0 : Y0;
            X1 = CX1 > X1 ? CX1 : X1;
            Y1 = CY1 > Y1 ? CY1 : Y1;
        }
    }
    if (!Any)
        return;
    Blit(Window, Xdg->Surface, -Rect.X, -Rect.Y);
    SendPixels(Window, X0, Y0, X1 - X0, Y1 - Y0);
}

static
void
Map(XDG_SURFACE *Xdg)
{
    TOPLEVEL *Toplevel = XdgToplevel(Xdg);
    POPUP *Popup = XdgPopup(Xdg);
    RECT32 Rect;
    WINDOW *Window;
    LX_WIN_CREATE_MSG Create;

    if (!Host || (Toplevel == NULL && Popup == NULL) || Xdg->Surface == NULL)
        return;
    Rect = WindowRect(Xdg);
    if (Popup != NULL && (Popup->Parent == NULL || Popup->Parent->Window == NULL))
        return;
    Window = LxAllocZero(sizeof(*Window));
    if (Window == NULL)
        return;
    Window->Image = LxAllocZero((size_t)Rect.Width * Rect.Height * 4);
    if (Window->Image == NULL)
    {
        LxFree(Window);
        return;
    }
    Window->Id = NextWindow++;
    Window->Xdg = Xdg;
    Ref(Xdg->Resource);
    Window->Popup = Popup != NULL;
    Window->Width = Rect.Width;
    Window->Height = Rect.Height;
    Window->Next = Windows;
    Windows = Window;
    Xdg->Window = Window;
    LxLog("map %s %u %dx%d %s", Popup != NULL ? "popup" : "toplevel", Window->Id, Rect.Width, Rect.Height,
          Toplevel != NULL && Toplevel->Title != NULL ? Toplevel->Title : "");

    memset(&Create, 0, sizeof(Create));
    Create.Width = (ULONG)Rect.Width;
    Create.Height = (ULONG)Rect.Height;
    if (Popup != NULL)
    {
        Create.X = Popup->Geometry.X;
        Create.Y = Popup->Geometry.Y;
        Create.Parent = Popup->Parent->Window->Id;
        Create.Flags = LX_WIN_FLAG_POPUP;
        HostSend(LX_WIN_CREATE, Window->Id, &Create.X, sizeof(Create) - sizeof(Create.Header));
        if (Grow((void **)&Popups, &PopupCapacity, PopupCount + 1, sizeof(POPUP *)))
            Popups[PopupCount++] = Popup;
    }
    else
    {
        Create.Flags = Toplevel->Decorated ? LX_WIN_FLAG_DECORATED : 0;
        HostSend(LX_WIN_CREATE, Window->Id, &Create.X, sizeof(Create) - sizeof(Create.Header));
        if (Toplevel->Title != NULL && Toplevel->Title[0] != '\0')
            HostSend(LX_WIN_TITLE, Window->Id, Toplevel->Title, strlen(Toplevel->Title));
        SendAppInfo(Window, Toplevel);
        SendSizes(Toplevel);
    }
}

static
void
FreeWindow(WINDOW *Window)
{
    WINDOW **Link;

    for (Link = &Windows; *Link != NULL; Link = &(*Link)->Next)
    {
        if (*Link == Window)
        {
            *Link = Window->Next;
            break;
        }
    }
    LxFree(Window->Image);
    Unref(Window->Xdg->Resource);
    LxFree(Window);
}

static
void
Unmap(XDG_SURFACE *Xdg)
{
    WINDOW *Window = Xdg->Window;
    POPUP *Popup;
    uint32_t Id;

    if (Window == NULL)
        return;
    Xdg->Window = NULL;
    if (FocusedWindow == Window)
        FocusedWindow = NULL;
    if (PointerFocus != NULL && PointerFocus->Xdg == Xdg)
        PointerFocus = NULL;
    Popup = XdgPopup(Xdg);
    if (Popup != NULL)
        PointerArrayRemove((void **)Popups, &PopupCount, Popup);
    Id = Window->Id;
    FreeWindow(Window);
    HostSend(LX_WIN_DESTROY, Id, NULL, 0);
}

static
void
ApplyIcon(TOPLEVEL *Toplevel)
{
    WINDOW *Window;

    if (!Toplevel->IconPending)
        return;
    Toplevel->IconPending = false;
    IconSetFree(Toplevel->Icon);
    Toplevel->Icon = Toplevel->PendingIcon;
    Toplevel->PendingIcon = NULL;
    Window = Toplevel->Xdg != NULL ? Toplevel->Xdg->Window : NULL;
    if (Window != NULL)
        SendIcon(Window, Toplevel);
}

static
void
CommitXdg(XDG_SURFACE *Xdg, const RECT32 *Damage, uint32_t DamageCount, bool Attached)
{
    TOPLEVEL *Toplevel;
    POPUP *Popup;

    if (Xdg->HasPendingGeometry)
    {
        Xdg->Geometry = Xdg->PendingGeometry;
        Xdg->HasGeometry = true;
        Xdg->HasPendingGeometry = false;
    }
    if (Xdg->Role == NULL || !Xdg->Role->Alive)
        return;
    Toplevel = XdgToplevel(Xdg);
    Popup = XdgPopup(Xdg);
    if (Toplevel != NULL)
        ApplyIcon(Toplevel);
    if (!Xdg->Configured)
    {
        if (Toplevel != NULL)
        {
            SendToplevelConfigure(Toplevel);
        }
        else if (Popup != NULL)
        {
            WlSend(Popup->Resource, XDG_POPUP_EVENT_CONFIGURE, Popup->Geometry.X, Popup->Geometry.Y,
                   Popup->Geometry.Width, Popup->Geometry.Height);
            SendXdgConfigure(Xdg);
        }
        return;
    }
    if (Xdg->Surface == NULL || Xdg->Surface->Buffer == NULL)
    {
        Unmap(Xdg);
        return;
    }
    if (Xdg->Window == NULL)
        Map(Xdg);
    if (Xdg->Window != NULL)
    {
        if (Attached && DamageCount == 0)
            DrawWindow(Xdg->Window, NULL, 0);
        else
            DrawWindow(Xdg->Window, Damage, DamageCount);
    }
}

static
uint32_t
PlaceAxis(int32_t Start, int32_t Length, uint32_t Value, const uint8_t *Low, const uint8_t *High, int32_t *Point)
{
    uint32_t Index;

    *Point = Start + Length / 2;
    for (Index = 0; Index < 3; Index++)
    {
        if (Value == Low[Index])
            *Point = Start;
        if (Value == High[Index])
            *Point = Start + Length;
    }
    return 0;
}

static
RECT32
PlacePopup(const POSITIONER *Positioner)
{
    static const uint8_t Left[] = { 3, 5, 6 }, Right[] = { 4, 7, 8 }, Top[] = { 1, 5, 7 }, Bottom[] = { 2, 6, 8 };
    int32_t PX, PY;
    int32_t Width = Positioner->Width, Height = Positioner->Height;
    RECT32 Rect;
    uint32_t Index;

    PlaceAxis(Positioner->Anchor.X, Positioner->Anchor.Width, Positioner->AnchorEdge, Left, Right, &PX);
    PlaceAxis(Positioner->Anchor.Y, Positioner->Anchor.Height, Positioner->AnchorEdge, Top, Bottom, &PY);
    Rect.X = PX - Width / 2;
    Rect.Y = PY - Height / 2;
    for (Index = 0; Index < 3; Index++)
    {
        if (Positioner->Gravity == Left[Index])
            Rect.X = PX - Width;
        if (Positioner->Gravity == Right[Index])
            Rect.X = PX;
        if (Positioner->Gravity == Top[Index])
            Rect.Y = PY - Height;
        if (Positioner->Gravity == Bottom[Index])
            Rect.Y = PY;
    }
    Rect.X += Positioner->OffsetX;
    Rect.Y += Positioner->OffsetY;
    Rect.Width = Width;
    Rect.Height = Height;
    return Rect;
}

static
void
RequestState(TOPLEVEL *Toplevel, uint32_t State)
{
    WINDOW *Window = Toplevel->Xdg != NULL ? Toplevel->Xdg->Window : NULL;
    uint32_t Values[2] = { State, 0 };

    if (Window != NULL)
        HostSendValues(LX_WIN_STATE, Window->Id, 2, Values);
    else if (State == LX_WIN_STATE_MAXIMIZED)
        Toplevel->States |= 1u << XDG_STATE_MAXIMIZED;
}

static
void
SendIcon(WINDOW *Window, TOPLEVEL *Toplevel)
{
    ICON_SET *Set = Toplevel->Icon;
    size_t Length = 8;
    uint32_t *Values;
    uint8_t *Data;
    uint32_t Index;

    if (Set == NULL)
        return;
    for (Index = 0; Index < Set->Count; Index++)
        Length += 8 + (size_t)Set->Images[Index].Size * Set->Images[Index].Size * 4;
    Data = HostBegin(LX_WIN_ICON, Window->Id, Length);
    if (Data == NULL)
        return;
    Values = (uint32_t *)Data;
    Values[0] = Set->Count;
    Values[1] = 0;
    for (Index = 0; Index < Set->Count; Index++)
    {
        Values[2 + Index * 2] = Set->Images[Index].Size;
        Values[3 + Index * 2] = Set->Images[Index].Size;
    }
    Data += 8 + (size_t)Set->Count * 8;
    for (Index = 0; Index < Set->Count; Index++)
    {
        size_t Bytes = (size_t)Set->Images[Index].Size * Set->Images[Index].Size * 4;

        memcpy(Data, Set->Images[Index].Pixels, Bytes);
        Data += Bytes;
    }
    HostWrite(sizeof(LX_WIN_HEADER) + Length);
}

static
void
SendAppInfo(WINDOW *Window, TOPLEVEL *Toplevel)
{
    if (Toplevel->AppId != NULL && Toplevel->AppId[0] != '\0')
        HostSend(LX_WIN_APP_ID, Window->Id, Toplevel->AppId, strlen(Toplevel->AppId));
    SendIcon(Window, Toplevel);
}

static
WINDOW *
PointerWindow(void)
{
    if (PointerFocus == NULL || PointerFocus->Xdg == NULL)
        return NULL;
    return PointerFocus->Xdg->Window;
}

static
void
OfferSelection(WL_RESOURCE *DataDevice)
{
    WL_RESOURCE *Offer;
    OFFER *OfferData;
    uint32_t Index;

    if (!SelectionSet || (SelectionSource != NULL && !SelectionSource->Alive))
    {
        WlSend(DataDevice, WL_DATA_DEVICE_EVENT_SELECTION, NULL);
        return;
    }
    OfferData = LxAllocZero(sizeof(*OfferData));
    if (OfferData == NULL)
        return;
    Offer = ClientAddServer(DataDevice->Client, &wl_data_offer_interface, DataDevice->Version);
    if (Offer == NULL)
    {
        LxFree(OfferData);
        return;
    }
    Offer->Data = OfferData;
    OfferData->Source = Ref(SelectionSource);
    OfferData->Host = SelectionHost;
    if (SelectionHost != NULL)
        SelectionHost->Refs++;
    WlSend(DataDevice, WL_DATA_DEVICE_EVENT_DATA_OFFER, Offer);
    if (SelectionHost != NULL)
    {
        for (Index = 0; Index < sizeof(TextTypes) / sizeof(TextTypes[0]); Index++)
            WlSend(Offer, WL_DATA_OFFER_EVENT_OFFER, TextTypes[Index]);
    }
    else
    {
        DATA_SOURCE *Source = SelectionSource->Data;

        for (Index = 0; Index < Source->MimeCount; Index++)
            WlSend(Offer, WL_DATA_OFFER_EVENT_OFFER, Source->Mimes[Index]);
    }
    WlSend(DataDevice, WL_DATA_DEVICE_EVENT_SELECTION, Offer);
}

static
void
SetSelection(WL_RESOURCE *Source, HOST_SOURCE *HostSource)
{
    WL_RESOURCE *Devices[64];
    uint32_t Count, Index;

    WL_RESOURCE *OldSource = SelectionSource;
    HOST_SOURCE *OldHost = SelectionHost;

    if (OldSource != NULL && OldSource != Source && OldSource->Alive)
        WlSend(OldSource, WL_DATA_SOURCE_EVENT_CANCELLED);
    SelectionSource = Ref(Source);
    SelectionHost = HostSource;
    Unref(OldSource);
    HostSourceRelease(OldHost);
    SelectionSet = Source != NULL || HostSource != NULL;
    if (KeyboardFocus != NULL)
    {
        Count = ClientResources(KeyboardFocus->Resource->Client, &wl_data_device_interface, Devices, 64);
        for (Index = 0; Index < Count; Index++)
            OfferSelection(Devices[Index]);
    }
}

static
uint32_t
KeyModifier(uint32_t Key)
{
    switch (Key)
    {
        case 42: case 54: return MOD_SHIFT;
        case 29: case 97: return MOD_CONTROL;
        case 56: case 100: return MOD_MOD1;
        case 125: case 126: return MOD_MOD4;
        case 58: return MOD_LOCK;
        case 69: return MOD_MOD2;
        default: return 0;
    }
}

static
void
KeyboardEnter(WL_RESOURCE *Keyboard, SURFACE *Surface)
{
    uint32_t Serial1 = NextSerial(), Serial2;

    WlSend(Keyboard, WL_KEYBOARD_EVENT_ENTER, Serial1, Surface->Resource, Pressed, PressedCount * 4);
    Serial2 = NextSerial();
    WlSend(Keyboard, WL_KEYBOARD_EVENT_MODIFIERS, Serial2, ModsDepressed, 0, ModsLocked, 0);
}

static
SURFACE *
KeyboardTarget(void)
{
    uint32_t Index;

    for (Index = PopupCount; Index-- != 0;)
    {
        POPUP *Popup = Popups[Index];

        if (Popup->Grab && Popup->Xdg != NULL && Popup->Xdg->Surface != NULL && Popup->Xdg->Surface->Resource->Alive)
            return Popup->Xdg->Surface;
    }
    if (FocusedWindow != NULL && FocusedWindow->Xdg->Surface != NULL && FocusedWindow->Xdg->Surface->Resource->Alive)
        return FocusedWindow->Xdg->Surface;
    return NULL;
}

static
void
UpdateKeyboardFocus(void)
{
    SURFACE *Target = KeyboardTarget();
    SURFACE *Old = KeyboardFocus;
    WL_RESOURCE *Found[64];
    uint32_t Count, Index;

    if (Target == Old)
        return;
    if (Old != NULL && Old->Resource->Alive)
    {
        uint32_t LeaveSerial = NextSerial();

        Count = ClientResources(Old->Resource->Client, &wl_keyboard_interface, Found, 64);
        for (Index = 0; Index < Count; Index++)
            WlSend(Found[Index], WL_KEYBOARD_EVENT_LEAVE, LeaveSerial, Old->Resource);
    }
    KeyboardFocus = Target;
    if (Target != NULL)
    {
        Count = ClientResources(Target->Resource->Client, &wl_keyboard_interface, Found, 64);
        for (Index = 0; Index < Count; Index++)
            KeyboardEnter(Found[Index], Target);
        Count = ClientResources(Target->Resource->Client, &wl_data_device_interface, Found, 64);
        for (Index = 0; Index < Count; Index++)
            OfferSelection(Found[Index]);
    }
}

static
void
Key(uint32_t KeyCode, bool Down)
{
    WL_RESOURCE *Found[64];
    uint32_t Mask = KeyModifier(KeyCode);
    bool Repeat = false;
    uint32_t Count, Index, KeySerial, Time;

    for (Index = 0; Index < PressedCount; Index++)
    {
        if (Pressed[Index] == KeyCode)
            break;
    }
    if (Down)
    {
        if (Index < PressedCount)
            Repeat = true;
        else if (PressedCount < sizeof(Pressed) / sizeof(Pressed[0]))
            Pressed[PressedCount++] = KeyCode;
    }
    else if (Index < PressedCount)
    {
        Pressed[Index] = Pressed[--PressedCount];
    }
    if (Mask != 0 && !Repeat)
    {
        ModsDepressed = 0;
        for (Index = 0; Index < PressedCount; Index++)
            ModsDepressed |= KeyModifier(Pressed[Index]);
        if (Mask == MOD_LOCK || Mask == MOD_MOD2)
        {
            if (Down)
            {
                ModsUnlockOnRelease = (ModsUnlockOnRelease & ~Mask) | (ModsLocked & Mask);
                ModsLocked |= Mask;
            }
            else
            {
                ModsLocked &= ~(ModsUnlockOnRelease & Mask);
                ModsUnlockOnRelease &= ~Mask;
            }
        }
    }
    if (KeyboardFocus == NULL || !KeyboardFocus->Resource->Alive)
        return;
    KeySerial = NextSerial();
    Time = NowMs();
    Count = ClientResources(KeyboardFocus->Resource->Client, &wl_keyboard_interface, Found, 64);
    for (Index = 0; Index < Count; Index++)
    {
        WlSend(Found[Index], WL_KEYBOARD_EVENT_KEY, KeySerial, Time, KeyCode, Down ? 1 : 0);
        WlSend(Found[Index], WL_KEYBOARD_EVENT_MODIFIERS, KeySerial, ModsDepressed, 0, ModsLocked, 0);
    }
}

static
void
PointerLeave(void)
{
    SURFACE *Surface = PointerFocus;
    WL_RESOURCE *Found[64];
    uint32_t Count, Index, LeaveSerial;

    if (Surface == NULL)
        return;
    PointerFocus = NULL;
    if (!Surface->Resource->Alive)
        return;
    LeaveSerial = NextSerial();
    Count = ClientResources(Surface->Resource->Client, &wl_pointer_interface, Found, 64);
    for (Index = 0; Index < Count; Index++)
    {
        WlSend(Found[Index], WL_POINTER_EVENT_LEAVE, LeaveSerial, Surface->Resource);
        if (Found[Index]->Version >= 5)
            WlSend(Found[Index], WL_POINTER_EVENT_FRAME);
    }
}

static
void
PointerMotion(WINDOW *Window, int32_t X, int32_t Y)
{
    SURFACE *Surface = Window->Xdg->Surface;
    RECT32 Rect = WindowRect(Window->Xdg);
    WL_RESOURCE *Found[64];
    uint32_t Count, Index, Time;
    int32_t LocalX = (X + Rect.X) * 256, LocalY = (Y + Rect.Y) * 256;

    if (Surface == NULL || !Surface->Resource->Alive)
        return;
    if (PointerFocus != Surface)
    {
        uint32_t EnterSerial;

        PointerLeave();
        PointerFocus = Surface;
        EnterSerial = NextSerial();
        Count = ClientResources(Surface->Resource->Client, &wl_pointer_interface, Found, 64);
        for (Index = 0; Index < Count; Index++)
        {
            WlSend(Found[Index], WL_POINTER_EVENT_ENTER, EnterSerial, Surface->Resource, LocalX, LocalY);
            if (Found[Index]->Version >= 5)
                WlSend(Found[Index], WL_POINTER_EVENT_FRAME);
        }
    }
    Time = NowMs();
    Count = ClientResources(Surface->Resource->Client, &wl_pointer_interface, Found, 64);
    for (Index = 0; Index < Count; Index++)
    {
        WlSend(Found[Index], WL_POINTER_EVENT_MOTION, Time, LocalX, LocalY);
        if (Found[Index]->Version >= 5)
            WlSend(Found[Index], WL_POINTER_EVENT_FRAME);
    }
}

static
void
DismissPopups(void)
{
    while (PopupCount != 0)
    {
        POPUP *Popup = Popups[--PopupCount];

        if (Popup->Resource->Alive)
            WlSend(Popup->Resource, XDG_POPUP_EVENT_POPUP_DONE);
        if (Popup->Xdg != NULL)
            Unmap(Popup->Xdg);
    }
}

static
void
PointerButton(WINDOW *Window, uint32_t Button, bool Down)
{
    SURFACE *Surface;
    WL_RESOURCE *Found[64];
    uint32_t Count, Index, ButtonSerial, Time;

    if (Down && PopupCount != 0 && !Window->Popup)
        DismissPopups();
    Surface = PointerFocus;
    if (Surface == NULL || !Surface->Resource->Alive)
        return;
    ButtonSerial = NextSerial();
    Time = NowMs();
    Count = ClientResources(Surface->Resource->Client, &wl_pointer_interface, Found, 64);
    for (Index = 0; Index < Count; Index++)
    {
        WlSend(Found[Index], WL_POINTER_EVENT_BUTTON, ButtonSerial, Time, Button, Down ? 1 : 0);
        if (Found[Index]->Version >= 5)
            WlSend(Found[Index], WL_POINTER_EVENT_FRAME);
    }
}

static
void
PointerAxis(uint32_t Axis, int32_t Delta)
{
    SURFACE *Surface = PointerFocus;
    WL_RESOURCE *Found[64];
    uint32_t Count, Index, Time;
    int64_t Scaled = (int64_t)Delta * 2560;
    int32_t Value = (int32_t)(Scaled / 120);
    int32_t Discrete = Delta / 120;

    if (Surface == NULL || !Surface->Resource->Alive)
        return;
    if (2 * (Scaled % 120 < 0 ? -(Scaled % 120) : Scaled % 120) >= 120)
        Value += Scaled < 0 ? -1 : 1;
    if (Discrete == 0)
        Discrete = Delta > 0 ? 1 : -1;
    Time = NowMs();
    Count = ClientResources(Surface->Resource->Client, &wl_pointer_interface, Found, 64);
    for (Index = 0; Index < Count; Index++)
    {
        WL_RESOURCE *Pointer = Found[Index];

        if (Pointer->Version >= 5)
            WlSend(Pointer, WL_POINTER_EVENT_AXIS_SOURCE, 0);
        if (Pointer->Version >= 8)
            WlSend(Pointer, WL_POINTER_EVENT_AXIS_VALUE120, Axis, Delta);
        else if (Pointer->Version >= 5)
            WlSend(Pointer, WL_POINTER_EVENT_AXIS_DISCRETE, Axis, Discrete);
        WlSend(Pointer, WL_POINTER_EVENT_AXIS, Time, Axis, Value);
        if (Pointer->Version >= 5)
            WlSend(Pointer, WL_POINTER_EVENT_FRAME);
    }
}

static
bool
WatchAdd(int Fd, uint32_t Events, WATCH *Watch)
{
    LX_EPOLL_EVENT Event;

    Event.Events = Events;
    Event.Data = (uint64_t)(uintptr_t)Watch;
    return LxSyscall(LX_NR_EPOLL_CTL, Epoll, LX_EPOLL_CTL_ADD, Fd, (long)&Event, 0, 0) == 0;
}

static
void
WatchModify(int Fd, uint32_t Events, WATCH *Watch)
{
    LX_EPOLL_EVENT Event;

    Event.Events = Events;
    Event.Data = (uint64_t)(uintptr_t)Watch;
    LxSyscall(LX_NR_EPOLL_CTL, Epoll, LX_EPOLL_CTL_MOD, Fd, (long)&Event, 0, 0);
}

static
void
WatchRemove(int Fd)
{
    LX_EPOLL_EVENT Event;

    memset(&Event, 0, sizeof(Event));
    LxSyscall(LX_NR_EPOLL_CTL, Epoll, LX_EPOLL_CTL_DEL, Fd, (long)&Event, 0, 0);
}

static
void
ClipboardWriteFree(CLIPBOARD_WRITE *Write, bool Watched)
{
    if (Watched)
        WatchRemove(Write->Fd);
    CloseFd(Write->Fd);
    LxFree(Write->Data);
    LxFree(Write);
}

static
bool
ClipboardWriteStep(CLIPBOARD_WRITE *Write)
{
    while (Write->Offset < Write->Length)
    {
        long Result = LxSyscall(LX_NR_WRITE, Write->Fd, (long)(Write->Data + Write->Offset),
                                (long)(Write->Length - Write->Offset), 0, 0, 0);

        if (Result == -LX_EAGAIN)
            return false;
        if (Result == -LX_EINTR)
            continue;
        if (Result <= 0)
            return true;
        Write->Offset += (size_t)Result;
    }
    return true;
}

static
void
ClipboardWriteStart(int Fd, const HOST_SOURCE *Source)
{
    CLIPBOARD_WRITE *Write = LxAllocZero(sizeof(*Write));
    long Flags;

    if (Write == NULL || (Source->Length != 0 && (Write->Data = LxAlloc(Source->Length)) == NULL))
    {
        LxFree(Write);
        CloseFd(Fd);
        return;
    }
    Write->Watch.Type = WATCH_CLIPBOARD_WRITE;
    Write->Watch.Object = Write;
    Write->Fd = Fd;
    Write->Length = Source->Length;
    if (Source->Length != 0)
        memcpy(Write->Data, Source->Text, Source->Length);
    Flags = LxSyscall(LX_NR_FCNTL, Fd, LX_F_GETFL, 0, 0, 0, 0);
    if (Flags >= 0 && WatchAdd(Fd, LX_EPOLLOUT, &Write->Watch))
    {
        LxSyscall(LX_NR_FCNTL, Fd, LX_F_SETFL, Flags | LX_O_NONBLOCK, 0, 0, 0);
        if (ClipboardWriteStep(Write))
            ClipboardWriteFree(Write, true);
        return;
    }
    ClipboardWriteStep(Write);
    ClipboardWriteFree(Write, false);
}

static
void
ClipboardReadDone(CLIPBOARD_READ *Read)
{
    size_t Length = Read->Length < LX_CLIPBOARD_LIMIT ? Read->Length : LX_CLIPBOARD_LIMIT;

    WatchRemove(Read->Fd);
    CloseFd(Read->Fd);
    if (SelectionSource == Read->Source && Length != 0)
        HostSend(LX_WIN_CLIPBOARD, 0, Read->Data, Length);
    Unref(Read->Source);
    LxFree(Read->Data);
    LxFree(Read);
}

static
void
ClipboardRead(CLIPBOARD_READ *Read)
{
    long Result;

    if (!GrowBytes(&Read->Data, &Read->Capacity, Read->Length + 65536))
    {
        ClipboardReadDone(Read);
        return;
    }
    Result = LxSyscall(LX_NR_READ, Read->Fd, (long)(Read->Data + Read->Length), 65536, 0, 0, 0);
    if (Result == -LX_EAGAIN || Result == -LX_EINTR)
        return;
    if (Result > 0 && Read->Length < LX_CLIPBOARD_LIMIT)
    {
        Read->Length += (size_t)Result;
        return;
    }
    ClipboardReadDone(Read);
}

static
void
ClipboardReadStart(WL_RESOURCE *Source, const char *Mime)
{
    CLIPBOARD_READ *Read;
    int Pipe[2];

    if (LxSyscall(LX_NR_PIPE2, (long)Pipe, LX_O_CLOEXEC, 0, 0, 0, 0) != 0)
        return;
    Read = LxAllocZero(sizeof(*Read));
    if (Read == NULL)
    {
        CloseFd(Pipe[0]);
        CloseFd(Pipe[1]);
        return;
    }
    LxSyscall(LX_NR_FCNTL, Pipe[0], LX_F_SETFL, LX_O_NONBLOCK, 0, 0, 0);
    Read->Watch.Type = WATCH_CLIPBOARD_READ;
    Read->Watch.Object = Read;
    Read->Fd = Pipe[0];
    Read->Source = Ref(Source);
    WlSend(Source, WL_DATA_SOURCE_EVENT_SEND, Mime, Pipe[1]);
    if (!WatchAdd(Pipe[0], LX_EPOLLIN, &Read->Watch))
    {
        CloseFd(Pipe[0]);
        Unref(Read->Source);
        LxFree(Read);
    }
}

static
void
SnapshotIcon(TOPLEVEL *Toplevel, WL_RESOURCE *IconResource)
{
    ICON *Icon = IconResource != NULL ? IconResource->Data : NULL;
    ICON_SET *Set = NULL;
    uint32_t Index, Slot;

    if (Icon != NULL)
    {
        Icon->Immutable = true;
        Set = LxAllocZero(sizeof(*Set));
        for (Index = 0; Set != NULL && Index < Icon->BufferCount; Index++)
        {
            BUFFER *Buffer = Icon->Buffers[Index].Buffer->Data;
            uint32_t Size = (uint32_t)Buffer->Width;
            uint8_t *Pixels;
            uint32_t Row, Column;

            if (Size == 0 || Size > LX_MAX_ICON || Buffer->Pool == NULL || Buffer->Pool->Map == NULL ||
                (int64_t)Buffer->Offset + (int64_t)Buffer->Stride * Buffer->Height > Buffer->Pool->Size)
            {
                continue;
            }
            for (Slot = 0; Slot < Set->Count; Slot++)
            {
                if (Set->Images[Slot].Size == Size)
                    break;
            }
            if (Slot < Set->Count && Icon->Buffers[Index].Scale != 1)
                continue;
            if (Slot == Set->Count && Set->Count == sizeof(Set->Images) / sizeof(Set->Images[0]))
                continue;
            Pixels = LxAlloc((size_t)Size * Size * 4);
            if (Pixels == NULL)
                continue;
            for (Row = 0; Row < Size; Row++)
            {
                memcpy(Pixels + (size_t)Row * Size * 4,
                       Buffer->Pool->Map + Buffer->Offset + (size_t)Row * Buffer->Stride, (size_t)Size * 4);
                if (Buffer->Format == 1)
                {
                    for (Column = 0; Column < Size; Column++)
                        Pixels[((size_t)Row * Size + Column) * 4 + 3] = 0xFF;
                }
            }
            if (Slot < Set->Count)
            {
                LxFree(Set->Images[Slot].Pixels);
            }
            else
            {
                Set->Count++;
            }
            Set->Images[Slot].Size = Size;
            Set->Images[Slot].Pixels = Pixels;
        }
        if (Set != NULL && Set->Count == 0)
        {
            IconSetFree(Set);
            Set = NULL;
        }
    }
    LxLog("icon for %s: %u images, name %s", Toplevel->AppId != NULL ? Toplevel->AppId : "",
          Set != NULL ? Set->Count : 0, Icon != NULL && Icon->Name != NULL ? Icon->Name : "(none)");
    if (Set != NULL)
    {
        uint32_t I, J;

        for (I = 1; I < Set->Count; I++)
        {
            for (J = I; J > 0 && Set->Images[J - 1].Size > Set->Images[J].Size; J--)
            {
                ICON_IMAGE Swap = Set->Images[J - 1];

                Set->Images[J - 1] = Set->Images[J];
                Set->Images[J] = Swap;
            }
        }
    }
    else
    {
        Set = LxAllocZero(sizeof(*Set));
    }
    IconSetFree(Toplevel->PendingIcon);
    Toplevel->PendingIcon = Set;
    Toplevel->IconPending = Set != NULL;
}

static
void
SurfaceCommit(SURFACE *Surface)
{
    RECT32 Damage[LX_MAX_DAMAGE];
    uint32_t DamageCount = Surface->PendingDamageFull ? 0 : Surface->PendingDamageCount;
    bool Attached = Surface->PendingAttached;
    uint32_t Index;

    if (Attached)
    {
        Unref(Surface->Buffer);
        Surface->Buffer = NULL;
        if (Surface->PendingBuffer != NULL && Surface->PendingBuffer->Alive)
        {
            BUFFER *Buffer = Surface->PendingBuffer->Data;

            Surface->Buffer = Ref(Surface->PendingBuffer);
            Surface->Width = Buffer->Width;
            Surface->Height = Buffer->Height;
        }
        else
        {
            Surface->Width = 0;
            Surface->Height = 0;
        }
        Unref(Surface->PendingBuffer);
        Surface->PendingBuffer = NULL;
    }
    memcpy(Damage, Surface->PendingDamage, DamageCount * sizeof(RECT32));
    Surface->PendingDamageCount = 0;
    Surface->PendingDamageFull = false;
    if (Grow((void **)&Frames, &FrameCapacity, FrameCount + Surface->PendingFrameCount, sizeof(WL_RESOURCE *)))
    {
        for (Index = 0; Index < Surface->PendingFrameCount; Index++)
            Frames[FrameCount++] = Surface->PendingFrames[Index];
    }
    else
    {
        for (Index = 0; Index < Surface->PendingFrameCount; Index++)
            Unref(Surface->PendingFrames[Index]);
    }
    Surface->PendingFrameCount = 0;
    if (Surface->PendingSubSet)
    {
        Surface->SubX = Surface->PendingSubX;
        Surface->SubY = Surface->PendingSubY;
        Surface->PendingSubSet = false;
    }
    Surface->PendingAttached = false;
    if (Surface->Role == ROLE_TOPLEVEL || Surface->Role == ROLE_POPUP)
    {
        if (Surface->Xdg != NULL && Surface->Xdg->Resource->Alive)
            CommitXdg(Surface->Xdg, Damage, DamageCount, Attached);
    }
    else if (Surface->Role == ROLE_SUBSURFACE)
    {
        SURFACE *Root = Surface;

        while (Root->Parent != NULL && Root->Role == ROLE_SUBSURFACE)
            Root = Root->Parent;
        if (Root->Xdg != NULL && Root->Xdg->Window != NULL)
            DrawWindow(Root->Xdg->Window, NULL, 0);
    }
    if (Surface->Buffer != NULL && Attached)
        WlSend(Surface->Buffer, WL_BUFFER_EVENT_RELEASE);
}

static
void
DetachChild(SURFACE *Child)
{
    SURFACE *Parent = Child->Parent;

    if (Parent == NULL)
        return;
    Child->Parent = NULL;
    if (PointerArrayRemove((void **)Parent->Children, &Parent->ChildCount, Child))
        Unref(Child->Resource);
    Unref(Parent->Resource);
}

static
void
ServerDestroyResource(WL_RESOURCE *Resource)
{
    const WL_INTERFACE *Interface = Resource->Interface;
    uint32_t Index;

    if (Interface == &xdg_surface_interface)
    {
        XDG_SURFACE *Xdg = Resource->Data;

        Unmap(Xdg);
        if (Xdg->Surface != NULL)
        {
            if (Xdg->Surface->Xdg == Xdg)
            {
                Xdg->Surface->Xdg = NULL;
                Unref(Resource);
            }
            Unref(Xdg->Surface->Resource);
            Xdg->Surface = NULL;
        }
        Unref(Xdg->Role);
        Xdg->Role = NULL;
    }
    else if (Interface == &xdg_toplevel_interface)
    {
        TOPLEVEL *Toplevel = Resource->Data;

        if (Toplevel->Xdg != NULL)
        {
            Unmap(Toplevel->Xdg);
            Unref(Toplevel->Xdg->Resource);
            Toplevel->Xdg = NULL;
        }
        if (Toplevel->Decoration != NULL)
        {
            WL_RESOURCE *Decoration = Toplevel->Decoration;

            Toplevel->Decoration = NULL;
            if (Decoration->Data == Toplevel)
            {
                Decoration->Data = NULL;
                Unref(Resource);
            }
            Unref(Decoration);
        }
    }
    else if (Interface == &xdg_popup_interface)
    {
        POPUP *Popup = Resource->Data;

        if (Popup->Xdg != NULL)
        {
            Unmap(Popup->Xdg);
            Unref(Popup->Xdg->Resource);
            Popup->Xdg = NULL;
        }
        PointerArrayRemove((void **)Popups, &PopupCount, Popup);
        if (Popup->Parent != NULL)
        {
            Unref(Popup->Parent->Resource);
            Popup->Parent = NULL;
        }
    }
    else if (Interface == &wl_surface_interface)
    {
        SURFACE *Surface = Resource->Data;

        if (PointerFocus == Surface)
            PointerFocus = NULL;
        if (KeyboardFocus == Surface)
            KeyboardFocus = NULL;
        DetachChild(Surface);
        while (Surface->ChildCount != 0)
        {
            SURFACE *Child = Surface->Children[Surface->ChildCount - 1];

            if (Child->Parent == Surface)
            {
                DetachChild(Child);
            }
            else
            {
                Surface->ChildCount--;
                Unref(Child->Resource);
            }
        }
        Unref(Surface->PendingBuffer);
        Surface->PendingBuffer = NULL;
        Unref(Surface->Buffer);
        Surface->Buffer = NULL;
        for (Index = 0; Index < Surface->PendingFrameCount; Index++)
            Unref(Surface->PendingFrames[Index]);
        Surface->PendingFrameCount = 0;
        if (Surface->Xdg != NULL)
        {
            Unref(Surface->Xdg->Resource);
            Surface->Xdg = NULL;
        }
    }
    else if (Interface == &wl_subsurface_interface)
    {
        WL_RESOURCE *SurfaceResource = Resource->Data;

        if (SurfaceResource != NULL)
        {
            DetachChild(SurfaceResource->Data);
            Resource->Data = NULL;
            Unref(SurfaceResource);
        }
    }
    else if (Interface == &wl_shm_pool_interface)
    {
        if (Resource->Data != NULL)
        {
            PoolRelease(Resource->Data);
            Resource->Data = NULL;
        }
    }
    else if (Interface == &wl_data_offer_interface)
    {
        OFFER *Offer = Resource->Data;

        Unref(Offer->Source);
        Offer->Source = NULL;
        HostSourceRelease(Offer->Host);
        Offer->Host = NULL;
    }
    else if (Interface == &zxdg_toplevel_decoration_v1_interface)
    {
        TOPLEVEL *Toplevel = Resource->Data;

        if (Toplevel != NULL)
        {
            Resource->Data = NULL;
            if (Toplevel->Decoration == Resource)
            {
                Toplevel->Decoration = NULL;
                Unref(Resource);
            }
            Unref(Toplevel->Resource);
        }
    }
    else if (Interface == &xdg_toplevel_icon_v1_interface)
    {
        ICON *Icon = Resource->Data;

        for (Index = 0; Index < Icon->BufferCount; Index++)
            Unref(Icon->Buffers[Index].Buffer);
        Icon->BufferCount = 0;
    }
    if (SelectionSource == Resource)
        SetSelection(NULL, NULL);
}

static
void
DisplayRequest(WL_RESOURCE *Display, uint32_t Opcode, WL_ARG *Args)
{
    WL_CLIENT *Client = Display->Client;
    WL_RESOURCE *Resource;
    uint32_t Index;

    switch (Opcode)
    {
        case WL_DISPLAY_SYNC:
            Resource = ClientAdd(Client, Args[0].U, &wl_callback_interface, 1);
            if (Resource == NULL)
                return;
            WlSend(Resource, WL_CALLBACK_EVENT_DONE, Serial);
            ClientRemove(Resource);
            break;
        case WL_DISPLAY_GET_REGISTRY:
            Resource = ClientAdd(Client, Args[0].U, &wl_registry_interface, 1);
            if (Resource == NULL)
                return;
            for (Index = 0; Index < sizeof(Globals) / sizeof(Globals[0]); Index++)
                WlSend(Resource, WL_REGISTRY_EVENT_GLOBAL, Index + 1, Globals[Index].Interface->Name, Globals[Index].Version);
            break;
    }
}

static
void
RegistryBind(WL_RESOURCE *Registry, WL_ARG *Args)
{
    uint32_t Name = Args[0].U, Version = Args[2].U, Index;
    const char *InterfaceName = Args[1].S;
    const GLOBAL *Global;
    WL_RESOURCE *Resource;

    if (Name == 0 || Name > sizeof(Globals) / sizeof(Globals[0]) || InterfaceName == NULL)
    {
        WlError(Registry, 0, "invalid global");
        return;
    }
    Global = &Globals[Name - 1];
    if (strcmp(Global->Interface->Name, InterfaceName) != 0 || Version > Global->Version || Version < 1)
    {
        WlError(Registry, 0, "invalid global");
        return;
    }
    Resource = ClientAdd(Registry->Client, Args[3].U, Global->Interface, Version);
    if (Resource == NULL)
        return;
    if (Global->Interface == &wl_shm_interface)
    {
        WlSend(Resource, WL_SHM_EVENT_FORMAT, 0);
        WlSend(Resource, WL_SHM_EVENT_FORMAT, 1);
    }
    else if (Global->Interface == &wl_seat_interface)
    {
        WlSend(Resource, WL_SEAT_EVENT_CAPABILITIES, 3);
        if (Version >= 2)
            WlSend(Resource, WL_SEAT_EVENT_NAME, "seat0");
    }
    else if (Global->Interface == &wl_output_interface)
    {
        SendOutput(Resource);
    }
    else if (Global->Interface == &xdg_toplevel_icon_manager_v1_interface)
    {
        for (Index = 0; Index < sizeof(IconSizes) / sizeof(IconSizes[0]); Index++)
            WlSend(Resource, XDG_TOPLEVEL_ICON_MANAGER_V1_EVENT_ICON_SIZE, IconSizes[Index]);
        WlSend(Resource, XDG_TOPLEVEL_ICON_MANAGER_V1_EVENT_DONE);
    }
}

static
void
ShmCreatePool(WL_RESOURCE *Shm, WL_ARG *Args)
{
    WL_RESOURCE *Resource = ClientAdd(Shm->Client, Args[0].U, &wl_shm_pool_interface, 1);
    int Fd = Args[1].H;
    int32_t Size = Args[2].I;
    POOL *Pool;
    long Map = 0;

    if (Resource == NULL)
    {
        CloseFd(Fd);
        return;
    }
    if (Size < 0)
    {
        CloseFd(Fd);
        WlError(Shm, 2, "cannot map pool");
        return;
    }
    if (Size > 0)
    {
        Map = LxSyscall(LX_NR_MMAP, 0, Size, LX_PROT_READ, LX_MAP_SHARED, Fd, 0);
        if (Map < 0 && Map > -4096)
        {
            CloseFd(Fd);
            WlError(Shm, 2, "cannot map pool");
            return;
        }
    }
    Pool = LxAllocZero(sizeof(*Pool));
    if (Pool == NULL)
    {
        if (Size > 0)
            LxSyscall(LX_NR_MUNMAP, Map, Size, 0, 0, 0, 0);
        CloseFd(Fd);
        WlError(Shm, 2, "out of memory");
        return;
    }
    Pool->Refs = 1;
    Pool->Fd = Fd;
    Pool->Size = Size;
    Pool->Map = Size > 0 ? (uint8_t *)Map : NULL;
    Resource->Data = Pool;
}

static
void
ShmPoolRequest(WL_RESOURCE *PoolResource, uint32_t Opcode, WL_ARG *Args)
{
    POOL *Pool = PoolResource->Data;
    WL_RESOURCE *Resource;
    BUFFER *Buffer;

    if (Pool == NULL)
        return;
    switch (Opcode)
    {
        case WL_SHM_POOL_CREATE_BUFFER:
        {
            int32_t Offset = Args[1].I, Width = Args[2].I, Height = Args[3].I, Stride = Args[4].I;
            uint32_t Format = Args[5].U;

            if (Format != 0 && Format != 1)
            {
                WlError(PoolResource, 0, "unsupported format");
                return;
            }
            if (Width <= 0 || Height <= 0 || (int64_t)Stride < (int64_t)Width * 4 || Offset < 0 ||
                (int64_t)Offset + (int64_t)Stride * Height > Pool->Size)
            {
                WlError(PoolResource, 1, "invalid buffer geometry");
                return;
            }
            Buffer = LxAllocZero(sizeof(*Buffer));
            if (Buffer == NULL)
            {
                WlError(PoolResource, 2, "out of memory");
                return;
            }
            Resource = ClientAdd(PoolResource->Client, Args[0].U, &wl_buffer_interface, 1);
            if (Resource == NULL)
            {
                LxFree(Buffer);
                return;
            }
            Pool->Refs++;
            Buffer->Pool = Pool;
            Buffer->Offset = Offset;
            Buffer->Width = Width;
            Buffer->Height = Height;
            Buffer->Stride = Stride;
            Buffer->Format = Format;
            Resource->Data = Buffer;
            break;
        }
        case WL_SHM_POOL_RESIZE:
        {
            int32_t Size = Args[0].I;
            long Map;

            if (Size < Pool->Size)
            {
                WlError(PoolResource, 2, "pool cannot shrink");
                return;
            }
            if (Size == Pool->Size)
                return;
            Map = LxSyscall(LX_NR_MMAP, 0, Size, LX_PROT_READ, LX_MAP_SHARED, Pool->Fd, 0);
            if (Map < 0 && Map > -4096)
            {
                WlError(PoolResource, 2, "cannot remap pool");
                return;
            }
            if (Pool->Map != NULL)
                LxSyscall(LX_NR_MUNMAP, (long)Pool->Map, Pool->Size, 0, 0, 0, 0);
            Pool->Map = (uint8_t *)Map;
            Pool->Size = Size;
            break;
        }
    }
}

static
void
SurfaceRequest(WL_RESOURCE *Resource, uint32_t Opcode, WL_ARG *Args)
{
    SURFACE *Surface = Resource->Data;
    WL_RESOURCE *Callback;

    switch (Opcode)
    {
        case WL_SURFACE_ATTACH:
            if (Args[0].O != NULL && Args[0].O->Interface != &wl_buffer_interface)
            {
                WlError(Resource, 2, "not a buffer");
                return;
            }
            Unref(Surface->PendingBuffer);
            Surface->PendingBuffer = Ref(Args[0].O);
            Surface->PendingAttached = true;
            break;
        case WL_SURFACE_DAMAGE:
        case WL_SURFACE_DAMAGE_BUFFER:
            if (Surface->PendingDamageCount == LX_MAX_DAMAGE)
            {
                Surface->PendingDamageFull = true;
                break;
            }
            Surface->PendingDamage[Surface->PendingDamageCount].X = Args[0].I;
            Surface->PendingDamage[Surface->PendingDamageCount].Y = Args[1].I;
            Surface->PendingDamage[Surface->PendingDamageCount].Width = Args[2].I;
            Surface->PendingDamage[Surface->PendingDamageCount].Height = Args[3].I;
            Surface->PendingDamageCount++;
            break;
        case WL_SURFACE_FRAME:
            if (!Grow((void **)&Surface->PendingFrames, &Surface->PendingFrameCapacity, Surface->PendingFrameCount + 1,
                      sizeof(WL_RESOURCE *)))
            {
                WlError(Resource->Client->Display, 2, "out of memory");
                return;
            }
            Callback = ClientAdd(Resource->Client, Args[0].U, &wl_callback_interface, 1);
            if (Callback != NULL)
                Surface->PendingFrames[Surface->PendingFrameCount++] = Ref(Callback);
            break;
        case WL_SURFACE_COMMIT:
            SurfaceCommit(Surface);
            break;
    }
}

static
void
SetCursor(uint32_t Cursor)
{
    WINDOW *Window = PointerWindow();
    uint32_t Values[2] = { Cursor, 0 };

    if (Window != NULL)
        HostSendValues(LX_WIN_CURSOR, Window->Id, 2, Values);
}

static
void
SeatRequest(WL_RESOURCE *Seat, uint32_t Opcode, WL_ARG *Args)
{
    WL_RESOURCE *Resource;

    switch (Opcode)
    {
        case WL_SEAT_GET_POINTER:
            ClientAdd(Seat->Client, Args[0].U, &wl_pointer_interface, Seat->Version);
            break;
        case WL_SEAT_GET_KEYBOARD:
        {
            long Fd;

            Resource = ClientAdd(Seat->Client, Args[0].U, &wl_keyboard_interface, Seat->Version);
            if (Resource == NULL)
                return;
            Fd = LxSyscall(LX_NR_MEMFD_CREATE, (long)"lxwayland-keymap", LX_MFD_CLOEXEC, 0, 0, 0, 0);
            if (Fd >= 0 && LxSyscall(LX_NR_WRITE, Fd, (long)Keymap, sizeof(Keymap), 0, 0, 0) == (long)sizeof(Keymap))
            {
                WlSend(Resource, WL_KEYBOARD_EVENT_KEYMAP, 1, (int)Fd, (uint32_t)sizeof(Keymap));
            }
            else
            {
                CloseFd((int)Fd);
                Fd = LxSyscall(LX_NR_OPENAT, LX_AT_FDCWD, (long)"/dev/null", LX_O_RDONLY | LX_O_CLOEXEC, 0, 0, 0);
                if (Fd >= 0)
                    WlSend(Resource, WL_KEYBOARD_EVENT_KEYMAP, 0, (int)Fd, 0);
            }
            if (Resource->Version >= 4)
                WlSend(Resource, WL_KEYBOARD_EVENT_REPEAT_INFO, 25, 600);
            if (KeyboardFocus != NULL && KeyboardFocus->Resource->Client == Seat->Client)
                KeyboardEnter(Resource, KeyboardFocus);
            break;
        }
        case WL_SEAT_GET_TOUCH:
            ClientAdd(Seat->Client, Args[0].U, &wl_touch_interface, Seat->Version);
            break;
    }
}

static
void
DataDeviceManagerRequest(WL_RESOURCE *Manager, uint32_t Opcode, WL_ARG *Args)
{
    WL_RESOURCE *Resource;
    DATA_SOURCE *Source;

    switch (Opcode)
    {
        case WL_DATA_DEVICE_MANAGER_CREATE_DATA_SOURCE:
            Source = LxAllocZero(sizeof(*Source));
            if (Source == NULL)
            {
                WlError(Manager->Client->Display, 2, "out of memory");
                return;
            }
            Resource = ClientAdd(Manager->Client, Args[0].U, &wl_data_source_interface, Manager->Version);
            if (Resource == NULL)
            {
                LxFree(Source);
                return;
            }
            Resource->Data = Source;
            break;
        case WL_DATA_DEVICE_MANAGER_GET_DATA_DEVICE:
            Resource = ClientAdd(Manager->Client, Args[0].U, &wl_data_device_interface, Manager->Version);
            if (Resource == NULL)
                return;
            if (KeyboardFocus != NULL && KeyboardFocus->Resource->Client == Manager->Client)
                OfferSelection(Resource);
            break;
    }
}

static
void
DataDeviceSetSelection(WL_RESOURCE *Source)
{
    DATA_SOURCE *Data;
    uint32_t Type, Index;

    if (Source != NULL && Source->Interface != &wl_data_source_interface)
        Source = NULL;
    SetSelection(Source, NULL);
    if (Source == NULL || !Host)
        return;
    Data = Source->Data;
    for (Type = 0; Type < sizeof(TextTypes) / sizeof(TextTypes[0]); Type++)
    {
        for (Index = 0; Index < Data->MimeCount; Index++)
        {
            if (strcmp(Data->Mimes[Index], TextTypes[Type]) == 0)
            {
                ClipboardReadStart(Source, TextTypes[Type]);
                return;
            }
        }
    }
}

static
void
DataOfferReceive(WL_RESOURCE *Resource, const char *Mime, int Fd)
{
    OFFER *Offer = Resource->Data;

    if (Offer->Host != NULL)
        ClipboardWriteStart(Fd, Offer->Host);
    else if (Offer->Source != NULL && Offer->Source->Alive)
        WlSend(Offer->Source, WL_DATA_SOURCE_EVENT_SEND, Mime != NULL ? Mime : "", Fd);
    else
        CloseFd(Fd);
}

static
void
XdgWmBaseRequest(WL_RESOURCE *Base, uint32_t Opcode, WL_ARG *Args)
{
    WL_RESOURCE *Resource;

    switch (Opcode)
    {
        case XDG_WM_BASE_CREATE_POSITIONER:
        {
            POSITIONER *Positioner = LxAllocZero(sizeof(*Positioner));

            if (Positioner == NULL)
            {
                WlError(Base->Client->Display, 2, "out of memory");
                return;
            }
            Resource = ClientAdd(Base->Client, Args[0].U, &xdg_positioner_interface, Base->Version);
            if (Resource == NULL)
            {
                LxFree(Positioner);
                return;
            }
            Positioner->Width = 1;
            Positioner->Height = 1;
            Positioner->Anchor.Width = 1;
            Positioner->Anchor.Height = 1;
            Resource->Data = Positioner;
            break;
        }
        case XDG_WM_BASE_GET_XDG_SURFACE:
        {
            SURFACE *Surface = Args[1].O->Data;
            XDG_SURFACE *Xdg;

            if (Args[1].O->Interface != &wl_surface_interface)
            {
                WlError(Base, 0, "not a surface");
                return;
            }
            Xdg = LxAllocZero(sizeof(*Xdg));
            if (Xdg == NULL)
            {
                WlError(Base->Client->Display, 2, "out of memory");
                return;
            }
            Resource = ClientAdd(Base->Client, Args[0].U, &xdg_surface_interface, Base->Version);
            if (Resource == NULL)
            {
                LxFree(Xdg);
                return;
            }
            Resource->Data = Xdg;
            Xdg->Resource = Resource;
            Xdg->Surface = Surface;
            Ref(Surface->Resource);
            if (Surface->Xdg != NULL)
                Unref(Surface->Xdg->Resource);
            Surface->Xdg = Xdg;
            Ref(Resource);
            break;
        }
    }
}

static
void
PositionerRequest(WL_RESOURCE *Resource, uint32_t Opcode, WL_ARG *Args)
{
    POSITIONER *Positioner = Resource->Data;

    switch (Opcode)
    {
        case XDG_POSITIONER_SET_SIZE:
            Positioner->Width = Args[0].I;
            Positioner->Height = Args[1].I;
            break;
        case XDG_POSITIONER_SET_ANCHOR_RECT:
            Positioner->Anchor.X = Args[0].I;
            Positioner->Anchor.Y = Args[1].I;
            Positioner->Anchor.Width = Args[2].I;
            Positioner->Anchor.Height = Args[3].I;
            break;
        case XDG_POSITIONER_SET_ANCHOR:
            Positioner->AnchorEdge = Args[0].U;
            break;
        case XDG_POSITIONER_SET_GRAVITY:
            Positioner->Gravity = Args[0].U;
            break;
        case XDG_POSITIONER_SET_OFFSET:
            Positioner->OffsetX = Args[0].I;
            Positioner->OffsetY = Args[1].I;
            break;
    }
}

static
void
XdgSurfaceRequest(WL_RESOURCE *Resource, uint32_t Opcode, WL_ARG *Args)
{
    XDG_SURFACE *Xdg = Resource->Data;
    WL_RESOURCE *Role;

    switch (Opcode)
    {
        case XDG_SURFACE_GET_TOPLEVEL:
        {
            TOPLEVEL *Toplevel = LxAllocZero(sizeof(*Toplevel));
            static const uint32_t Capabilities[] = { 2, 3, 4 };

            if (Toplevel == NULL)
            {
                WlError(Resource->Client->Display, 2, "out of memory");
                return;
            }
            Role = ClientAdd(Resource->Client, Args[0].U, &xdg_toplevel_interface, Resource->Version);
            if (Role == NULL)
            {
                LxFree(Toplevel);
                return;
            }
            Role->Data = Toplevel;
            Toplevel->Resource = Role;
            Toplevel->Xdg = Xdg;
            Ref(Resource);
            Unref(Xdg->Role);
            Xdg->Role = Ref(Role);
            Xdg->RoleType = ROLE_TOPLEVEL;
            if (Xdg->Surface != NULL)
                Xdg->Surface->Role = ROLE_TOPLEVEL;
            if (Resource->Version >= 5)
                WlSend(Role, XDG_TOPLEVEL_EVENT_WM_CAPABILITIES, Capabilities, (uint32_t)sizeof(Capabilities));
            break;
        }
        case XDG_SURFACE_GET_POPUP:
        {
            POPUP *Popup;
            XDG_SURFACE *Parent;

            if (Args[1].O == NULL || Args[1].O->Interface != &xdg_surface_interface)
            {
                WlError(Resource, 0, "popups without a parent are not supported");
                return;
            }
            if (Args[2].O->Interface != &xdg_positioner_interface)
            {
                WlError(Resource, 0, "invalid positioner");
                return;
            }
            Parent = Args[1].O->Data;
            Popup = LxAllocZero(sizeof(*Popup));
            if (Popup == NULL)
            {
                WlError(Resource->Client->Display, 2, "out of memory");
                return;
            }
            Role = ClientAdd(Resource->Client, Args[0].U, &xdg_popup_interface, Resource->Version);
            if (Role == NULL)
            {
                LxFree(Popup);
                return;
            }
            Role->Data = Popup;
            Popup->Resource = Role;
            Popup->Xdg = Xdg;
            Ref(Resource);
            Popup->Parent = Parent;
            Ref(Parent->Resource);
            Popup->Positioner = *(POSITIONER *)Args[2].O->Data;
            Popup->Geometry = PlacePopup(&Popup->Positioner);
            Unref(Xdg->Role);
            Xdg->Role = Ref(Role);
            Xdg->RoleType = ROLE_POPUP;
            if (Xdg->Surface != NULL)
                Xdg->Surface->Role = ROLE_POPUP;
            break;
        }
        case XDG_SURFACE_SET_WINDOW_GEOMETRY:
            Xdg->PendingGeometry.X = Args[0].I;
            Xdg->PendingGeometry.Y = Args[1].I;
            Xdg->PendingGeometry.Width = Args[2].I;
            Xdg->PendingGeometry.Height = Args[3].I;
            Xdg->HasPendingGeometry = true;
            break;
    }
}

static
void
ToplevelRequest(WL_RESOURCE *Resource, uint32_t Opcode, WL_ARG *Args)
{
    TOPLEVEL *Toplevel = Resource->Data;
    WINDOW *Window = Toplevel->Xdg != NULL ? Toplevel->Xdg->Window : NULL;
    uint32_t Values[2];

    switch (Opcode)
    {
        case XDG_TOPLEVEL_SET_TITLE:
            LxFree(Toplevel->Title);
            Toplevel->Title = LxStrDup(Args[0].S != NULL ? Args[0].S : "");
            if (Window != NULL && Toplevel->Title != NULL)
                HostSend(LX_WIN_TITLE, Window->Id, Toplevel->Title, strlen(Toplevel->Title));
            break;
        case XDG_TOPLEVEL_SET_APP_ID:
        {
            const char *AppId = Args[0].S != NULL ? Args[0].S : "";
            bool Changed = Toplevel->AppId == NULL || strcmp(Toplevel->AppId, AppId) != 0;

            if (!Changed)
                break;
            LxFree(Toplevel->AppId);
            Toplevel->AppId = LxStrDup(AppId);
            if (Window != NULL && Toplevel->AppId != NULL && Toplevel->AppId[0] != '\0')
                SendAppInfo(Window, Toplevel);
            break;
        }
        case XDG_TOPLEVEL_SET_MIN_SIZE:
            Toplevel->MinWidth = Args[0].I;
            Toplevel->MinHeight = Args[1].I;
            SendSizes(Toplevel);
            break;
        case XDG_TOPLEVEL_SET_MAX_SIZE:
            Toplevel->MaxWidth = Args[0].I;
            Toplevel->MaxHeight = Args[1].I;
            SendSizes(Toplevel);
            break;
        case XDG_TOPLEVEL_MOVE:
            if (Window != NULL)
                HostSend(LX_WIN_MOVE, Window->Id, NULL, 0);
            break;
        case XDG_TOPLEVEL_RESIZE:
            if (Window != NULL)
            {
                Values[0] = Args[2].U;
                Values[1] = 0;
                HostSendValues(LX_WIN_RESIZE, Window->Id, 2, Values);
            }
            break;
        case XDG_TOPLEVEL_SET_MAXIMIZED:
        case XDG_TOPLEVEL_SET_FULLSCREEN:
            RequestState(Toplevel, LX_WIN_STATE_MAXIMIZED);
            break;
        case XDG_TOPLEVEL_UNSET_MAXIMIZED:
        case XDG_TOPLEVEL_UNSET_FULLSCREEN:
            RequestState(Toplevel, LX_WIN_STATE_NORMAL);
            break;
        case XDG_TOPLEVEL_SET_MINIMIZED:
            RequestState(Toplevel, LX_WIN_STATE_MINIMIZED);
            break;
    }
}

static
void
PopupRequest(WL_RESOURCE *Resource, uint32_t Opcode, WL_ARG *Args)
{
    POPUP *Popup = Resource->Data;

    switch (Opcode)
    {
        case XDG_POPUP_GRAB:
            Popup->Grab = true;
            break;
        case XDG_POPUP_REPOSITION:
            if (Args[0].O->Interface != &xdg_positioner_interface)
            {
                WlError(Resource, 0, "invalid positioner");
                return;
            }
            Popup->Positioner = *(POSITIONER *)Args[0].O->Data;
            Popup->Geometry = PlacePopup(&Popup->Positioner);
            WlSend(Resource, XDG_POPUP_EVENT_REPOSITIONED, Args[1].U);
            WlSend(Resource, XDG_POPUP_EVENT_CONFIGURE, Popup->Geometry.X, Popup->Geometry.Y, Popup->Geometry.Width,
                   Popup->Geometry.Height);
            if (Popup->Xdg != NULL)
                SendXdgConfigure(Popup->Xdg);
            break;
    }
}

static
void
DecorationManagerRequest(WL_RESOURCE *Manager, uint32_t Opcode, WL_ARG *Args)
{
    WL_RESOURCE *Resource;
    TOPLEVEL *Toplevel;

    if (Opcode != ZXDG_DECORATION_MANAGER_V1_GET_TOPLEVEL_DECORATION)
        return;
    if (Args[1].O->Interface != &xdg_toplevel_interface)
    {
        WlError(Manager, 0, "not a toplevel");
        return;
    }
    Toplevel = Args[1].O->Data;
    Resource = ClientAdd(Manager->Client, Args[0].U, &zxdg_toplevel_decoration_v1_interface, 1);
    if (Resource == NULL)
        return;
    Resource->Data = Toplevel;
    Ref(Toplevel->Resource);
    Unref(Toplevel->Decoration);
    Toplevel->Decoration = Ref(Resource);
    Toplevel->Decorated = true;
    WlSend(Resource, ZXDG_TOPLEVEL_DECORATION_V1_EVENT_CONFIGURE, 2);
}

static
void
DecorationRequest(WL_RESOURCE *Resource, uint32_t Opcode, WL_ARG *Args)
{
    TOPLEVEL *Toplevel = Resource->Data;

    if (Toplevel == NULL)
        return;
    switch (Opcode)
    {
        case ZXDG_TOPLEVEL_DECORATION_V1_SET_MODE:
            Toplevel->Decorated = Args[0].U == 2;
            WlSend(Resource, ZXDG_TOPLEVEL_DECORATION_V1_EVENT_CONFIGURE, Args[0].U);
            break;
        case ZXDG_TOPLEVEL_DECORATION_V1_UNSET_MODE:
            Toplevel->Decorated = true;
            WlSend(Resource, ZXDG_TOPLEVEL_DECORATION_V1_EVENT_CONFIGURE, 2);
            break;
    }
}

static
void
IconManagerRequest(WL_RESOURCE *Manager, uint32_t Opcode, WL_ARG *Args)
{
    WL_RESOURCE *Resource;
    ICON *Icon;

    switch (Opcode)
    {
        case XDG_TOPLEVEL_ICON_MANAGER_V1_CREATE_ICON:
            Icon = LxAllocZero(sizeof(*Icon));
            if (Icon == NULL)
            {
                WlError(Manager->Client->Display, 2, "out of memory");
                return;
            }
            Resource = ClientAdd(Manager->Client, Args[0].U, &xdg_toplevel_icon_v1_interface, 1);
            if (Resource == NULL)
            {
                LxFree(Icon);
                return;
            }
            Resource->Data = Icon;
            break;
        case XDG_TOPLEVEL_ICON_MANAGER_V1_SET_ICON:
        {
            WL_RESOURCE *IconResource = Args[1].O;
            uint32_t Index;

            if (Args[0].O->Interface != &xdg_toplevel_interface ||
                (IconResource != NULL && IconResource->Interface != &xdg_toplevel_icon_v1_interface))
            {
                WlError(Manager, 0, "invalid object");
                return;
            }
            if (IconResource != NULL)
            {
                ICON *Data = IconResource->Data;

                for (Index = 0; Index < Data->BufferCount; Index++)
                {
                    if (!Data->Buffers[Index].Buffer->Alive)
                    {
                        WlError(IconResource, 3, "icon buffer was destroyed");
                        return;
                    }
                }
            }
            SnapshotIcon(Args[0].O->Data, IconResource);
            break;
        }
    }
}

static
void
IconRequest(WL_RESOURCE *Resource, uint32_t Opcode, WL_ARG *Args)
{
    ICON *Icon = Resource->Data;
    BUFFER *Buffer;
    uint32_t Index;

    switch (Opcode)
    {
        case XDG_TOPLEVEL_ICON_V1_SET_NAME:
            if (Icon->Immutable)
            {
                WlError(Resource, 2, "icon is immutable");
                return;
            }
            LxFree(Icon->Name);
            Icon->Name = LxStrDup(Args[0].S != NULL ? Args[0].S : "");
            break;
        case XDG_TOPLEVEL_ICON_V1_ADD_BUFFER:
            if (Icon->Immutable)
            {
                WlError(Resource, 2, "icon is immutable");
                return;
            }
            if (Args[0].O->Interface != &wl_buffer_interface)
            {
                WlError(Resource, 1, "not a buffer");
                return;
            }
            Buffer = Args[0].O->Data;
            if (Buffer->Width != Buffer->Height)
            {
                WlError(Resource, 1, "icon buffer is not square");
                return;
            }
            for (Index = 0; Index < Icon->BufferCount; Index++)
            {
                BUFFER *Other = Icon->Buffers[Index].Buffer->Data;

                if (Other->Width == Buffer->Width && Icon->Buffers[Index].Scale == Args[1].I)
                    break;
            }
            if (Index == sizeof(Icon->Buffers) / sizeof(Icon->Buffers[0]))
                return;
            if (Index < Icon->BufferCount)
                Unref(Icon->Buffers[Index].Buffer);
            else
                Icon->BufferCount++;
            Icon->Buffers[Index].Buffer = Ref(Args[0].O);
            Icon->Buffers[Index].Scale = Args[1].I;
            break;
    }
}

static
void
Dispatch(WL_RESOURCE *Resource, uint32_t Opcode, WL_ARG *Args)
{
    const WL_INTERFACE *Interface = Resource->Interface;
    WL_RESOURCE *Created;

    if (Interface == &wl_display_interface)
    {
        DisplayRequest(Resource, Opcode, Args);
    }
    else if (Interface == &wl_registry_interface)
    {
        if (Opcode == WL_REGISTRY_BIND)
            RegistryBind(Resource, Args);
    }
    else if (Interface == &wl_compositor_interface)
    {
        if (Opcode == WL_COMPOSITOR_CREATE_SURFACE)
        {
            SURFACE *Surface = LxAllocZero(sizeof(*Surface));

            if (Surface == NULL)
            {
                WlError(Resource->Client->Display, 2, "out of memory");
                return;
            }
            Created = ClientAdd(Resource->Client, Args[0].U, &wl_surface_interface, Resource->Version);
            if (Created == NULL)
            {
                LxFree(Surface);
                return;
            }
            Surface->Resource = Created;
            Created->Data = Surface;
        }
        else if (Opcode == WL_COMPOSITOR_CREATE_REGION)
        {
            ClientAdd(Resource->Client, Args[0].U, &wl_region_interface, 1);
        }
    }
    else if (Interface == &wl_shm_interface)
    {
        if (Opcode == WL_SHM_CREATE_POOL)
            ShmCreatePool(Resource, Args);
    }
    else if (Interface == &wl_shm_pool_interface)
    {
        ShmPoolRequest(Resource, Opcode, Args);
    }
    else if (Interface == &wl_surface_interface)
    {
        SurfaceRequest(Resource, Opcode, Args);
    }
    else if (Interface == &wl_subcompositor_interface)
    {
        if (Opcode == WL_SUBCOMPOSITOR_GET_SUBSURFACE)
        {
            SURFACE *Surface, *Parent;

            if (Args[1].O->Interface != &wl_surface_interface || Args[2].O->Interface != &wl_surface_interface ||
                Args[1].O == Args[2].O)
            {
                WlError(Resource, 1, "bad surface");
                return;
            }
            Surface = Args[1].O->Data;
            Parent = Args[2].O->Data;
            if (!Grow((void **)&Parent->Children, &Parent->ChildCapacity, Parent->ChildCount + 1, sizeof(SURFACE *)))
            {
                WlError(Resource->Client->Display, 2, "out of memory");
                return;
            }
            Created = ClientAdd(Resource->Client, Args[0].U, &wl_subsurface_interface, 1);
            if (Created == NULL)
                return;
            DetachChild(Surface);
            Surface->Role = ROLE_SUBSURFACE;
            Surface->Parent = Parent;
            Ref(Parent->Resource);
            Parent->Children[Parent->ChildCount++] = Surface;
            Ref(Surface->Resource);
            Created->Data = Ref(Surface->Resource);
        }
    }
    else if (Interface == &wl_subsurface_interface)
    {
        WL_RESOURCE *SurfaceResource = Resource->Data;

        if (Opcode == WL_SUBSURFACE_SET_POSITION && SurfaceResource != NULL)
        {
            SURFACE *Surface = SurfaceResource->Data;

            Surface->PendingSubSet = true;
            Surface->PendingSubX = Args[0].I;
            Surface->PendingSubY = Args[1].I;
        }
    }
    else if (Interface == &wl_seat_interface)
    {
        SeatRequest(Resource, Opcode, Args);
    }
    else if (Interface == &wl_pointer_interface)
    {
        if (Opcode == WL_POINTER_SET_CURSOR)
        {
            if (Args[1].O != NULL && Args[1].O->Interface == &wl_surface_interface)
            {
                SURFACE *Surface = Args[1].O->Data;

                if (Surface->Role == ROLE_NONE)
                    Surface->Role = ROLE_CURSOR;
            }
            SetCursor(Args[1].O == NULL ? LX_WIN_CURSOR_HIDDEN : LX_WIN_CURSOR_DEFAULT);
        }
    }
    else if (Interface == &wp_cursor_shape_manager_v1_interface)
    {
        if (Opcode == WP_CURSOR_SHAPE_MANAGER_V1_GET_POINTER)
            ClientAdd(Resource->Client, Args[0].U, &wp_cursor_shape_device_v1_interface, 1);
    }
    else if (Interface == &wp_cursor_shape_device_v1_interface)
    {
        if (Opcode == WP_CURSOR_SHAPE_DEVICE_V1_SET_SHAPE)
            SetCursor(Args[1].U < sizeof(CursorShapes) ? CursorShapes[Args[1].U] : 0);
    }
    else if (Interface == &wl_data_device_manager_interface)
    {
        DataDeviceManagerRequest(Resource, Opcode, Args);
    }
    else if (Interface == &wl_data_source_interface)
    {
        if (Opcode == WL_DATA_SOURCE_OFFER)
        {
            DATA_SOURCE *Source = Resource->Data;
            char *Mime = LxStrDup(Args[0].S != NULL ? Args[0].S : "");

            if (Mime == NULL || !Grow((void **)&Source->Mimes, &Source->MimeCapacity, Source->MimeCount + 1, sizeof(char *)))
            {
                LxFree(Mime);
                WlError(Resource->Client->Display, 2, "out of memory");
                return;
            }
            Source->Mimes[Source->MimeCount++] = Mime;
        }
    }
    else if (Interface == &wl_data_device_interface)
    {
        if (Opcode == WL_DATA_DEVICE_START_DRAG)
        {
            if (Args[0].O != NULL && Args[0].O->Interface == &wl_data_source_interface)
                WlSend(Args[0].O, WL_DATA_SOURCE_EVENT_CANCELLED);
        }
        else if (Opcode == WL_DATA_DEVICE_SET_SELECTION)
        {
            DataDeviceSetSelection(Args[0].O);
        }
    }
    else if (Interface == &wl_data_offer_interface)
    {
        if (Opcode == WL_DATA_OFFER_RECEIVE)
            DataOfferReceive(Resource, Args[0].S, Args[1].H);
    }
    else if (Interface == &xdg_wm_base_interface)
    {
        XdgWmBaseRequest(Resource, Opcode, Args);
    }
    else if (Interface == &xdg_positioner_interface)
    {
        PositionerRequest(Resource, Opcode, Args);
    }
    else if (Interface == &xdg_surface_interface)
    {
        XdgSurfaceRequest(Resource, Opcode, Args);
    }
    else if (Interface == &xdg_toplevel_interface)
    {
        ToplevelRequest(Resource, Opcode, Args);
    }
    else if (Interface == &xdg_popup_interface)
    {
        PopupRequest(Resource, Opcode, Args);
    }
    else if (Interface == &zxdg_decoration_manager_v1_interface)
    {
        DecorationManagerRequest(Resource, Opcode, Args);
    }
    else if (Interface == &zxdg_toplevel_decoration_v1_interface)
    {
        DecorationRequest(Resource, Opcode, Args);
    }
    else if (Interface == &xdg_toplevel_icon_manager_v1_interface)
    {
        IconManagerRequest(Resource, Opcode, Args);
    }
    else if (Interface == &xdg_toplevel_icon_v1_interface)
    {
        IconRequest(Resource, Opcode, Args);
    }
}

static
bool
Decode(WL_CLIENT *Client, const WL_MESSAGE *Message, const uint8_t *Body, uint32_t Length, WL_ARG *Args,
       uint32_t *ArgCount, uint32_t *FdsUsed)
{
    const char *Signature;
    uint32_t Offset = 0, Count = 0, Fds = 0;
    bool Nullable = false;

    for (Signature = Message->Signature; *Signature != '\0'; Signature++)
    {
        uint32_t Value;

        if (*Signature == '?')
        {
            Nullable = true;
            continue;
        }
        if (Count == LX_MAX_ARGS)
            return false;
        if (*Signature == 'h')
        {
            if (Fds >= Client->InFdCount)
            {
                WlError(Client->Display, 1, "missing file descriptor");
                return false;
            }
            Args[Count++].H = Client->InFds[Fds++];
            Nullable = false;
            continue;
        }
        if (Offset + 4 > Length)
            return false;
        memcpy(&Value, Body + Offset, 4);
        Offset += 4;
        switch (*Signature)
        {
            case 'i':
            case 'u':
            case 'f':
            case 'n':
                Args[Count++].U = Value;
                break;
            case 'o':
                Args[Count].O = Value != 0 ? ClientLookup(Client, Value) : NULL;
                if (Args[Count].O == NULL && !Nullable)
                {
                    WlError(Client->Display, 0, "invalid object");
                    return false;
                }
                Count++;
                break;
            case 's':
            case 'a':
            {
                uint32_t Padded = (Value + 3) & ~3u;

                if (Value > Length - Offset || Padded > Length - Offset)
                    return false;
                if (*Signature == 's')
                {
                    if (Value != 0 && Body[Offset + Value - 1] != '\0')
                        return false;
                    Args[Count++].S = Value != 0 ? (const char *)Body + Offset : NULL;
                }
                else
                {
                    Args[Count].A.Data = Body + Offset;
                    Args[Count].A.Size = Value;
                    Count++;
                }
                Offset += Padded;
                break;
            }
            default:
                return false;
        }
        Nullable = false;
    }
    *ArgCount = Count;
    *FdsUsed = Fds;
    return true;
}

static
void
ClientDrop(WL_CLIENT *Client)
{
    WL_CLIENT **Link;
    uint32_t Index;

    if (Client->Closed)
        return;
    Client->Closed = true;
    LxLog("client %d disconnected", Client->Pid);
    WatchRemove(Client->Socket);
    for (Index = Client->ObjectCapacity; Index-- != 0;)
    {
        WL_RESOURCE *Resource = Client->Objects[Index];

        if (Resource != NULL)
            DestroyResource(Resource);
    }
    while (Client->ServerCount != 0)
        DestroyResource(Client->ServerObjects[Client->ServerCount - 1]);
    for (Index = 0; Index < Client->InFdCount; Index++)
        CloseFd(Client->InFds[Index]);
    Client->InFdCount = 0;
    for (Index = 0; Index < Client->OutFdCount; Index++)
        CloseFd(Client->OutFds[Index].Fd);
    Client->OutFdCount = 0;
    CloseFd(Client->Socket);
    Client->Socket = -1;
    for (Link = &Clients; *Link != NULL; Link = &(*Link)->Next)
    {
        if (*Link == Client)
        {
            *Link = Client->Next;
            break;
        }
    }
    ClientUnref(Client);
}

static
void
ClientProcess(WL_CLIENT *Client)
{
    size_t Start = 0;

    while (Client->InLength - Start >= 8 && !Client->Failed && !Client->Closed)
    {
        uint32_t Id, Word, Size, Opcode, ArgCount, FdsUsed;
        const WL_MESSAGE *Message;
        WL_RESOURCE *Resource;
        WL_ARG Args[LX_MAX_ARGS];

        memcpy(&Id, Client->In + Start, 4);
        memcpy(&Word, Client->In + Start + 4, 4);
        Size = Word >> 16;
        Opcode = Word & 0xFFFF;
        if (Size < 8 || (Size & 3) != 0)
        {
            Client->Failed = true;
            break;
        }
        if (Client->InLength - Start < Size)
            break;
        Resource = ClientLookup(Client, Id);
        if (Resource == NULL)
        {
            Start += Size;
            continue;
        }
        if (Opcode >= Resource->Interface->RequestCount)
        {
            WlError(Resource, 1, "invalid opcode");
            break;
        }
        Message = &Resource->Interface->Requests[Opcode];
        if (!Decode(Client, Message, Client->In + Start + 8, Size - 8, Args, &ArgCount, &FdsUsed))
        {
            if (!Client->Failed)
            {
                LxLog("malformed request %s.%s", Resource->Interface->Name, Message->Name);
                Client->Failed = true;
            }
            break;
        }
        memmove(Client->InFds, Client->InFds + FdsUsed, (Client->InFdCount - FdsUsed) * sizeof(int));
        Client->InFdCount -= FdsUsed;
        Start += Size;
        Ref(Resource);
        Dispatch(Resource, Opcode, Args);
        if (Message->Destructor && Resource->Alive && !Client->Failed)
            DestroyResource(Resource);
        Unref(Resource);
    }
    if (Start != 0)
    {
        memmove(Client->In, Client->In + Start, Client->InLength - Start);
        Client->InLength -= Start;
    }
}

static
void
ClientReceive(WL_CLIENT *Client)
{
    uint8_t Control[LX_CMSG_SPACE(LX_MAX_FDS * sizeof(int))];
    LX_IOVEC Iov;
    LX_MSGHDR Header;
    LX_CMSGHDR *Cmsg;
    long Result;

    if (!GrowBytes(&Client->In, &Client->InCapacity, Client->InLength + 65536))
    {
        Client->Failed = true;
        return;
    }
    Iov.Base = Client->In + Client->InLength;
    Iov.Length = 65536;
    memset(&Header, 0, sizeof(Header));
    Header.Iov = &Iov;
    Header.IovLength = 1;
    Header.Control = Control;
    Header.ControlLength = sizeof(Control);
    Result = LxSyscall(LX_NR_RECVMSG, Client->Socket, (long)&Header, LX_MSG_DONTWAIT | LX_MSG_CMSG_CLOEXEC, 0, 0, 0);
    if (Result == -LX_EAGAIN || Result == -LX_EINTR)
        return;
    if (Result < 0)
    {
        Client->Failed = true;
        return;
    }
    if (Header.ControlLength >= sizeof(LX_CMSGHDR))
    {
        size_t Offset = 0;

        while (Offset + sizeof(LX_CMSGHDR) <= Header.ControlLength)
        {
            Cmsg = (LX_CMSGHDR *)(Control + Offset);
            if (Cmsg->Length < sizeof(LX_CMSGHDR) || Offset + Cmsg->Length > Header.ControlLength)
                break;
            if (Cmsg->Level == LX_SOL_SOCKET && Cmsg->Type == LX_SCM_RIGHTS)
            {
                uint32_t Count = (uint32_t)((Cmsg->Length - LX_CMSG_LEN(0)) / sizeof(int));
                uint32_t Index;

                if (Grow((void **)&Client->InFds, &Client->InFdCapacity, Client->InFdCount + Count, sizeof(int)))
                {
                    memcpy(Client->InFds + Client->InFdCount, LX_CMSG_DATA(Cmsg), Count * sizeof(int));
                    Client->InFdCount += Count;
                }
                else
                {
                    for (Index = 0; Index < Count; Index++)
                        CloseFd(((int *)LX_CMSG_DATA(Cmsg))[Index]);
                    Client->Failed = true;
                }
            }
            Offset += LX_CMSG_ALIGN(Cmsg->Length);
        }
    }
    if (Result == 0)
    {
        Client->Failed = true;
        return;
    }
    Client->InLength += (size_t)Result;
    ClientProcess(Client);
}

static
bool
ClientFlush(WL_CLIENT *Client)
{
    while (Client->OutLength != 0 && !Client->Closed)
    {
        uint8_t Control[LX_CMSG_SPACE(LX_MAX_FDS * sizeof(int))];
        uint32_t FdCount = Client->OutFdCount < LX_MAX_FDS ? Client->OutFdCount : LX_MAX_FDS;
        size_t Limit = Client->OutLength;
        LX_IOVEC Iov;
        LX_MSGHDR Header;
        uint32_t Index;
        long Result;

        if (Client->OutFdCount > LX_MAX_FDS)
            Limit = Client->OutFds[LX_MAX_FDS].Offset;
        if (Limit == 0)
        {
            Client->Failed = true;
            return true;
        }
        Iov.Base = Client->Out;
        Iov.Length = Limit;
        memset(&Header, 0, sizeof(Header));
        Header.Iov = &Iov;
        Header.IovLength = 1;
        if (FdCount != 0)
        {
            LX_CMSGHDR *Cmsg = (LX_CMSGHDR *)Control;

            memset(Control, 0, sizeof(Control));
            Cmsg->Length = LX_CMSG_LEN(FdCount * sizeof(int));
            Cmsg->Level = LX_SOL_SOCKET;
            Cmsg->Type = LX_SCM_RIGHTS;
            for (Index = 0; Index < FdCount; Index++)
                ((int *)LX_CMSG_DATA(Cmsg))[Index] = Client->OutFds[Index].Fd;
            Header.Control = Control;
            Header.ControlLength = LX_CMSG_SPACE(FdCount * sizeof(int));
        }
        Result = LxSyscall(LX_NR_SENDMSG, Client->Socket, (long)&Header, LX_MSG_DONTWAIT | LX_MSG_NOSIGNAL, 0, 0, 0);
        if (Result == -LX_EAGAIN)
            return false;
        if (Result == -LX_EINTR)
            continue;
        if (Result <= 0)
        {
            Client->Failed = true;
            return true;
        }
        for (Index = 0; Index < FdCount; Index++)
            CloseFd(Client->OutFds[Index].Fd);
        memmove(Client->OutFds, Client->OutFds + FdCount, (Client->OutFdCount - FdCount) * sizeof(OUT_FD));
        Client->OutFdCount -= FdCount;
        for (Index = 0; Index < Client->OutFdCount; Index++)
            Client->OutFds[Index].Offset -= (size_t)Result;
        memmove(Client->Out, Client->Out + Result, Client->OutLength - (size_t)Result);
        Client->OutLength -= (size_t)Result;
    }
    return true;
}

static
void
Accept(int Listener)
{
    WL_CLIENT *Client;
    WL_RESOURCE *Display;
    LX_UCRED Credentials;
    uint32_t Length = sizeof(Credentials);
    long Socket;

    Socket = LxSyscall(LX_NR_ACCEPT4, Listener, 0, 0, LX_SOCK_NONBLOCK | LX_SOCK_CLOEXEC, 0, 0);
    if (Socket < 0)
        return;
    Client = LxAllocZero(sizeof(*Client));
    if (Client == NULL)
    {
        CloseFd((int)Socket);
        return;
    }
    Client->Socket = (int)Socket;
    Client->Refs = 1;
    Client->NextServerId = LX_SERVER_ID_BASE;
    Client->Watch.Type = WATCH_CLIENT;
    Client->Watch.Object = Client;
    Display = ClientAdd(Client, 1, &wl_display_interface, 1);
    if (Display == NULL)
    {
        CloseFd(Client->Socket);
        LxFree(Client->Objects);
        LxFree(Client);
        return;
    }
    Client->Display = Display;
    memset(&Credentials, 0, sizeof(Credentials));
    if (LxSyscall(LX_NR_GETSOCKOPT, Client->Socket, LX_SOL_SOCKET, LX_SO_PEERCRED, (long)&Credentials, (long)&Length, 0) == 0)
        Client->Pid = Credentials.Pid;
    LxLog("client %d connected", Client->Pid);
    if (!WatchAdd(Client->Socket, LX_EPOLLIN, &Client->Watch))
    {
        Client->Next = NULL;
        ClientDrop(Client);
        return;
    }
    Client->Next = Clients;
    Clients = Client;
}

static
void
HostDetach(void)
{
    while (Windows != NULL)
    {
        WINDOW *Window = Windows;

        Window->Xdg->Window = NULL;
        FreeWindow(Window);
    }
    PopupCount = 0;
    FocusedWindow = NULL;
}

static
void
HostHello(const uint8_t *Payload, uint32_t Length)
{
    WL_RESOURCE *Found[256];
    WL_CLIENT *Client;
    uint32_t Count, Index;

    if (Length < sizeof(LX_WIN_HELLO_MSG) - sizeof(LX_WIN_HEADER))
        return;
    memcpy(&ScreenWidth, Payload, 4);
    memcpy(&ScreenHeight, Payload + 4, 4);
    Host = true;
    LxLog("window host ready, screen %ux%u", ScreenWidth, ScreenHeight);
    HostSend(LX_WIN_READY, 0, NULL, 0);
    for (Client = Clients; Client != NULL; Client = Client->Next)
    {
        Count = ClientResources(Client, &wl_output_interface, Found, 256);
        for (Index = 0; Index < Count; Index++)
            SendOutput(Found[Index]);
    }
    for (Client = Clients; Client != NULL; Client = Client->Next)
    {
        Count = ClientResources(Client, &xdg_surface_interface, Found, 256);
        for (Index = 0; Index < Count; Index++)
        {
            XDG_SURFACE *Xdg = Found[Index]->Data;

            if (Xdg->Configured && Xdg->Surface != NULL && Xdg->Surface->Buffer != NULL && Xdg->Window == NULL)
            {
                Map(Xdg);
                if (Xdg->Window != NULL)
                    DrawWindow(Xdg->Window, NULL, 0);
            }
        }
    }
}

static
void
HostEvent(const uint8_t *Data, size_t Length)
{
    LX_WIN_HEADER Header;
    const uint8_t *Payload;
    uint32_t PayloadLength;
    WINDOW *Window;
    TOPLEVEL *Toplevel;
    uint32_t Values[4];

    if (Length < sizeof(Header))
        return;
    memcpy(&Header, Data, sizeof(Header));
    if (Header.Size < sizeof(Header) || Header.Size > Length)
        return;
    Payload = Data + sizeof(Header);
    PayloadLength = Header.Size - sizeof(Header);
    switch (Header.Type)
    {
        case LX_WIN_CLIPBOARD_SET:
        {
            HOST_SOURCE *Source = LxAllocZero(sizeof(*Source));

            if (Source == NULL)
                return;
            Source->Refs = 1;
            Source->Length = PayloadLength;
            if (PayloadLength != 0)
            {
                Source->Text = LxAlloc(PayloadLength);
                if (Source->Text == NULL)
                {
                    LxFree(Source);
                    return;
                }
                memcpy(Source->Text, Payload, PayloadLength);
            }
            SetSelection(NULL, Source);
            return;
        }
        case LX_WIN_HOST_ATTACHED:
            LxLog("window host attached");
            Host = true;
            return;
        case LX_WIN_HOST_DETACHED:
            LxLog("window host detached");
            Host = false;
            HostDetach();
            return;
        case LX_WIN_HELLO:
            HostHello(Payload, PayloadLength);
            return;
    }
    for (Window = Windows; Window != NULL; Window = Window->Next)
    {
        if (Window->Id == Header.Window)
            break;
    }
    if (Window == NULL)
        return;
    Toplevel = XdgToplevel(Window->Xdg);
    if (PayloadLength > sizeof(Values))
        PayloadLength = sizeof(Values);
    memset(Values, 0, sizeof(Values));
    memcpy(Values, Payload, PayloadLength);
    switch (Header.Type)
    {
        case LX_WIN_CONFIGURE:
            if (Toplevel != NULL && PayloadLength >= 12)
            {
                uint32_t States = 0;
                RECT32 Rect = WindowRect(Window->Xdg);

                if (Values[2] & LX_WIN_CONFIGURE_MAXIMIZED)
                    States |= 1u << XDG_STATE_MAXIMIZED;
                if (Values[2] & LX_WIN_CONFIGURE_FULLSCREEN)
                    States |= 1u << XDG_STATE_FULLSCREEN;
                if ((int32_t)Values[0] != Rect.Width || (int32_t)Values[1] != Rect.Height || States != Toplevel->States)
                {
                    Toplevel->Width = (int32_t)Values[0];
                    Toplevel->Height = (int32_t)Values[1];
                    Toplevel->States = States;
                    SendToplevelConfigure(Toplevel);
                }
            }
            break;
        case LX_WIN_CLOSE:
            if (Toplevel != NULL)
                WlSend(Toplevel->Resource, XDG_TOPLEVEL_EVENT_CLOSE);
            break;
        case LX_WIN_FOCUS:
            if (PayloadLength < 4)
                break;
            if (Values[0] != 0)
            {
                FocusedWindow = Window;
            }
            else if (FocusedWindow == Window)
            {
                FocusedWindow = NULL;
                DismissPopups();
            }
            if (Toplevel != NULL)
                SendToplevelConfigure(Toplevel);
            UpdateKeyboardFocus();
            break;
        case LX_WIN_POINTER:
            if (PayloadLength >= 8)
                PointerMotion(Window, (int32_t)Values[0], (int32_t)Values[1]);
            break;
        case LX_WIN_LEAVE:
            if (PointerFocus != NULL && PointerFocus == Window->Xdg->Surface)
                PointerLeave();
            break;
        case LX_WIN_BUTTON:
            if (PayloadLength >= 8)
                PointerButton(Window, Values[0], Values[1] != 0);
            break;
        case LX_WIN_AXIS:
            if (PayloadLength >= 8)
                PointerAxis(Values[0], (int32_t)Values[1]);
            break;
        case LX_WIN_KEY:
            if (PayloadLength >= 8)
            {
                UpdateKeyboardFocus();
                Key(Values[0], Values[1] != 0);
            }
            break;
    }
}

static
void
DeviceRead(void)
{
    for (;;)
    {
        long Result = LxSyscall(LX_NR_READ, Device, (long)DeviceBuffer, LX_DEVICE_READ, 0, 0, 0);

        if (Result == -LX_EINTR)
            continue;
        if (Result <= 0)
            break;
        HostEvent(DeviceBuffer, (size_t)Result);
    }
}

static
void
RunFrames(void)
{
    WL_RESOURCE **Pending = Frames;
    uint32_t Count = FrameCount, Index;
    uint32_t Stamp = NowMs();

    Frames = NULL;
    FrameCount = 0;
    FrameCapacity = 0;
    for (Index = 0; Index < Count; Index++)
    {
        WL_RESOURCE *Callback = Pending[Index];

        if (Callback->Alive && !Callback->Client->Closed)
        {
            WlSend(Callback, WL_CALLBACK_EVENT_DONE, Stamp);
            ClientRemove(Callback);
        }
        Unref(Callback);
    }
    LxFree(Pending);
}

static
void
FlushClients(void)
{
    WL_CLIENT *Client = Clients;

    while (Client != NULL)
    {
        WL_CLIENT *Next = Client->Next;
        bool Done = ClientFlush(Client);

        if (Client->Failed)
        {
            ClientDrop(Client);
        }
        else if (Done == Client->Writing)
        {
            Client->Writing = !Done;
            WatchModify(Client->Socket, Done ? LX_EPOLLIN : LX_EPOLLIN | LX_EPOLLOUT, &Client->Watch);
        }
        Client = Next;
    }
}

static
void
MakeDirectory(const char *Path, uint32_t Mode)
{
    LxSyscall(LX_NR_MKDIRAT, LX_AT_FDCWD, (long)Path, Mode, 0, 0, 0);
}

int
LxMain(void)
{
    LX_SOCKADDR_UN Address;
    LX_EPOLL_EVENT Events[64];
    uint64_t NextFrame = 0;
    long Listener, Result;
    int Index;

    LxIgnoreSigpipe();
    Result = LxSyscall(LX_NR_OPENAT, LX_AT_FDCWD, (long)LX_DEVICE, LX_O_RDWR | LX_O_NONBLOCK | LX_O_CLOEXEC, 0, 0, 0);
    if (Result < 0)
    {
        LxLog("cannot open %s (error %d); another compositor owns this root", LX_DEVICE, (int)-Result);
        return 0;
    }
    Device = (int)Result;
    DeviceBuffer = LxAlloc(LX_DEVICE_READ);
    Epoll = (int)LxSyscall(LX_NR_EPOLL_CREATE1, LX_O_CLOEXEC, 0, 0, 0, 0, 0);
    if (DeviceBuffer == NULL || Epoll < 0)
    {
        LxLog("cannot create the event loop");
        return 1;
    }
    MakeDirectory(LX_SOCKET_DIR, 0755);
    MakeDirectory("/run/user", 0755);
    MakeDirectory("/run/user/0", 0700);
    LxSyscall(LX_NR_UNLINKAT, LX_AT_FDCWD, (long)LX_SOCKET_PATH, 0, 0, 0, 0);
    Listener = LxSyscall(LX_NR_SOCKET, LX_AF_UNIX, LX_SOCK_STREAM | LX_SOCK_NONBLOCK | LX_SOCK_CLOEXEC, 0, 0, 0, 0);
    memset(&Address, 0, sizeof(Address));
    Address.Family = LX_AF_UNIX;
    memcpy(Address.Path, LX_SOCKET_PATH, sizeof(LX_SOCKET_PATH));
    if (Listener < 0 ||
        LxSyscall(LX_NR_BIND, Listener, (long)&Address, sizeof(Address.Family) + sizeof(LX_SOCKET_PATH), 0, 0, 0) != 0 ||
        LxSyscall(LX_NR_FCHMODAT, LX_AT_FDCWD, (long)LX_SOCKET_PATH, 0777, 0, 0, 0) != 0 ||
        LxSyscall(LX_NR_LISTEN, Listener, 64, 0, 0, 0, 0) != 0)
    {
        LxLog("cannot listen on %s", LX_SOCKET_PATH);
        return 1;
    }
    if (!WatchAdd((int)Listener, LX_EPOLLIN, &ListenerWatch) || !WatchAdd(Device, LX_EPOLLIN, &DeviceWatch))
    {
        LxLog("cannot watch the listener");
        return 1;
    }
    LxLog("listening on %s", LX_SOCKET_PATH);

    for (;;)
    {
        long Timeout = -1;
        long Count;

        if (FrameCount != 0)
        {
            uint64_t Now = LxMonotonicMs();

            Timeout = NextFrame > Now ? (long)(NextFrame - Now) : 0;
        }
        Count = LxSyscall(LX_NR_EPOLL_PWAIT, Epoll, (long)Events, 64, Timeout, 0, 8);
        for (Index = 0; Index < Count; Index++)
        {
            WATCH *Watch = (WATCH *)(uintptr_t)Events[Index].Data;

            switch (Watch->Type)
            {
                case WATCH_LISTENER:
                    Accept((int)Listener);
                    break;
                case WATCH_DEVICE:
                    DeviceRead();
                    break;
                case WATCH_CLIENT:
                {
                    WL_CLIENT *Client = Watch->Object;

                    if (Client->Closed)
                        break;
                    if (Events[Index].Events & (LX_EPOLLIN | LX_EPOLLERR | LX_EPOLLHUP))
                        ClientReceive(Client);
                    if (Client->Failed)
                    {
                        ClientFlush(Client);
                        ClientDrop(Client);
                    }
                    break;
                }
                case WATCH_CLIPBOARD_READ:
                    ClipboardRead(Watch->Object);
                    break;
                case WATCH_CLIPBOARD_WRITE:
                    if (ClipboardWriteStep(Watch->Object))
                        ClipboardWriteFree(Watch->Object, true);
                    break;
            }
        }
        if (FrameCount != 0 && LxMonotonicMs() >= NextFrame)
        {
            RunFrames();
            NextFrame = LxMonotonicMs() + LX_FRAME_MS;
        }
        FlushClients();
    }
}
