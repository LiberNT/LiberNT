/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Key management: add_key, request_key, keyctl, /proc/keys and /proc/key-users
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"
#include <ntstrsafe.h>

#define NDEBUG
#include <debug.h>

#define LX_KEY_POS_VIEW         0x01000000
#define LX_KEY_POS_READ         0x02000000
#define LX_KEY_POS_WRITE        0x04000000
#define LX_KEY_POS_SEARCH       0x08000000
#define LX_KEY_POS_LINK         0x10000000
#define LX_KEY_POS_SETATTR      0x20000000
#define LX_KEY_POS_ALL          0x3F000000
#define LX_KEY_USR_VIEW         0x00010000
#define LX_KEY_USR_READ         0x00020000
#define LX_KEY_USR_LINK         0x00100000
#define LX_KEY_USR_ALL          0x003F0000
#define LX_KEY_GRP_ALL          0x00003F00
#define LX_KEY_OTH_ALL          0x0000003F
#define LX_KEY_PERM_MASK        (LX_KEY_POS_ALL | LX_KEY_USR_ALL | LX_KEY_GRP_ALL | LX_KEY_OTH_ALL)

#define LX_KEY_F_REVOKED        0x01
#define LX_KEY_F_INVALIDATED    0x02
#define LX_KEY_F_IN_QUOTA       0x04
#define LX_KEY_F_CONSTRUCT      0x08
#define LX_KEY_F_UID_KEYRING    0x10

#define LX_KEY_A_IN_QUOTA       0x00
#define LX_KEY_A_QUOTA_OVERRUN  0x01
#define LX_KEY_A_NOT_IN_QUOTA   0x02
#define LX_KEY_A_UID_KEYRING    0x04

#define LX_KEY_UNINSTANTIATED   0
#define LX_KEY_POSITIVE         1

#define LX_KEY_NEED_VIEW        1
#define LX_KEY_NEED_READ        2
#define LX_KEY_NEED_WRITE       3
#define LX_KEY_NEED_SEARCH      4
#define LX_KEY_NEED_LINK        5
#define LX_KEY_NEED_SETATTR     6
#define LX_KEY_NEED_UNLINK      7
#define LX_KEY_AUTH_OVERRIDE    8
#define LX_KEY_DEFER_PERM_CHECK 9

#define LX_KL_CREATE            0x01
#define LX_KL_PARTIAL           0x02

#define LX_KS_STATE_CHECK       0x01
#define LX_KS_SKIP_EXPIRED      0x02
#define LX_KS_NO_CHECK_PERM     0x04
#define LX_KS_DETECT_TOO_DEEP   0x08
#define LX_KS_RECURSE           0x10

#define LX_KEY_MAX_DESC         4096
#define LX_KEY_TYPE_LENGTH      32
#define LX_KEY_LINK_BYTES       4
#define LX_KEY_SEARCH_DEPTH     6
#define LX_KEY_NEGATIVE_TIMEOUT 60
#define LX_KEY_PERMANENT        MAXLONG64
#define LX_KEY_PAGE             4096
#define LX_KEY_MAX_PAYLOAD      (1024 * 1024 - 1)
#define LX_KEY_UIO_MAXIOV       1024
#define LX_KEY_INVALID_ID       MAXULONG
/* FIXME: big_key keeps payloads above sizeof(struct inode) + sizeof(struct dentry) in a file; 800 is the x86-64 Linux 7.0 value measured on the host, not an arm64 or riscv64 measurement. */
#define LX_KEY_BIG_THRESHOLD    800

typedef enum _LX_KEY_TYPE_ID
{
    LxKeyTypeKeyring,
    LxKeyTypeUser,
    LxKeyTypeLogon,
    LxKeyTypeBig,
    LxKeyTypeAuth,
    LxKeyTypeCount
} LX_KEY_TYPE_ID;

typedef struct _LX_KEY_TYPE_INFO
{
    PCSTR Name;
    ULONG MaxData;
    BOOLEAN Read;
    BOOLEAN Update;
} LX_KEY_TYPE_INFO;

/* TODO: the asymmetric, encrypted, trusted and dns_resolver key types are not implemented; add_key returns ENODEV for them. */
static const LX_KEY_TYPE_INFO LxKeyTypes[LxKeyTypeCount] =
{
    { "keyring", 0, TRUE, FALSE },
    { "user", 32767, TRUE, TRUE },
    { "logon", 32767, FALSE, TRUE },
    { "big_key", 1024 * 1024, TRUE, TRUE },
    { ".request_key_auth", 0, TRUE, FALSE },
};

typedef struct _LX_KEY_USER
{
    LIST_ENTRY Link;
    PLX_INSTANCE Instance;
    ULONG Uid;
    LONG NKeys;
    LONG NIKeys;
    ULONG QnKeys;
    ULONG QnBytes;
} LX_KEY_USER, *PLX_KEY_USER;

typedef struct _LX_KEY_CRED
{
    PLX_THREAD Thread;
    PLX_INSTANCE Instance;
    PLX_NS UserNs;
    ULONG Uid;
    ULONG Euid;
    ULONG Suid;
    ULONG Gid;
    ULONG Egid;
    ULONG Sgid;
    ULONG Fsuid;
    ULONG Fsgid;
    ULONG GroupCount;
    ULONG Groups[32];
    PLX_KEY ThreadKeyring;
    PLX_KEY ProcessKeyring;
    PLX_KEY SessionKeyring;
    PLX_KEY Auth;
    ULONG Pid;
} LX_KEY_CRED, *PLX_KEY_CRED;

typedef struct _LX_KEY_AUTH
{
    PLX_KEY Target;
    PLX_KEY Dest;
    PUCHAR Callout;
    ULONG CalloutLength;
    LX_KEY_CRED Cred;
} LX_KEY_AUTH, *PLX_KEY_AUTH;

struct _LX_KEY
{
    RTL_BALANCED_NODE TreeNode;
    LIST_ENTRY Link;
    LIST_ENTRY NameLink;
    LIST_ENTRY DeadLink;
    LONG Usage;
    LONG Serial;
    LX_KEY_TYPE_ID Type;
    ULONG Flags;
    LONG State;
    PLX_INSTANCE Instance;
    ULONG64 UserNsInum;
    ULONG Uid;
    ULONG Gid;
    ULONG Perm;
    LONG64 Expiry;
    PSTR Description;
    ULONG DescriptionLength;
    PUCHAR Data;
    ULONG DataLength;
    ULONG Reserved;
    ULONG QuotaLength;
    PLX_KEY_USER User;
    PLX_KEY *Links;
    ULONG LinkCount;
    ULONG LinkCapacity;
    BOOLEAN Restricted;
    PLX_KEY_AUTH Auth;
    KEVENT Constructed;
};

typedef struct _LX_KEY_SEARCH
{
    PLX_KEY_CRED Cred;
    LX_KEY_TYPE_ID Type;
    PCSTR Description;
    PLX_KEY Target;
    BOOLEAN Cycle;
    BOOLEAN Possessed;
    BOOLEAN Abort;
    ULONG Flags;
    LONG SkippedRet;
    LONG64 Now;
    PLX_KEY Result;
    LONG Error;
} LX_KEY_SEARCH, *PLX_KEY_SEARCH;

typedef struct _LX_KEY_TEXT
{
    PSTR Buffer;
    ULONG Length;
    ULONG Capacity;
    BOOLEAN Failed;
} LX_KEY_TEXT, *PLX_KEY_TEXT;

static FAST_MUTEX LxKeyLock;
static RTL_RB_TREE LxKeyTree;
static LIST_ENTRY LxKeyList;
static LIST_ENTRY LxKeyNames;
static LIST_ENTRY LxKeyDead;
static LIST_ENTRY LxKeyUsers;
static LONG64 LxKeyNextGc = LX_KEY_PERMANENT;
static LONG LxKeyMaxKeys = 200;
static LONG LxKeyMaxBytes = 20000;
static LONG LxKeyRootMaxKeys = 1000000;
static LONG LxKeyRootMaxBytes = 25000000;
static LONG LxKeyGcDelay = 300;
static LONG LxKeyPersistentExpiry = 259200;

static
LONG64
LxKeyNow(VOID)
{
    LARGE_INTEGER Time;

    KeQuerySystemTime(&Time);
    return (Time.QuadPart - 116444736000000000LL) / 10000000LL;
}

static
VOID
LxKeyAcquire(VOID)
{
    ExAcquireFastMutex(&LxKeyLock);
}

static
VOID
LxKeyDestroy(
    _In_ PLX_KEY Key);

static
VOID
LxKeyRelease(VOID)
{
    while (!IsListEmpty(&LxKeyDead))
    {
        PLX_KEY Key = CONTAINING_RECORD(RemoveHeadList(&LxKeyDead), LX_KEY, DeadLink);

        LxKeyDestroy(Key);
    }
    ExReleaseFastMutex(&LxKeyLock);
}

static
VOID
LxKeyGet(
    _In_ PLX_KEY Key)
{
    Key->Usage++;
}

static
VOID
LxKeyPut(
    _In_opt_ PLX_KEY Key)
{
    if ((Key != NULL) && (--Key->Usage == 0))
        InsertTailList(&LxKeyDead, &Key->DeadLink);
}

static
VOID
LxKeySwap(
    _Inout_ PLX_KEY *Slot,
    _In_opt_ PLX_KEY Key)
{
    if (Key != NULL)
        LxKeyGet(Key);
    LxKeyPut(*Slot);
    *Slot = Key;
}

static
BOOLEAN
LxKeyIsRoot(
    _In_ ULONG Uid)
{
    return Uid == 0;
}

static
ULONG
LxKeyQuotaKeys(
    _In_ ULONG Uid)
{
    return (ULONG)(LxKeyIsRoot(Uid) ? LxKeyRootMaxKeys : LxKeyMaxKeys);
}

static
ULONG
LxKeyQuotaBytes(
    _In_ ULONG Uid)
{
    return (ULONG)(LxKeyIsRoot(Uid) ? LxKeyRootMaxBytes : LxKeyMaxBytes);
}

static
PLX_KEY_USER
LxKeyUserLookup(
    _In_ PLX_INSTANCE Instance,
    _In_ ULONG Uid)
{
    PLIST_ENTRY Entry;
    PLX_KEY_USER User;

    for (Entry = LxKeyUsers.Flink; Entry != &LxKeyUsers; Entry = Entry->Flink)
    {
        User = CONTAINING_RECORD(Entry, LX_KEY_USER, Link);
        if ((User->Instance == Instance) && (User->Uid == Uid))
            return User;
        if ((User->Instance == Instance) && (User->Uid > Uid))
            break;
    }
    User = LxAlloc(sizeof(*User));
    if (User == NULL)
        return NULL;
    RtlZeroMemory(User, sizeof(*User));
    User->Instance = Instance;
    User->Uid = Uid;
    InsertTailList(Entry, &User->Link);
    return User;
}

static
VOID
LxKeyUserPut(
    _In_ PLX_KEY_USER User)
{
    if (User->NKeys == 0)
    {
        RemoveEntryList(&User->Link);
        LxFree(User);
    }
}

static
VOID
LxKeySetExpiry(
    _Inout_ PLX_KEY Key,
    _In_ LONG64 Expiry)
{
    Key->Expiry = Expiry;
    if ((Expiry != LX_KEY_PERMANENT) && (Expiry + LxKeyGcDelay < LxKeyNextGc))
        LxKeyNextGc = Expiry + LxKeyGcDelay;
}

static
LONG
LxKeyValidate(
    _In_ PLX_KEY Key)
{
    if (Key->Flags & LX_KEY_F_INVALIDATED)
        return -LX_ENOKEY;
    if (Key->Flags & LX_KEY_F_REVOKED)
        return -LX_EKEYREVOKED;
    if ((Key->Expiry != LX_KEY_PERMANENT) && (LxKeyNow() >= Key->Expiry))
        return -LX_EKEYEXPIRED;
    return 0;
}

static
BOOLEAN
LxKeyInGroup(
    _In_ PLX_KEY_CRED Cred,
    _In_ ULONG Gid)
{
    ULONG Index;

    if (Gid == Cred->Fsgid)
        return TRUE;
    for (Index = 0; Index < Cred->GroupCount; Index++)
    {
        if (Cred->Groups[Index] == Gid)
            return TRUE;
    }
    return FALSE;
}

static
LONG
LxKeyPermission(
    _In_ PLX_KEY Key,
    _In_ BOOLEAN Possessed,
    _In_ PLX_KEY_CRED Cred,
    _In_ ULONG Need)
{
    ULONG Mask, Perm;

    if ((Need == 0) || (Need > LX_KEY_NEED_SETATTR))
        return 0;
    Mask = 1UL << (Need - 1);
    if (Key->Uid == Cred->Fsuid)
        Perm = Key->Perm >> 16;
    else if ((Key->Gid != LX_KEY_INVALID_ID) && (Key->Perm & LX_KEY_GRP_ALL) && LxKeyInGroup(Cred, Key->Gid))
        Perm = Key->Perm >> 8;
    else
        Perm = Key->Perm;
    if (Possessed)
        Perm |= Key->Perm >> 24;
    return ((Perm & Mask) == Mask) ? 0 : -LX_EACCES;
}

static
PLX_KEY
LxKeyFindSerial(
    _In_ LONG Serial)
{
    PRTL_BALANCED_NODE Node = LxKeyTree.Root;

    while (Node != NULL)
    {
        PLX_KEY Key = CONTAINING_RECORD(Node, LX_KEY, TreeNode);

        if (Key->Serial == Serial)
            return Key;
        Node = Node->Children[Serial > Key->Serial];
    }
    return NULL;
}

static
VOID
LxKeyInsertSerial(
    _Inout_ PLX_KEY Key)
{
    PRTL_BALANCED_NODE Node = LxKeyTree.Root, Parent = NULL;
    BOOLEAN Right = FALSE;
    ULONG Serial;

    do
    {
        LxFillRandom((PUCHAR)&Serial, sizeof(Serial));
        Serial >>= 1;
    } while (Serial < 3);
    while (LxKeyFindSerial((LONG)Serial) != NULL)
    {
        Serial++;
        if (Serial > MAXLONG)
            Serial = 3;
    }
    Key->Serial = (LONG)Serial;
    while (Node != NULL)
    {
        Parent = Node;
        Right = (Key->Serial > CONTAINING_RECORD(Node, LX_KEY, TreeNode)->Serial);
        Node = Node->Children[Right];
    }
    RtlRbInsertNodeEx(&LxKeyTree, Parent, Right, &Key->TreeNode);
}

static
PLX_KEY
LxKeyNextSerial(
    _In_ LONG Serial)
{
    PRTL_BALANCED_NODE Node = LxKeyTree.Root;
    PLX_KEY Best = NULL;

    while (Node != NULL)
    {
        PLX_KEY Key = CONTAINING_RECORD(Node, LX_KEY, TreeNode);

        if (Key->Serial > Serial)
        {
            Best = Key;
            Node = Node->Left;
        }
        else
        {
            Node = Node->Right;
        }
    }
    return Best;
}

static
LONG
LxKeyReserve(
    _Inout_ PLX_KEY Key,
    _In_ ULONG Length)
{
    LONG64 Delta = (LONG64)Length - (LONG64)Key->Reserved;

    if ((Delta != 0) && (Key->Flags & LX_KEY_F_IN_QUOTA))
    {
        if ((Delta > 0) && ((ULONG64)Key->User->QnBytes + (ULONG64)Delta > LxKeyQuotaBytes(Key->User->Uid)))
            return -LX_EDQUOT;
        Key->User->QnBytes = (ULONG)((LONG64)Key->User->QnBytes + Delta);
        Key->QuotaLength = (ULONG)((LONG64)Key->QuotaLength + Delta);
    }
    Key->Reserved = Length;
    return 0;
}

static
VOID
LxKeyFreeData(
    _Inout_ PLX_KEY Key)
{
    if (Key->Data != NULL)
    {
        RtlSecureZeroMemory(Key->Data, Key->DataLength);
        LxFreePaged(Key->Data);
    }
    Key->Data = NULL;
}

static
VOID
LxKeyCredReleaseRefs(
    _Inout_ PLX_KEY_CRED Cred)
{
    LxKeySwap(&Cred->ThreadKeyring, NULL);
    LxKeySwap(&Cred->ProcessKeyring, NULL);
    LxKeySwap(&Cred->SessionKeyring, NULL);
    LxKeySwap(&Cred->Auth, NULL);
}

static
VOID
LxKeyAuthFree(
    _Inout_ PLX_KEY Key)
{
    PLX_KEY_AUTH Auth = Key->Auth;

    if (Auth == NULL)
        return;
    Key->Auth = NULL;
    LxKeyPut(Auth->Target);
    LxKeyPut(Auth->Dest);
    LxKeyCredReleaseRefs(&Auth->Cred);
    if (Auth->Cred.UserNs != NULL)
        LxNsDereference(Auth->Cred.UserNs);
    LxFree(Auth->Callout);
    LxFree(Auth);
}

static
VOID
LxKeyringClearLinks(
    _Inout_ PLX_KEY Keyring)
{
    ULONG Index;

    for (Index = 0; Index < Keyring->LinkCount; Index++)
        LxKeyPut(Keyring->Links[Index]);
    Keyring->LinkCount = 0;
}

static
VOID
LxKeyDestroy(
    _In_ PLX_KEY Key)
{
    PLX_KEY_USER User = Key->User;

    LxKeyringClearLinks(Key);
    LxFree(Key->Links);
    LxKeyAuthFree(Key);
    LxKeyFreeData(Key);
    RtlRbRemoveNode(&LxKeyTree, &Key->TreeNode);
    RemoveEntryList(&Key->Link);
    if (!IsListEmpty(&Key->NameLink))
        RemoveEntryList(&Key->NameLink);
    if (Key->Flags & LX_KEY_F_IN_QUOTA)
    {
        User->QnKeys--;
        User->QnBytes -= Key->QuotaLength;
    }
    if (Key->State != LX_KEY_UNINSTANTIATED)
        User->NIKeys--;
    User->NKeys--;
    LxKeyUserPut(User);
    if (Key->Flags & LX_KEY_F_CONSTRUCT)
        KeSetEvent(&Key->Constructed, IO_NO_INCREMENT, FALSE);
    LxFree(Key->Description);
    LxFree(Key);
}

static
LONG
LxKeyAlloc(
    _In_ PLX_KEY_CRED Cred,
    _In_ LX_KEY_TYPE_ID Type,
    _In_ PCSTR Description,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG Perm,
    _In_ ULONG Flags,
    _Out_ PLX_KEY *Result)
{
    SIZE_T Length;
    ULONG Quota;
    PLX_KEY_USER User;
    PLX_KEY Key;
    PCSTR Colon;

    *Result = NULL;
    if ((Description == NULL) || (Description[0] == '\0'))
        return -LX_EINVAL;
    if (Type == LxKeyTypeLogon)
    {
        Colon = strchr(Description, ':');
        if ((Colon == NULL) || (Colon == Description))
            return -LX_EINVAL;
    }
    Length = strlen(Description);
    Quota = (ULONG)Length + 1;
    User = LxKeyUserLookup(Cred->Instance, Uid);
    if (User == NULL)
        return -LX_ENOMEM;
    if (!(Flags & LX_KEY_A_NOT_IN_QUOTA) && !(Flags & LX_KEY_A_QUOTA_OVERRUN) &&
        ((User->QnKeys + 1 > LxKeyQuotaKeys(Uid)) || ((ULONG64)User->QnBytes + Quota > LxKeyQuotaBytes(Uid))))
    {
        LxKeyUserPut(User);
        return -LX_EDQUOT;
    }

    Key = LxAlloc(sizeof(*Key));
    if (Key == NULL)
    {
        LxKeyUserPut(User);
        return -LX_ENOMEM;
    }
    RtlZeroMemory(Key, sizeof(*Key));
    Key->Description = LxStrDup(Description);
    if (Key->Description == NULL)
    {
        LxFree(Key);
        LxKeyUserPut(User);
        return -LX_ENOMEM;
    }
    if (!(Flags & LX_KEY_A_NOT_IN_QUOTA))
    {
        User->QnKeys++;
        User->QnBytes += Quota;
        Key->Flags |= LX_KEY_F_IN_QUOTA;
    }
    if (Flags & LX_KEY_A_UID_KEYRING)
        Key->Flags |= LX_KEY_F_UID_KEYRING;
    User->NKeys++;
    Key->Usage = 1;
    Key->Type = Type;
    Key->Instance = Cred->Instance;
    Key->UserNsInum = Cred->UserNs->Inum;
    Key->Uid = Uid;
    Key->Gid = Gid;
    Key->Perm = Perm;
    Key->Expiry = LX_KEY_PERMANENT;
    Key->DescriptionLength = (ULONG)Length;
    Key->QuotaLength = Quota;
    Key->User = User;
    Key->State = LX_KEY_UNINSTANTIATED;
    KeInitializeEvent(&Key->Constructed, NotificationEvent, TRUE);
    InitializeListHead(&Key->NameLink);
    InsertTailList(&LxKeyList, &Key->Link);
    LxKeyInsertSerial(Key);
    *Result = Key;
    return 0;
}

static
VOID
LxKeyMarkInstantiated(
    _Inout_ PLX_KEY Key,
    _In_ LONG State)
{
    Key->State = State;
    Key->User->NIKeys++;
    if (Key->Flags & LX_KEY_F_CONSTRUCT)
    {
        Key->Flags &= ~LX_KEY_F_CONSTRUCT;
        KeSetEvent(&Key->Constructed, IO_NO_INCREMENT, FALSE);
    }
}

static
BOOLEAN
LxKeyIndexEquals(
    _In_ PLX_KEY Key,
    _In_ LX_KEY_TYPE_ID Type,
    _In_ PCSTR Description)
{
    return (Key->Type == Type) && (strcmp(Key->Description, Description) == 0);
}

static
LONG
LxKeyringIndexOf(
    _In_ PLX_KEY Keyring,
    _In_ LX_KEY_TYPE_ID Type,
    _In_ PCSTR Description)
{
    ULONG Index;

    for (Index = 0; Index < Keyring->LinkCount; Index++)
    {
        if (LxKeyIndexEquals(Keyring->Links[Index], Type, Description))
            return (LONG)Index;
    }
    return -1;
}

static
PLX_KEY
LxKeyringFind(
    _In_ PLX_KEY Keyring,
    _In_ LX_KEY_TYPE_ID Type,
    _In_ PCSTR Description)
{
    LONG Index = LxKeyringIndexOf(Keyring, Type, Description);

    return (Index >= 0) ? Keyring->Links[Index] : NULL;
}

static
VOID
LxKeyringRemoveAt(
    _Inout_ PLX_KEY Keyring,
    _In_ ULONG Index)
{
    PLX_KEY Key = Keyring->Links[Index];

    RtlMoveMemory(&Keyring->Links[Index], &Keyring->Links[Index + 1],
                  (Keyring->LinkCount - Index - 1) * sizeof(PLX_KEY));
    Keyring->LinkCount--;
    LxKeyReserve(Keyring, Keyring->Reserved - LX_KEY_LINK_BYTES);
    LxKeyPut(Key);
}

static
LONG
LxKeySearchIterate(
    _Inout_ PLX_KEY_SEARCH Search,
    _In_ PLX_KEY Key)
{
    if (Search->Cycle)
    {
        if (Key != Search->Target)
            return 0;
        Search->Error = -LX_EDEADLK;
        return 1;
    }
    if (Key->Type != Search->Type)
        return 0;
    if (Search->Flags & LX_KS_STATE_CHECK)
    {
        if (Key->Flags & (LX_KEY_F_INVALIDATED | LX_KEY_F_REVOKED))
        {
            Search->Error = -LX_EKEYREVOKED;
            return Search->SkippedRet;
        }
        if ((Key->Expiry != LX_KEY_PERMANENT) && (Search->Now >= Key->Expiry))
        {
            if (!(Search->Flags & LX_KS_SKIP_EXPIRED))
                Search->Error = -LX_EKEYEXPIRED;
            return Search->SkippedRet;
        }
    }
    if ((Search->Target != NULL) ? (Key != Search->Target) : (strcmp(Key->Description, Search->Description) != 0))
        return 0;
    if (!(Search->Flags & LX_KS_NO_CHECK_PERM) &&
        (LxKeyPermission(Key, Search->Possessed, Search->Cred, LX_KEY_NEED_SEARCH) != 0))
    {
        Search->Error = -LX_EACCES;
        return Search->SkippedRet;
    }
    if ((Search->Flags & LX_KS_STATE_CHECK) && (Key->State < 0))
    {
        Search->Error = Key->State;
        return Search->SkippedRet;
    }
    Search->Result = Key;
    return 1;
}

static
BOOLEAN
LxKeySearchNested(
    _In_ PLX_KEY Keyring,
    _Inout_ PLX_KEY_SEARCH Search,
    _In_ ULONG Depth)
{
    PLX_KEY Object;
    ULONG Index;

    if ((Depth == 0) && LxKeyIndexEquals(Keyring, Search->Type, Search->Description))
    {
        Search->SkippedRet = 2;
        switch (LxKeySearchIterate(Search, Keyring))
        {
            case 1:
                return TRUE;
            case 2:
                return FALSE;
            default:
                break;
        }
    }
    Search->SkippedRet = 0;
    if (Keyring->Flags & (LX_KEY_F_INVALIDATED | LX_KEY_F_REVOKED))
        return FALSE;

    Object = LxKeyringFind(Keyring, Search->Type, Search->Description);
    if ((Object != NULL) && (LxKeySearchIterate(Search, Object) == 1))
        return TRUE;
    if (!(Search->Flags & LX_KS_RECURSE))
        return FALSE;

    for (Index = 0; Index < Keyring->LinkCount; Index++)
    {
        PLX_KEY Child = Keyring->Links[Index];

        if (Child->Type != LxKeyTypeKeyring)
            continue;
        if (Depth >= LX_KEY_SEARCH_DEPTH)
        {
            if (Search->Flags & LX_KS_DETECT_TOO_DEEP)
            {
                Search->Error = -LX_ELOOP;
                Search->Abort = TRUE;
            }
            return FALSE;
        }
        if (!(Search->Flags & LX_KS_NO_CHECK_PERM) &&
            (LxKeyPermission(Child, Search->Possessed, Search->Cred, LX_KEY_NEED_SEARCH) != 0))
        {
            continue;
        }
        if (LxKeySearchNested(Child, Search, Depth + 1))
            return TRUE;
        if (Search->Abort)
            return FALSE;
    }
    return FALSE;
}

static
LONG
LxKeyringSearchIn(
    _In_ PLX_KEY Keyring,
    _In_ BOOLEAN Possessed,
    _Inout_ PLX_KEY_SEARCH Search,
    _Out_ PLX_KEY *Found)
{
    LONG Error;

    *Found = NULL;
    Search->Possessed = Possessed;
    if (Keyring->Type != LxKeyTypeKeyring)
        return -LX_ENOTDIR;
    if (!(Search->Flags & LX_KS_NO_CHECK_PERM))
    {
        Error = LxKeyPermission(Keyring, Possessed, Search->Cred, LX_KEY_NEED_SEARCH);
        if (Error != 0)
            return Error;
    }
    Search->Now = LxKeyNow();
    Search->Result = NULL;
    Search->Error = -LX_EAGAIN;
    Search->Abort = FALSE;
    if (LxKeySearchNested(Keyring, Search, 0) && (Search->Result != NULL))
    {
        *Found = Search->Result;
        return 0;
    }
    return Search->Error;
}

static
PLX_KEY
LxKeyUserRegister(
    _In_ PLX_NS UserNs)
{
    return UserNs->User.KeyRegister;
}

static
PLX_KEY
LxKeyFindUserSession(
    _In_ PLX_KEY_CRED Cred)
{
    PLX_KEY Register = LxKeyUserRegister(Cred->UserNs);
    CHAR Name[24];

    if (Register == NULL)
        return NULL;
    RtlStringCbPrintfA(Name, sizeof(Name), "_uid_ses.%lu", LxIdFromKernel(Cred->UserNs, FALSE, Cred->Uid));
    return LxKeyringFind(Register, LxKeyTypeKeyring, Name);
}

static
VOID
LxKeyMergeSearchError(
    _In_ LONG Error,
    _Inout_ PLONG Ret,
    _Inout_ PLONG Err)
{
    if (Error == -LX_ENOKEY)
        *Ret = Error;
    else if (Error == -LX_EAGAIN)
        *Ret = (*Ret != 0) ? *Ret : Error;
    else
        *Err = Error;
}

static
LONG
LxKeySearchCredKeyrings(
    _Inout_ PLX_KEY_SEARCH Search,
    _Out_ PLX_KEY *Found)
{
    PLX_KEY_CRED Cred = Search->Cred;
    PLX_KEY Rings[3];
    LONG Ret = 0, Err = -LX_EAGAIN, Error;
    ULONG Index;

    Rings[0] = Cred->ThreadKeyring;
    Rings[1] = Cred->ProcessKeyring;
    Rings[2] = (Cred->SessionKeyring != NULL) ? Cred->SessionKeyring : LxKeyFindUserSession(Cred);
    *Found = NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(Rings); Index++)
    {
        if (Rings[Index] == NULL)
            continue;
        Error = LxKeyringSearchIn(Rings[Index], TRUE, Search, Found);
        if (Error == 0)
            return 0;
        LxKeyMergeSearchError(Error, &Ret, &Err);
    }
    return (Ret != 0) ? Ret : Err;
}

static
LONG
LxKeySearchProcessKeyrings(
    _Inout_ PLX_KEY_SEARCH Search,
    _Out_ PLX_KEY *Found)
{
    PLX_KEY_CRED Cred = Search->Cred;
    LONG Err, Ret = -LX_EACCES, Error;

    Err = LxKeySearchCredKeyrings(Search, Found);
    if (Err == 0)
        return 0;
    if ((Cred->Auth != NULL) && (Cred->Thread != NULL) && (Search->Type != LxKeyTypeAuth) &&
        (LxKeyValidate(Cred->Auth) == 0) && (Cred->Auth->Auth != NULL))
    {
        Search->Cred = &Cred->Auth->Auth->Cred;
        Error = LxKeySearchCredKeyrings(Search, Found);
        Search->Cred = Cred;
        if (Error == 0)
            return 0;
        Ret = Error;
    }
    if ((Err == -LX_ENOKEY) || (Ret == -LX_ENOKEY))
        return -LX_ENOKEY;
    if (Err == -LX_EACCES)
        return Ret;
    return Err;
}

static
BOOLEAN
LxKeyPossessed(
    _In_ PLX_KEY_CRED Cred,
    _In_ PLX_KEY Key,
    _In_ BOOLEAN WithAuth)
{
    LX_KEY_SEARCH Search;
    PLX_KEY Found;

    RtlZeroMemory(&Search, sizeof(Search));
    Search.Cred = Cred;
    Search.Type = Key->Type;
    Search.Description = Key->Description;
    Search.Target = Key;
    Search.Flags = LX_KS_RECURSE;
    if (WithAuth)
        return LxKeySearchProcessKeyrings(&Search, &Found) == 0;
    return LxKeySearchCredKeyrings(&Search, &Found) == 0;
}

static
LONG
LxKeyDetectCycle(
    _In_ PLX_KEY Keyring,
    _In_ PLX_KEY Key)
{
    LX_KEY_SEARCH Search;

    RtlZeroMemory(&Search, sizeof(Search));
    Search.Type = Keyring->Type;
    Search.Description = Keyring->Description;
    Search.Target = Keyring;
    Search.Cycle = TRUE;
    Search.Flags = LX_KS_NO_CHECK_PERM | LX_KS_DETECT_TOO_DEEP | LX_KS_RECURSE;
    Search.Error = -LX_EAGAIN;
    LxKeySearchNested(Key, &Search, 0);
    return (Search.Error == -LX_EAGAIN) ? 0 : Search.Error;
}

static
LONG
LxKeyLinkCheck(
    _In_ PLX_KEY Keyring,
    _In_ LX_KEY_TYPE_ID Type,
    _In_ PCSTR Description,
    _Out_ PBOOLEAN Replace)
{
    *Replace = FALSE;
    if (Keyring->Type != LxKeyTypeKeyring)
        return -LX_ENOTDIR;
    if (Keyring->Flags & LX_KEY_F_REVOKED)
        return -LX_EKEYREVOKED;
    *Replace = (LxKeyringIndexOf(Keyring, Type, Description) >= 0);
    if (!*Replace && (Keyring->Flags & LX_KEY_F_IN_QUOTA) &&
        ((ULONG64)Keyring->User->QnBytes + LX_KEY_LINK_BYTES > LxKeyQuotaBytes(Keyring->User->Uid)))
    {
        return -LX_EDQUOT;
    }
    return 0;
}

static
LONG
LxKeyLinkCommit(
    _Inout_ PLX_KEY Keyring,
    _In_ PLX_KEY Key)
{
    LONG Index = LxKeyringIndexOf(Keyring, Key->Type, Key->Description);

    LxKeyGet(Key);
    if (Index >= 0)
    {
        LxKeyPut(Keyring->Links[Index]);
        Keyring->Links[Index] = Key;
        return 0;
    }
    if (Keyring->LinkCount == Keyring->LinkCapacity)
    {
        ULONG Capacity = Keyring->LinkCapacity ? Keyring->LinkCapacity * 2 : 8;
        PLX_KEY *Links = LxAlloc(Capacity * sizeof(PLX_KEY));

        if (Links == NULL)
        {
            LxKeyPut(Key);
            return -LX_ENOMEM;
        }
        if (Keyring->LinkCount != 0)
            RtlCopyMemory(Links, Keyring->Links, Keyring->LinkCount * sizeof(PLX_KEY));
        LxFree(Keyring->Links);
        Keyring->Links = Links;
        Keyring->LinkCapacity = Capacity;
    }
    Keyring->Links[Keyring->LinkCount++] = Key;
    LxKeyReserve(Keyring, Keyring->Reserved + LX_KEY_LINK_BYTES);
    return 0;
}

static
LONG
LxKeyLink(
    _Inout_ PLX_KEY Keyring,
    _In_ PLX_KEY Key)
{
    BOOLEAN Replace;
    LONG Error = LxKeyLinkCheck(Keyring, Key->Type, Key->Description, &Replace);

    if (Error != 0)
        return Error;
    if (Keyring->Restricted)
        return -LX_EPERM;
    if (Key->Type == LxKeyTypeKeyring)
    {
        Error = LxKeyDetectCycle(Keyring, Key);
        if (Error != 0)
            return Error;
    }
    return LxKeyLinkCommit(Keyring, Key);
}

static
VOID
LxKeyPublishName(
    _Inout_ PLX_KEY Keyring)
{
    if ((Keyring->Type == LxKeyTypeKeyring) && (Keyring->Description[0] != '.'))
        InsertTailList(&LxKeyNames, &Keyring->NameLink);
}

static
LONG
LxKeyringAlloc(
    _In_ PLX_KEY_CRED Cred,
    _In_ PCSTR Description,
    _In_ ULONG Uid,
    _In_ ULONG Gid,
    _In_ ULONG Perm,
    _In_ ULONG Flags,
    _In_opt_ PLX_KEY Dest,
    _Out_ PLX_KEY *Result)
{
    PLX_KEY Keyring;
    BOOLEAN Replace;
    LONG Error;

    *Result = NULL;
    Error = LxKeyAlloc(Cred, LxKeyTypeKeyring, Description, Uid, Gid, Perm, Flags, &Keyring);
    if (Error != 0)
        return Error;
    if (Dest != NULL)
    {
        Error = LxKeyLinkCheck(Dest, LxKeyTypeKeyring, Description, &Replace);
        if ((Error == 0) && Dest->Restricted)
            Error = -LX_EPERM;
        if (Error != 0)
        {
            LxKeyPut(Keyring);
            return Error;
        }
    }
    LxKeyMarkInstantiated(Keyring, LX_KEY_POSITIVE);
    LxKeyPublishName(Keyring);
    if (Dest != NULL)
    {
        Error = LxKeyLinkCommit(Dest, Keyring);
        if (Error != 0)
        {
            LxKeyPut(Keyring);
            return Error;
        }
    }
    *Result = Keyring;
    return 0;
}

static
VOID
LxKeyCredLoad(
    _Out_ PLX_KEY_CRED Cred,
    _In_ PLX_THREAD Thread)
{
    PLX_PROCESS Process = Thread->Process;

    RtlZeroMemory(Cred, sizeof(*Cred));
    Cred->Thread = Thread;
    Cred->Instance = Process->Instance;
    Cred->UserNs = LxCredUserNs(Process);
    Cred->Uid = Thread->Cred->Uid[0];
    Cred->Euid = Thread->Cred->Uid[1];
    Cred->Suid = Thread->Cred->Uid[2];
    Cred->Gid = Thread->Cred->Gid[0];
    Cred->Egid = Thread->Cred->Gid[1];
    Cred->Sgid = Thread->Cred->Gid[2];
    Cred->Fsuid = Thread->Cred->Fsuid;
    Cred->Fsgid = Thread->Cred->Fsgid;
    Cred->GroupCount = min(Thread->Cred->GroupCount, RTL_NUMBER_OF(Cred->Groups));
    RtlCopyMemory(Cred->Groups, Thread->Cred->Groups, Cred->GroupCount * sizeof(ULONG));
    Cred->Pid = Process->Pid;
}

static
VOID
LxKeyCredSync(
    _Inout_ PLX_KEY_CRED Cred)
{
    PLX_THREAD Thread = Cred->Thread;

    Cred->ThreadKeyring = Thread->ThreadKeyring;
    Cred->ProcessKeyring = Thread->ProcessKeyring;
    Cred->SessionKeyring = Thread->SessionKeyring;
    Cred->Auth = Thread->RequestKeyAuth;
}

static
VOID
LxKeyInstall(
    _Inout_ PLX_KEY_CRED Cred,
    _Inout_ PLX_KEY *Slot,
    _In_opt_ PLX_KEY Keyring)
{
    LxKeySwap(Slot, Keyring);
    LxKeyCredSync(Cred);
}

static
LONG
LxKeyInstallThreadKeyring(
    _Inout_ PLX_KEY_CRED Cred)
{
    PLX_KEY Keyring;
    LONG Error;

    if (Cred->Thread->ThreadKeyring != NULL)
        return 0;
    Error = LxKeyringAlloc(Cred, "_tid", Cred->Uid, Cred->Gid, LX_KEY_POS_ALL | LX_KEY_USR_VIEW,
                           LX_KEY_A_QUOTA_OVERRUN, NULL, &Keyring);
    if (Error != 0)
        return Error;
    LxKeyInstall(Cred, &Cred->Thread->ThreadKeyring, Keyring);
    LxKeyPut(Keyring);
    return 0;
}

static
LONG
LxKeyInstallProcessKeyring(
    _Inout_ PLX_KEY_CRED Cred)
{
    PLX_KEY Keyring;
    LONG Error;

    if (Cred->Thread->ProcessKeyring != NULL)
        return 0;
    Error = LxKeyringAlloc(Cred, "_pid", Cred->Uid, Cred->Gid, LX_KEY_POS_ALL | LX_KEY_USR_VIEW,
                           LX_KEY_A_QUOTA_OVERRUN, NULL, &Keyring);
    if (Error != 0)
        return Error;
    LxKeyInstall(Cred, &Cred->Thread->ProcessKeyring, Keyring);
    LxKeyPut(Keyring);
    return 0;
}

static
LONG
LxKeyInstallSessionKeyring(
    _Inout_ PLX_KEY_CRED Cred,
    _In_opt_ PLX_KEY Keyring)
{
    PLX_KEY Anonymous;
    LONG Error;

    if (Keyring != NULL)
    {
        LxKeyInstall(Cred, &Cred->Thread->SessionKeyring, Keyring);
        return 0;
    }
    Error = LxKeyringAlloc(Cred, "_ses", Cred->Uid, Cred->Gid, LX_KEY_POS_ALL | LX_KEY_USR_VIEW | LX_KEY_USR_READ,
                           (Cred->Thread->SessionKeyring != NULL) ? LX_KEY_A_IN_QUOTA : LX_KEY_A_QUOTA_OVERRUN, NULL,
                           &Anonymous);
    if (Error != 0)
        return Error;
    LxKeyInstall(Cred, &Cred->Thread->SessionKeyring, Anonymous);
    LxKeyPut(Anonymous);
    return 0;
}

static
LONG
LxKeyRegisterSearch(
    _In_ PLX_KEY_CRED Cred,
    _In_ PLX_KEY Register,
    _In_ PCSTR Name,
    _Out_ PLX_KEY *Found)
{
    LX_KEY_SEARCH Search;

    RtlZeroMemory(&Search, sizeof(Search));
    Search.Cred = Cred;
    Search.Type = LxKeyTypeKeyring;
    Search.Description = Name;
    Search.Flags = LX_KS_STATE_CHECK;
    return LxKeyringSearchIn(Register, TRUE, &Search, Found);
}

static
LONG
LxKeyLookUpUserKeyrings(
    _Inout_ PLX_KEY_CRED Cred,
    _Out_opt_ PLX_KEY *UserKeyring,
    _Out_opt_ PLX_KEY *UserSession)
{
    PLX_NS UserNs = Cred->UserNs;
    PLX_KEY Register = UserNs->User.KeyRegister, User, Session;
    ULONG Perm = (LX_KEY_POS_ALL & ~LX_KEY_POS_SETATTR) | LX_KEY_USR_ALL;
    ULONG Owner = (UserNs->Parent != NULL) ? UserNs->User.OwnerUid : 0;
    CHAR Name[24];
    LONG Error;

    if (UserKeyring != NULL)
        *UserKeyring = NULL;
    if (UserSession != NULL)
        *UserSession = NULL;
    if (Register == NULL)
    {
        Error = LxKeyringAlloc(Cred, ".user_reg", Owner, LX_KEY_INVALID_ID,
                               LX_KEY_POS_WRITE | LX_KEY_POS_SEARCH | LX_KEY_USR_VIEW | LX_KEY_USR_READ,
                               LX_KEY_A_IN_QUOTA, NULL, &Register);
        if (Error != 0)
            return Error;
        UserNs->User.KeyRegister = Register;
    }

    RtlStringCbPrintfA(Name, sizeof(Name), "_uid.%lu", LxIdFromKernel(UserNs, FALSE, Cred->Uid));
    Error = LxKeyRegisterSearch(Cred, Register, Name, &User);
    if (Error == -LX_EAGAIN)
    {
        Error = LxKeyringAlloc(Cred, Name, Cred->Uid, LX_KEY_INVALID_ID, Perm,
                               LX_KEY_A_UID_KEYRING | LX_KEY_A_IN_QUOTA, Register, &User);
        if (Error != 0)
            return Error;
    }
    else if (Error != 0)
    {
        return Error;
    }
    else
    {
        LxKeyGet(User);
    }

    RtlStringCbPrintfA(Name, sizeof(Name), "_uid_ses.%lu", LxIdFromKernel(UserNs, FALSE, Cred->Uid));
    Error = LxKeyRegisterSearch(Cred, Register, Name, &Session);
    if (Error == -LX_EAGAIN)
    {
        Error = LxKeyringAlloc(Cred, Name, Cred->Uid, LX_KEY_INVALID_ID, Perm,
                               LX_KEY_A_UID_KEYRING | LX_KEY_A_IN_QUOTA, NULL, &Session);
        if (Error == 0)
        {
            Error = LxKeyLink(Session, User);
            if (Error == 0)
                Error = LxKeyLink(Register, Session);
            if (Error != 0)
                LxKeyPut(Session);
        }
    }
    else if (Error == 0)
    {
        LxKeyGet(Session);
    }
    if (Error != 0)
    {
        LxKeyPut(User);
        return Error;
    }

    if (UserKeyring != NULL)
        *UserKeyring = User;
    else
        LxKeyPut(User);
    if (UserSession != NULL)
        *UserSession = Session;
    else
        LxKeyPut(Session);
    return 0;
}

static
PLX_KEY
LxKeyFindKeyringByName(
    _In_ PLX_KEY_CRED Cred,
    _In_ PCSTR Name)
{
    PLIST_ENTRY Entry;

    for (Entry = LxKeyNames.Flink; Entry != &LxKeyNames; Entry = Entry->Flink)
    {
        PLX_KEY Keyring = CONTAINING_RECORD(Entry, LX_KEY, NameLink);

        if ((Keyring->Instance != Cred->Instance) || (Keyring->UserNsInum != Cred->UserNs->Inum) ||
            (LxIdFromKernel(Cred->UserNs, FALSE, Keyring->User->Uid) == MAXULONG) ||
            (Keyring->Flags & (LX_KEY_F_REVOKED | LX_KEY_F_UID_KEYRING)) || (Keyring->Usage == 0) ||
            (strcmp(Keyring->Description, Name) != 0) ||
            (LxKeyPermission(Keyring, FALSE, Cred, LX_KEY_NEED_SEARCH) != 0))
        {
            continue;
        }
        return Keyring;
    }
    return NULL;
}

static
LONG64
LxKeyJoinSession(
    _Inout_ PLX_KEY_CRED Cred,
    _In_opt_ PCSTR Name)
{
    PLX_KEY Keyring;
    LONG Error;

    if (Name == NULL)
    {
        Error = LxKeyInstallSessionKeyring(Cred, NULL);
        return (Error != 0) ? Error : Cred->Thread->SessionKeyring->Serial;
    }
    Keyring = LxKeyFindKeyringByName(Cred, Name);
    if (Keyring == NULL)
    {
        Error = LxKeyringAlloc(Cred, Name, Cred->Uid, Cred->Gid,
                               LX_KEY_POS_ALL | LX_KEY_USR_VIEW | LX_KEY_USR_READ | LX_KEY_USR_LINK,
                               LX_KEY_A_IN_QUOTA, NULL, &Keyring);
        if (Error != 0)
            return Error;
    }
    else if (Keyring == Cred->Thread->SessionKeyring)
    {
        return 0;
    }
    else
    {
        LxKeyGet(Keyring);
    }
    LxKeyInstallSessionKeyring(Cred, Keyring);
    LxKeyPut(Keyring);
    return Keyring->Serial;
}

static
LONG
LxKeyWaitConstruction(
    _In_ PLX_KEY Key)
{
    NTSTATUS Status;

    while (Key->Flags & LX_KEY_F_CONSTRUCT)
    {
        ExReleaseFastMutex(&LxKeyLock);
        Status = LxWaitForEvent(&Key->Constructed, NULL);
        ExAcquireFastMutex(&LxKeyLock);
        if ((Status != STATUS_SUCCESS) && (Key->Flags & LX_KEY_F_CONSTRUCT))
            return -LX_ERESTARTSYS;
    }
    if (Key->State < 0)
        return Key->State;
    return LxKeyValidate(Key);
}

static
LONG
LxKeyLookupSpecial(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ ULONG Flags,
    _Out_ PLX_KEY *Result)
{
    PLX_KEY Key = NULL;
    LONG Error;

    *Result = NULL;
    switch (Id)
    {
        case LX_KEY_SPEC_THREAD_KEYRING:
            if (Cred->Thread->ThreadKeyring == NULL)
            {
                if (!(Flags & LX_KL_CREATE))
                    return -LX_ENOKEY;
                Error = LxKeyInstallThreadKeyring(Cred);
                if (Error != 0)
                    return Error;
            }
            Key = Cred->Thread->ThreadKeyring;
            break;

        case LX_KEY_SPEC_PROCESS_KEYRING:
            if (Cred->Thread->ProcessKeyring == NULL)
            {
                if (!(Flags & LX_KL_CREATE))
                    return -LX_ENOKEY;
                Error = LxKeyInstallProcessKeyring(Cred);
                if (Error != 0)
                    return Error;
            }
            Key = Cred->Thread->ProcessKeyring;
            break;

        case LX_KEY_SPEC_SESSION_KEYRING:
            if (Cred->Thread->SessionKeyring == NULL)
            {
                PLX_KEY UserSession;

                Error = LxKeyLookUpUserKeyrings(Cred, NULL, &UserSession);
                if (Error != 0)
                    return Error;
                if (Flags & LX_KL_CREATE)
                    Error = (LONG)LxKeyJoinSession(Cred, NULL);
                else
                    LxKeyInstallSessionKeyring(Cred, UserSession);
                LxKeyPut(UserSession);
                if (Error < 0)
                    return Error;
            }
            else if ((Cred->Thread->SessionKeyring->Flags & LX_KEY_F_UID_KEYRING) && (Flags & LX_KL_CREATE))
            {
                Error = (LONG)LxKeyJoinSession(Cred, NULL);
                if (Error < 0)
                    return Error;
            }
            Key = Cred->Thread->SessionKeyring;
            break;

        case LX_KEY_SPEC_USER_KEYRING:
            Error = LxKeyLookUpUserKeyrings(Cred, &Key, NULL);
            if (Error != 0)
                return Error;
            *Result = Key;
            return 0;

        case LX_KEY_SPEC_USER_SESSION_KEYRING:
            Error = LxKeyLookUpUserKeyrings(Cred, NULL, &Key);
            if (Error != 0)
                return Error;
            *Result = Key;
            return 0;

        case LX_KEY_SPEC_GROUP_KEYRING:
            return -LX_EINVAL;

        case LX_KEY_SPEC_REQKEY_AUTH_KEY:
            Key = Cred->Thread->RequestKeyAuth;
            if (Key == NULL)
                return -LX_ENOKEY;
            break;

        case LX_KEY_SPEC_REQUESTOR_KEYRING:
            if (Cred->Thread->RequestKeyAuth == NULL)
                return -LX_ENOKEY;
            if ((Cred->Thread->RequestKeyAuth->Flags & LX_KEY_F_REVOKED) || (Cred->Thread->RequestKeyAuth->Auth == NULL))
                return -LX_EKEYREVOKED;
            Key = Cred->Thread->RequestKeyAuth->Auth->Dest;
            if (Key == NULL)
                return -LX_ENOKEY;
            break;

        default:
            return -LX_EINVAL;
    }
    LxKeyGet(Key);
    *Result = Key;
    return 0;
}

static
LONG
LxKeyLookup(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ ULONG Flags,
    _In_ ULONG Need,
    _Out_ PLX_KEY *Result,
    _Out_opt_ PBOOLEAN Possessed)
{
    PLX_KEY Key;
    BOOLEAN Owned = TRUE;
    LONG Error;

    *Result = NULL;
    if (Possessed != NULL)
        *Possessed = FALSE;
    LxKeyCredSync(Cred);
    if (Id < 0)
    {
        Error = LxKeyLookupSpecial(Cred, Id, Flags, &Key);
        if (Error != 0)
            return Error;
    }
    else
    {
        if (Id == 0)
            return -LX_EINVAL;
        Key = LxKeyFindSerial(Id);
        if ((Key == NULL) || (Key->Usage == 0) || (Key->Instance != Cred->Instance))
            return -LX_ENOKEY;
        LxKeyGet(Key);
        Owned = LxKeyPossessed(Cred, Key, TRUE);
    }

    Error = 0;
    if (Need != LX_KEY_NEED_UNLINK)
    {
        if (!(Flags & LX_KL_PARTIAL))
        {
            Error = LxKeyWaitConstruction(Key);
            if ((Error == -LX_ERESTARTSYS) ||
                ((Error != 0) && (Need != LX_KEY_AUTH_OVERRIDE) && (Need != LX_KEY_DEFER_PERM_CHECK)))
            {
                LxKeyPut(Key);
                return Error;
            }
            Error = 0;
        }
        else if (Need != LX_KEY_DEFER_PERM_CHECK)
        {
            Error = LxKeyValidate(Key);
        }
        if ((Error == 0) && !(Flags & LX_KL_PARTIAL) && (Key->State == LX_KEY_UNINSTANTIATED))
            Error = -LX_EIO;
    }
    if (Error == 0)
        Error = LxKeyPermission(Key, Owned, Cred, Need);
    if (Error != 0)
    {
        LxKeyPut(Key);
        return Error;
    }
    *Result = Key;
    if (Possessed != NULL)
        *Possessed = Owned;
    return 0;
}

static
LONG
LxKeyFindAuthKey(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG TargetId,
    _Out_ PLX_KEY *Result)
{
    LX_KEY_SEARCH Search;
    CHAR Description[16];
    PLX_KEY Found;
    LONG Error;

    *Result = NULL;
    RtlStringCbPrintfA(Description, sizeof(Description), "%lx", (ULONG)TargetId);
    RtlZeroMemory(&Search, sizeof(Search));
    Search.Cred = Cred;
    Search.Type = LxKeyTypeAuth;
    Search.Description = Description;
    Search.Flags = LX_KS_STATE_CHECK | LX_KS_RECURSE;
    LxKeyCredSync(Cred);
    Error = LxKeySearchProcessKeyrings(&Search, &Found);
    if (Error != 0)
        return (Error == -LX_EAGAIN) ? -LX_ENOKEY : Error;
    if (Found->Flags & LX_KEY_F_REVOKED)
        return -LX_EKEYREVOKED;
    LxKeyGet(Found);
    *Result = Found;
    return 0;
}

static
LONG
LxKeyLookupOverride(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ ULONG Need,
    _Out_ PLX_KEY *Result)
{
    PLX_KEY Auth;
    LONG Error = LxKeyLookup(Cred, Id, LX_KL_PARTIAL, Need, Result, NULL);

    if ((Error != -LX_EACCES) || (LxKeyFindAuthKey(Cred, Id, &Auth) != 0))
        return Error;
    LxKeyPut(Auth);
    return LxKeyLookup(Cred, Id, LX_KL_PARTIAL, LX_KEY_AUTH_OVERRIDE, Result, NULL);
}

static
LONG
LxKeyCopyType(
    _In_ ULONG64 Source,
    _Out_ PSTR *Type)
{
    LONG64 Result = LxCopyStringFromUser(Source, Type, LX_KEY_TYPE_LENGTH - 1);

    if (Result == -LX_ENAMETOOLONG)
        return -LX_EINVAL;
    if (Result != 0)
        return (LONG)Result;
    if ((*Type)[0] == '\0')
    {
        LxFree(*Type);
        *Type = NULL;
        return -LX_EINVAL;
    }
    if ((*Type)[0] == '.')
    {
        LxFree(*Type);
        *Type = NULL;
        return -LX_EPERM;
    }
    return 0;
}

static
LONG
LxKeyCopyString(
    _In_ ULONG64 Source,
    _In_ SIZE_T Limit,
    _Out_ PSTR *String)
{
    LONG64 Result = LxCopyStringFromUser(Source, String, Limit - 1);

    if (Result == -LX_ENAMETOOLONG)
        return -LX_EINVAL;
    return (LONG)Result;
}

static
LONG
LxKeyCopyPayload(
    _In_ ULONG64 Source,
    _In_ ULONG64 Length,
    _Out_ PUCHAR *Payload)
{
    *Payload = NULL;
    if (Length == 0)
        return 0;
    *Payload = LxAllocPaged((SIZE_T)Length);
    if (*Payload == NULL)
        return -LX_ENOMEM;
    if (LxCopyFromUser(*Payload, Source, (SIZE_T)Length) != 0)
    {
        LxFreePaged(*Payload);
        *Payload = NULL;
        return -LX_EFAULT;
    }
    return 0;
}

static
VOID
LxKeyFreePayload(
    _In_opt_ PUCHAR Payload,
    _In_ ULONG64 Length)
{
    if (Payload != NULL)
    {
        RtlSecureZeroMemory(Payload, (SIZE_T)Length);
        LxFreePaged(Payload);
    }
}

static
LONG
LxKeyTypeLookup(
    _In_ PCSTR Name,
    _Out_ LX_KEY_TYPE_ID *Type)
{
    ULONG Index;

    for (Index = 0; Index < LxKeyTypeCount; Index++)
    {
        if (strcmp(LxKeyTypes[Index].Name, Name) == 0)
        {
            *Type = (LX_KEY_TYPE_ID)Index;
            return 0;
        }
    }
    return -LX_ENOKEY;
}

static
LONG
LxKeyPreparse(
    _In_ LX_KEY_TYPE_ID Type,
    _In_opt_ PUCHAR Payload,
    _In_ ULONG64 Length)
{
    if (Type == LxKeyTypeKeyring)
        return (Length != 0) ? -LX_EINVAL : 0;
    if ((Length == 0) || (Length > LxKeyTypes[Type].MaxData) || (Payload == NULL))
        return -LX_EINVAL;
    return 0;
}

static
ULONG
LxKeyPreparseQuota(
    _In_ LX_KEY_TYPE_ID Type,
    _In_ ULONG Length)
{
    return (Type == LxKeyTypeBig) ? 16 : Length;
}

static
LONG
LxKeySetPayload(
    _Inout_ PLX_KEY Key,
    _In_reads_bytes_(Length) PUCHAR Payload,
    _In_ ULONG Length,
    _In_ ULONG Quota)
{
    PUCHAR Copy = LxAllocPaged(Length);
    LONG Error;

    if (Copy == NULL)
        return -LX_ENOMEM;
    Error = LxKeyReserve(Key, Quota);
    if (Error != 0)
    {
        LxFreePaged(Copy);
        return Error;
    }
    RtlCopyMemory(Copy, Payload, Length);
    LxKeyFreeData(Key);
    Key->Data = Copy;
    Key->DataLength = Length;
    return 0;
}

static
LONG
LxKeyApplyUpdate(
    _Inout_ PLX_KEY Key,
    _In_ PUCHAR Payload,
    _In_ ULONG64 Length)
{
    LONG Error = LxKeySetPayload(Key, Payload, (ULONG)Length, (ULONG)Length);

    if (Error != 0)
        return Error;
    Key->Expiry = LX_KEY_PERMANENT;
    if (Key->State != LX_KEY_POSITIVE)
    {
        if (Key->State == LX_KEY_UNINSTANTIATED)
            Key->User->NIKeys++;
        Key->State = LX_KEY_POSITIVE;
    }
    return 0;
}

static
LONG
LxKeyUpdatePayload(
    _Inout_ PLX_KEY Key,
    _In_opt_ PUCHAR Payload,
    _In_ ULONG64 Length)
{
    LONG Error;

    if (!LxKeyTypes[Key->Type].Update)
        return -LX_EOPNOTSUPP;
    Error = LxKeyPreparse(Key->Type, Payload, Length);
    if (Error != 0)
        return Error;
    return LxKeyApplyUpdate(Key, Payload, Length);
}

static
ULONG
LxKeyDefaultPerm(
    _In_ LX_KEY_TYPE_ID Type)
{
    ULONG Perm = LX_KEY_POS_VIEW | LX_KEY_POS_SEARCH | LX_KEY_POS_LINK | LX_KEY_POS_SETATTR | LX_KEY_USR_VIEW;

    if (LxKeyTypes[Type].Read)
        Perm |= LX_KEY_POS_READ;
    if ((Type == LxKeyTypeKeyring) || LxKeyTypes[Type].Update)
        Perm |= LX_KEY_POS_WRITE;
    return Perm;
}

static
LONG64
LxKeyCreateOrUpdate(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ PLX_KEY Keyring,
    _In_ BOOLEAN Possessed,
    _In_ PCSTR TypeName,
    _In_opt_ PCSTR Description,
    _In_opt_ PUCHAR Payload,
    _In_ ULONG64 Length)
{
    LX_KEY_TYPE_ID Type;
    BOOLEAN Replace;
    PLX_KEY Key;
    LONG Error;

    if (LxKeyTypeLookup(TypeName, &Type) != 0)
        return -LX_ENODEV;
    if (Keyring->Type != LxKeyTypeKeyring)
        return -LX_ENOTDIR;
    Error = LxKeyPreparse(Type, Payload, Length);
    if (Error != 0)
        return Error;
    if (Description == NULL)
        return -LX_EINVAL;
    Error = LxKeyLinkCheck(Keyring, Type, Description, &Replace);
    if (Error != 0)
        return Error;
    if (Keyring->Restricted)
        return -LX_EPERM;
    Error = LxKeyPermission(Keyring, Possessed, Cred, LX_KEY_NEED_WRITE);
    if (Error != 0)
        return Error;

    if (LxKeyTypes[Type].Update)
    {
        Key = LxKeyringFind(Keyring, Type, Description);
        if ((Key != NULL) && !(Key->Flags & (LX_KEY_F_INVALIDATED | LX_KEY_F_REVOKED)))
        {
            Error = LxKeyPermission(Key, Possessed, Cred, LX_KEY_NEED_WRITE);
            if (Error != 0)
                return Error;
            if (Key->Flags & LX_KEY_F_CONSTRUCT)
            {
                LxKeyGet(Key);
                Error = LxKeyWaitConstruction(Key);
                if (Error == -LX_ERESTARTSYS)
                {
                    LxKeyPut(Key);
                    return Error;
                }
                LxKeyPut(Key);
            }
            Error = LxKeyApplyUpdate(Key, Payload, Length);
            return (Error != 0) ? Error : Key->Serial;
        }
    }

    Error = LxKeyAlloc(Cred, Type, Description, Cred->Fsuid, Cred->Fsgid, LxKeyDefaultPerm(Type), LX_KEY_A_IN_QUOTA, &Key);
    if (Error != 0)
        return Error;
    if (Type != LxKeyTypeKeyring)
    {
        Error = LxKeySetPayload(Key, Payload, (ULONG)Length, LxKeyPreparseQuota(Type, (ULONG)Length));
        if (Error != 0)
        {
            LxKeyPut(Key);
            return Error;
        }
    }
    LxKeyMarkInstantiated(Key, LX_KEY_POSITIVE);
    LxKeyPublishName(Key);
    Error = LxKeyLinkCommit(Keyring, Key);
    LxKeyPut(Key);
    return (Error != 0) ? Error : Key->Serial;
}

static
VOID
LxKeyInvalidateLinks(
    _In_ PLX_KEY Dead)
{
    PLIST_ENTRY Entry;

    for (Entry = LxKeyList.Flink; Entry != &LxKeyList; Entry = Entry->Flink)
    {
        PLX_KEY Keyring = CONTAINING_RECORD(Entry, LX_KEY, Link);
        ULONG Index = 0;

        if ((Keyring->Type != LxKeyTypeKeyring) || (Keyring->Usage == 0))
            continue;
        while (Index < Keyring->LinkCount)
        {
            if (Keyring->Links[Index] == Dead)
                LxKeyringRemoveAt(Keyring, Index);
            else
                Index++;
        }
    }
}

static
BOOLEAN
LxKeyDeadForGc(
    _In_ PLX_KEY Key,
    _In_ LONG64 Now)
{
    if (Key->Flags & LX_KEY_F_INVALIDATED)
        return TRUE;
    return (Key->Expiry != LX_KEY_PERMANENT) && (Key->Expiry + LxKeyGcDelay <= Now);
}

static
VOID
LxKeyCollect(VOID)
{
    LONG64 Now = LxKeyNow(), Next = LX_KEY_PERMANENT;
    PLIST_ENTRY Entry;

    if (Now < LxKeyNextGc)
        return;
    for (Entry = LxKeyList.Flink; Entry != &LxKeyList; Entry = Entry->Flink)
    {
        PLX_KEY Key = CONTAINING_RECORD(Entry, LX_KEY, Link);
        ULONG Index = 0;

        if ((Key->Expiry != LX_KEY_PERMANENT) && !LxKeyDeadForGc(Key, Now) && (Key->Expiry + LxKeyGcDelay < Next))
            Next = Key->Expiry + LxKeyGcDelay;
        if ((Key->Type != LxKeyTypeKeyring) || (Key->Usage == 0))
            continue;
        while (Index < Key->LinkCount)
        {
            if (LxKeyDeadForGc(Key->Links[Index], Now))
                LxKeyringRemoveAt(Key, Index);
            else
                Index++;
        }
    }
    LxKeyNextGc = Next;
}

static
VOID
LxKeyRevoke(
    _Inout_ PLX_KEY Key)
{
    LONG64 Now = LxKeyNow();

    if (Key->Flags & LX_KEY_F_REVOKED)
        return;
    Key->Flags |= LX_KEY_F_REVOKED;
    switch (Key->Type)
    {
        case LxKeyTypeKeyring:
            LxKeyringClearLinks(Key);
            LxKeyReserve(Key, 0);
            break;
        case LxKeyTypeUser:
        case LxKeyTypeLogon:
            LxKeyReserve(Key, 0);
            LxKeyFreeData(Key);
            Key->DataLength = 0;
            break;
        case LxKeyTypeBig:
            LxKeyReserve(Key, 0);
            break;
        case LxKeyTypeAuth:
            LxKeyAuthFree(Key);
            break;
        default:
            break;
    }
    if (Key->Expiry > Now)
        LxKeySetExpiry(Key, Now);
}

static
VOID
LxKeyInvalidate(
    _Inout_ PLX_KEY Key)
{
    if (Key->Flags & LX_KEY_F_INVALIDATED)
        return;
    Key->Flags |= LX_KEY_F_INVALIDATED;
    LxKeyInvalidateLinks(Key);
}

static
LONG
LxKeyRejectAndLink(
    _Inout_ PLX_KEY Key,
    _In_ ULONG Timeout,
    _In_ LONG Error,
    _In_opt_ PLX_KEY Keyring,
    _In_opt_ PLX_KEY Auth)
{
    BOOLEAN Replace;
    LONG LinkError = 0;

    if (Keyring != NULL)
        LinkError = LxKeyLinkCheck(Keyring, Key->Type, Key->Description, &Replace);
    if (Key->State != LX_KEY_UNINSTANTIATED)
        return -LX_EBUSY;
    LxKeyMarkInstantiated(Key, -Error);
    LxKeySetExpiry(Key, LxKeyNow() + Timeout);
    if ((Keyring != NULL) && (LinkError == 0))
        LinkError = LxKeyLinkCommit(Keyring, Key);
    if (Auth != NULL)
        LxKeyInvalidate(Auth);
    return LinkError;
}

static
LONG
LxKeyInstantiateAndLink(
    _Inout_ PLX_KEY Key,
    _In_opt_ PUCHAR Payload,
    _In_ ULONG64 Length,
    _In_opt_ PLX_KEY Keyring,
    _In_opt_ PLX_KEY Auth)
{
    BOOLEAN Replace;
    LONG Error;

    Error = LxKeyPreparse(Key->Type, Payload, Length);
    if (Error != 0)
        return Error;
    if (Keyring != NULL)
    {
        Error = LxKeyLinkCheck(Keyring, Key->Type, Key->Description, &Replace);
        if ((Error == 0) && Keyring->Restricted)
            Error = -LX_EPERM;
        if (Error != 0)
            return Error;
    }
    if (Key->State != LX_KEY_UNINSTANTIATED)
        return -LX_EBUSY;
    if (Key->Type != LxKeyTypeKeyring)
    {
        Error = LxKeySetPayload(Key, Payload, (ULONG)Length, LxKeyPreparseQuota(Key->Type, (ULONG)Length));
        if (Error != 0)
            return Error;
    }
    LxKeyMarkInstantiated(Key, LX_KEY_POSITIVE);
    if (Keyring != NULL)
        Error = LxKeyLinkCommit(Keyring, Key);
    if (Auth != NULL)
        LxKeyInvalidate(Auth);
    return Error;
}

static
VOID
LxKeyTextAppend(
    _Inout_ PLX_KEY_TEXT Text,
    _In_ PCSTR Format,
    ...)
{
    va_list Arguments;
    CHAR Line[512];
    SIZE_T Length;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Line, sizeof(Line), Format, Arguments);
    va_end(Arguments);
    Length = strlen(Line);
    if (Text->Failed)
        return;
    if (Text->Length + Length + 1 > Text->Capacity)
    {
        ULONG Capacity = max(Text->Capacity * 2, Text->Length + (ULONG)Length + 1024);
        PSTR Buffer = LxAlloc(Capacity);

        if (Buffer == NULL)
        {
            Text->Failed = TRUE;
            return;
        }
        if (Text->Length != 0)
            RtlCopyMemory(Buffer, Text->Buffer, Text->Length);
        LxFree(Text->Buffer);
        Text->Buffer = Buffer;
        Text->Capacity = Capacity;
    }
    RtlCopyMemory(Text->Buffer + Text->Length, Line, Length);
    Text->Length += (ULONG)Length;
    Text->Buffer[Text->Length] = '\0';
}

static
ULONG
LxKeyViewUid(
    _In_ PLX_NS UserNs,
    _In_ ULONG Uid)
{
    return (Uid == LX_KEY_INVALID_ID) ? LxCredOverflowId(FALSE) : LxIdFromKernelMunged(UserNs, FALSE, Uid);
}

static
ULONG
LxKeyViewGid(
    _In_ PLX_NS UserNs,
    _In_ ULONG Gid)
{
    return (Gid == LX_KEY_INVALID_ID) ? LxCredOverflowId(TRUE) : LxIdFromKernelMunged(UserNs, TRUE, Gid);
}

static
VOID
LxKeyDescribeType(
    _Inout_ PLX_KEY_TEXT Text,
    _In_ PLX_KEY Key)
{
    switch (Key->Type)
    {
        case LxKeyTypeKeyring:
            LxKeyTextAppend(Text, "%s", Key->Description);
            if (Key->State == LX_KEY_POSITIVE)
            {
                if (Key->LinkCount != 0)
                    LxKeyTextAppend(Text, ": %lu", Key->LinkCount);
                else
                    LxKeyTextAppend(Text, ": empty");
            }
            break;
        case LxKeyTypeBig:
            LxKeyTextAppend(Text, "%s", Key->Description);
            if (Key->State == LX_KEY_POSITIVE)
                LxKeyTextAppend(Text, ": %lu [%s]", Key->DataLength, (Key->DataLength > LX_KEY_BIG_THRESHOLD) ? "file" : "buff");
            break;
        case LxKeyTypeAuth:
            LxKeyTextAppend(Text, "key:%s", Key->Description);
            if ((Key->State == LX_KEY_POSITIVE) && (Key->Auth != NULL))
                LxKeyTextAppend(Text, " pid:%lu ci:%lu", Key->Auth->Cred.Pid, Key->Auth->CalloutLength);
            break;
        default:
            LxKeyTextAppend(Text, "%s", Key->Description);
            if (Key->State == LX_KEY_POSITIVE)
                LxKeyTextAppend(Text, ": %lu", Key->Reserved);
            break;
    }
}

static
LONG64
LxKeyRead(
    _In_ PLX_KEY Key,
    _Out_ PUCHAR *Copy,
    _In_ ULONG64 Length)
{
    ULONG64 Size;

    *Copy = NULL;
    if (Key->Type == LxKeyTypeKeyring)
    {
        if ((Length != 0) && (Length <= LX_KEY_PAGE) && (Length & 3))
            return -LX_EINVAL;
        Size = (ULONG64)Key->LinkCount * sizeof(LONG);
        if ((Length != 0) && (Size <= Length) && (Size != 0))
        {
            ULONG Index;

            *Copy = LxAllocPaged((SIZE_T)Size);
            if (*Copy == NULL)
                return -LX_ENOMEM;
            for (Index = 0; Index < Key->LinkCount; Index++)
                ((PLONG)*Copy)[Index] = Key->Links[Index]->Serial;
        }
        return (LONG64)Size;
    }
    if (Key->Type == LxKeyTypeAuth)
        Size = (Key->Auth != NULL) ? Key->Auth->CalloutLength : 0;
    else
        Size = Key->DataLength;
    if ((Length != 0) && (Size <= Length) && (Size != 0))
    {
        *Copy = LxAllocPaged((SIZE_T)Size);
        if (*Copy == NULL)
            return -LX_ENOMEM;
        RtlCopyMemory(*Copy, (Key->Type == LxKeyTypeAuth) ? Key->Auth->Callout : Key->Data, (SIZE_T)Size);
    }
    return (LONG64)Size;
}

static
LONG64
LxKeyctlGetKeyringId(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ LONG Create)
{
    PLX_KEY Key;
    LONG Error = LxKeyLookup(Cred, Id, Create ? LX_KL_CREATE : 0, LX_KEY_NEED_SEARCH, &Key, NULL);
    LONG Serial;

    if (Error != 0)
        return Error;
    Serial = Key->Serial;
    LxKeyPut(Key);
    return Serial;
}

static
LONG64
LxKeyctlUpdate(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_opt_ PUCHAR Payload,
    _In_ ULONG64 Length)
{
    PLX_KEY Key;
    LONG Error = LxKeyLookup(Cred, Id, 0, LX_KEY_NEED_WRITE, &Key, NULL);

    if (Error != 0)
        return Error;
    Error = LxKeyUpdatePayload(Key, Payload, Length);
    LxKeyPut(Key);
    return Error;
}

static
LONG64
LxKeyctlRevoke(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id)
{
    PLX_KEY Key;
    LONG Error = LxKeyLookup(Cred, Id, 0, LX_KEY_NEED_WRITE, &Key, NULL);

    if (Error == -LX_EACCES)
        Error = LxKeyLookup(Cred, Id, 0, LX_KEY_NEED_SETATTR, &Key, NULL);
    if (Error != 0)
        return Error;
    LxKeyRevoke(Key);
    LxKeyPut(Key);
    return 0;
}

static
LONG64
LxKeyctlChown(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ ULONG UserId,
    _In_ ULONG GroupId)
{
    ULONG Uid = LX_KEY_INVALID_ID, Gid = LX_KEY_INVALID_ID;
    PLX_KEY_USER Owner;
    PLX_KEY Key;
    LONG Error;

    if ((UserId == MAXULONG) && (GroupId == MAXULONG))
        return 0;
    if (UserId != MAXULONG)
    {
        Uid = LxIdToKernel(Cred->UserNs, FALSE, UserId);
        if (Uid == MAXULONG)
            return -LX_EINVAL;
    }
    if (GroupId != MAXULONG)
    {
        Gid = LxIdToKernel(Cred->UserNs, TRUE, GroupId);
        if (Gid == MAXULONG)
            return -LX_EINVAL;
    }
    Error = LxKeyLookup(Cred, Id, LX_KL_CREATE | LX_KL_PARTIAL, LX_KEY_NEED_SETATTR, &Key, NULL);
    if (Error != 0)
        return Error;
    if ((((Uid != LX_KEY_INVALID_ID) && (Uid != Key->Uid)) ||
         ((Gid != LX_KEY_INVALID_ID) && (Gid != Key->Gid) && !LxKeyInGroup(Cred, Gid))) &&
        !LxCapable(Cred->Thread->Process, LX_CAP_SYS_ADMIN))
    {
        LxKeyPut(Key);
        return -LX_EACCES;
    }
    if ((Uid != LX_KEY_INVALID_ID) && (Uid != Key->Uid))
    {
        Owner = LxKeyUserLookup(Key->Instance, Uid);
        if (Owner == NULL)
        {
            LxKeyPut(Key);
            return -LX_ENOMEM;
        }
        if ((Key->Flags & LX_KEY_F_IN_QUOTA) &&
            ((Owner->QnKeys + 1 > LxKeyQuotaKeys(Uid)) ||
             ((ULONG64)Owner->QnBytes + Key->QuotaLength > LxKeyQuotaBytes(Uid))))
        {
            LxKeyUserPut(Owner);
            LxKeyPut(Key);
            return -LX_EDQUOT;
        }
        if (Key->Flags & LX_KEY_F_IN_QUOTA)
        {
            Owner->QnKeys++;
            Owner->QnBytes += Key->QuotaLength;
            Key->User->QnKeys--;
            Key->User->QnBytes -= Key->QuotaLength;
        }
        Owner->NKeys++;
        Key->User->NKeys--;
        if (Key->State != LX_KEY_UNINSTANTIATED)
        {
            Owner->NIKeys++;
            Key->User->NIKeys--;
        }
        LxKeyUserPut(Key->User);
        Key->User = Owner;
        Key->Uid = Uid;
    }
    if (Gid != LX_KEY_INVALID_ID)
        Key->Gid = Gid;
    LxKeyPut(Key);
    return 0;
}

static
LONG64
LxKeyctlSetperm(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ ULONG Perm)
{
    PLX_KEY Key;
    LONG Error;

    if (Perm & ~LX_KEY_PERM_MASK)
        return -LX_EINVAL;
    Error = LxKeyLookup(Cred, Id, LX_KL_CREATE | LX_KL_PARTIAL, LX_KEY_NEED_SETATTR, &Key, NULL);
    if (Error != 0)
        return Error;
    Error = -LX_EACCES;
    if (Key->Uid == Cred->Fsuid)
    {
        Key->Perm = Perm;
        Error = 0;
    }
    LxKeyPut(Key);
    return Error;
}

static
LONG64
LxKeyctlDescribe(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _Out_ PLX_KEY_TEXT Text)
{
    PLX_KEY Key;
    LONG Error = LxKeyLookupOverride(Cred, Id, LX_KEY_NEED_VIEW, &Key);

    if (Error != 0)
        return Error;
    LxKeyTextAppend(Text, "%s;%ld;%ld;%08lx;%s", LxKeyTypes[Key->Type].Name, (LONG)LxKeyViewUid(Cred->UserNs, Key->Uid),
                    (LONG)LxKeyViewGid(Cred->UserNs, Key->Gid), Key->Perm, Key->Description);
    LxKeyPut(Key);
    if (Text->Failed)
        return -LX_ENOMEM;
    return Text->Length + 1;
}

static
LONG64
LxKeyctlClear(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id)
{
    PLX_KEY Keyring;
    LONG Error = LxKeyLookup(Cred, Id, LX_KL_CREATE, LX_KEY_NEED_WRITE, &Keyring, NULL);

    if (Error != 0)
        return Error;
    if (Keyring->Type != LxKeyTypeKeyring)
    {
        Error = -LX_ENOTDIR;
    }
    else
    {
        LxKeyringClearLinks(Keyring);
        LxKeyReserve(Keyring, 0);
    }
    LxKeyPut(Keyring);
    return Error;
}

static
LONG64
LxKeyctlLink(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ LONG RingId)
{
    PLX_KEY Keyring, Key;
    LONG Error = LxKeyLookup(Cred, RingId, LX_KL_CREATE, LX_KEY_NEED_WRITE, &Keyring, NULL);

    if (Error != 0)
        return Error;
    Error = LxKeyLookup(Cred, Id, LX_KL_CREATE, LX_KEY_NEED_LINK, &Key, NULL);
    if (Error == 0)
    {
        Error = LxKeyLink(Keyring, Key);
        LxKeyPut(Key);
    }
    LxKeyPut(Keyring);
    return Error;
}

static
LONG64
LxKeyctlUnlink(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ LONG RingId)
{
    PLX_KEY Keyring, Key;
    LONG Error = LxKeyLookup(Cred, RingId, 0, LX_KEY_NEED_WRITE, &Keyring, NULL);
    LONG Index;

    if (Error != 0)
        return Error;
    Error = LxKeyLookup(Cred, Id, LX_KL_PARTIAL, LX_KEY_NEED_UNLINK, &Key, NULL);
    if (Error == 0)
    {
        if (Keyring->Type != LxKeyTypeKeyring)
        {
            Error = -LX_ENOTDIR;
        }
        else
        {
            Index = LxKeyringIndexOf(Keyring, Key->Type, Key->Description);
            if (Index < 0)
                Error = -LX_ENOENT;
            else
                LxKeyringRemoveAt(Keyring, (ULONG)Index);
        }
        LxKeyPut(Key);
    }
    LxKeyPut(Keyring);
    return Error;
}

static
LONG64
LxKeyctlSearch(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG RingId,
    _In_ PCSTR TypeName,
    _In_ PCSTR Description,
    _In_ LONG DestId)
{
    PLX_KEY Keyring, Dest = NULL, Found = NULL;
    BOOLEAN Possessed;
    LX_KEY_SEARCH Search;
    LX_KEY_TYPE_ID Type;
    LONG Error, Serial;

    Error = LxKeyLookup(Cred, RingId, 0, LX_KEY_NEED_SEARCH, &Keyring, &Possessed);
    if (Error != 0)
        return Error;
    if (DestId != 0)
    {
        Error = LxKeyLookup(Cred, DestId, LX_KL_CREATE, LX_KEY_NEED_WRITE, &Dest, NULL);
        if (Error != 0)
        {
            LxKeyPut(Keyring);
            return Error;
        }
    }
    Error = LxKeyTypeLookup(TypeName, &Type);
    if (Error == 0)
    {
        RtlZeroMemory(&Search, sizeof(Search));
        Search.Cred = Cred;
        Search.Type = Type;
        Search.Description = Description;
        Search.Flags = LX_KS_STATE_CHECK | LX_KS_RECURSE;
        Error = LxKeyringSearchIn(Keyring, Possessed, &Search, &Found);
        if (Error == -LX_EAGAIN)
            Error = -LX_ENOKEY;
    }
    if ((Error == 0) && (Dest != NULL))
    {
        Error = LxKeyPermission(Found, Search.Possessed, Cred, LX_KEY_NEED_LINK);
        if (Error == 0)
            Error = LxKeyLink(Dest, Found);
    }
    Serial = (Error == 0) ? Found->Serial : Error;
    LxKeyPut(Dest);
    LxKeyPut(Keyring);
    return Serial;
}

static
LONG64
LxKeyctlRead(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ ULONG64 Length,
    _Out_ PUCHAR *Copy)
{
    PLX_KEY Key;
    BOOLEAN Possessed;
    LONG64 Result;
    LONG Error;

    *Copy = NULL;
    if (LxKeyLookup(Cred, Id, 0, LX_KEY_DEFER_PERM_CHECK, &Key, &Possessed) != 0)
        return -LX_ENOKEY;
    if (Key->State < 0)
    {
        Error = Key->State;
        LxKeyPut(Key);
        return Error;
    }
    Error = LxKeyPermission(Key, Possessed, Cred, LX_KEY_NEED_READ);
    if ((Error == -LX_EACCES) && Possessed)
        Error = 0;
    if ((Error == 0) && !LxKeyTypes[Key->Type].Read)
        Error = -LX_EOPNOTSUPP;
    if (Error == 0)
        Error = LxKeyValidate(Key);
    if (Error != 0)
    {
        LxKeyPut(Key);
        return Error;
    }
    Result = LxKeyRead(Key, Copy, Length);
    LxKeyPut(Key);
    return Result;
}

static
LONG64
LxKeyctlSetTimeout(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ ULONG Timeout)
{
    PLX_KEY Key;
    LONG Error = LxKeyLookup(Cred, Id, LX_KL_CREATE | LX_KL_PARTIAL, LX_KEY_NEED_SETATTR, &Key, NULL);
    PLX_KEY Auth;

    if ((Error == -LX_EACCES) && (LxKeyFindAuthKey(Cred, Id, &Auth) == 0))
    {
        LxKeyPut(Auth);
        Error = LxKeyLookup(Cred, Id, LX_KL_PARTIAL, LX_KEY_AUTH_OVERRIDE, &Key, NULL);
    }
    if (Error != 0)
        return Error;
    LxKeySetExpiry(Key, (Timeout != 0) ? LxKeyNow() + Timeout : LX_KEY_PERMANENT);
    LxKeyPut(Key);
    return 0;
}

static
LONG64
LxKeyctlInvalidate(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id)
{
    PLX_KEY Key;
    LONG Error = LxKeyLookup(Cred, Id, 0, LX_KEY_NEED_SEARCH, &Key, NULL);

    if (Error != 0)
        return Error;
    LxKeyInvalidate(Key);
    LxKeyPut(Key);
    return 0;
}

static
LONG64
LxKeyctlSetReqkeyKeyring(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Default)
{
    LONG Old = Cred->Thread->JitKeyring;
    LONG Error = 0;

    switch (Default)
    {
        case LX_KEY_REQKEY_DEFL_THREAD_KEYRING:
            Error = LxKeyInstallThreadKeyring(Cred);
            break;
        case LX_KEY_REQKEY_DEFL_PROCESS_KEYRING:
            Error = LxKeyInstallProcessKeyring(Cred);
            break;
        case LX_KEY_REQKEY_DEFL_DEFAULT:
        case LX_KEY_REQKEY_DEFL_SESSION_KEYRING:
        case LX_KEY_REQKEY_DEFL_USER_KEYRING:
        case LX_KEY_REQKEY_DEFL_USER_SESSION_KEYRING:
        case LX_KEY_REQKEY_DEFL_REQUESTOR_KEYRING:
            break;
        case LX_KEY_REQKEY_DEFL_NO_CHANGE:
            return Old;
        default:
            return -LX_EINVAL;
    }
    if (Error != 0)
        return Error;
    Cred->Thread->JitKeyring = Default;
    return Old;
}

static
LONG64
LxKeyctlAssumeAuthority(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id)
{
    PLX_KEY Auth;
    LONG Error;

    if (Id < 0)
        return -LX_EINVAL;
    if (Id == 0)
    {
        LxKeyInstall(Cred, &Cred->Thread->RequestKeyAuth, NULL);
        return 0;
    }
    Error = LxKeyFindAuthKey(Cred, Id, &Auth);
    if (Error != 0)
        return Error;
    LxKeyInstall(Cred, &Cred->Thread->RequestKeyAuth, Auth);
    LxKeyPut(Auth);
    return Auth->Serial;
}

static
LONG64
LxKeyctlGetSecurity(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id)
{
    PLX_KEY Key;
    LONG Error = LxKeyLookupOverride(Cred, Id, LX_KEY_NEED_VIEW, &Key);

    if (Error != 0)
        return Error;
    LxKeyPut(Key);
    return 1;
}

static
LONG64
LxKeyctlSessionToParent(
    _Inout_ PLX_KEY_CRED Cred)
{
    PLX_PROCESS Parent;
    PLX_THREAD Target = NULL;
    PLX_KEY Keyring;
    LONG Error;

    Error = LxKeyLookup(Cred, LX_KEY_SPEC_SESSION_KEYRING, 0, LX_KEY_NEED_LINK, &Keyring, NULL);
    if (Error != 0)
        return Error;
    Parent = LxProcessLookup(Cred->Instance, Cred->Thread->Process->ParentPid);
    if (Parent == NULL)
    {
        LxKeyPut(Keyring);
        return -LX_EPERM;
    }
    ExReleaseFastMutex(&LxKeyLock);
    ExAcquireFastMutex(&Parent->Lock);
    ExAcquireFastMutex(&LxKeyLock);
    Error = -LX_EPERM;
    if ((Parent->Pid > 1) && !Parent->Exited && !IsListEmpty(&Parent->Threads) &&
        (Parent->Threads.Flink->Flink == &Parent->Threads))
    {
        Target = CONTAINING_RECORD(Parent->Threads.Flink, LX_THREAD, Link);
        LxKeyCredSync(Cred);
        if (Cred->SessionKeyring == Target->SessionKeyring)
        {
            Error = 0;
        }
        else if ((Parent->Cred.Uid[0] == Cred->Euid) && (Parent->Cred.Uid[1] == Cred->Euid) && (Parent->Cred.Uid[2] == Cred->Euid) &&
                 (Parent->Cred.Gid[0] == Cred->Egid) && (Parent->Cred.Gid[1] == Cred->Egid) && (Parent->Cred.Gid[2] == Cred->Egid) &&
                 ((Target->SessionKeyring == NULL) || (Target->SessionKeyring->Uid == Cred->Euid)) &&
                 (Keyring->Uid == Cred->Euid))
        {
            LxKeySwap(&Target->SessionKeyring, Keyring);
            Error = 0;
        }
    }
    LxKeyPut(Keyring);
    LxKeyRelease();
    ExReleaseFastMutex(&Parent->Lock);
    LxProcessDereference(Parent);
    LxKeyAcquire();
    return Error;
}

static
LONG64
LxKeyctlGetPersistent(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ ULONG UserId,
    _In_ LONG DestId)
{
    PLX_NS UserNs = Cred->UserNs;
    PLX_KEY Dest, Register, Persistent;
    BOOLEAN Replace;
    ULONG Uid;
    CHAR Name[32];
    LONG Error;

    if (UserId == MAXULONG)
    {
        Uid = Cred->Uid;
    }
    else
    {
        Uid = LxIdToKernel(UserNs, FALSE, UserId);
        if (Uid == MAXULONG)
            return -LX_EINVAL;
        if ((Uid != Cred->Uid) && (Uid != Cred->Euid) && !LxNsCapable(Cred->Thread->Process, UserNs, LX_CAP_SETUID))
            return -LX_EPERM;
    }
    Error = LxKeyLookup(Cred, DestId, LX_KL_CREATE, LX_KEY_NEED_WRITE, &Dest, NULL);
    if (Error != 0)
        return Error;
    if (Dest->Type != LxKeyTypeKeyring)
    {
        LxKeyPut(Dest);
        return -LX_ENOTDIR;
    }
    RtlStringCbPrintfA(Name, sizeof(Name), "_persistent.%lu", LxIdFromKernel(UserNs, FALSE, Uid));
    Register = UserNs->User.PersistentRegister;
    if (Register == NULL)
    {
        Error = LxKeyringAlloc(Cred, ".persistent_register", 0, 0,
                               (LX_KEY_POS_ALL & ~LX_KEY_POS_SETATTR) | LX_KEY_USR_VIEW | LX_KEY_USR_READ,
                               LX_KEY_A_NOT_IN_QUOTA, NULL, &Register);
        if (Error != 0)
        {
            LxKeyPut(Dest);
            return Error;
        }
        UserNs->User.PersistentRegister = Register;
    }
    Persistent = LxKeyringFind(Register, LxKeyTypeKeyring, Name);
    if ((Persistent != NULL) && !(Persistent->Flags & (LX_KEY_F_INVALIDATED | LX_KEY_F_REVOKED)))
    {
        LxKeyGet(Persistent);
    }
    else
    {
        Error = LxKeyLinkCheck(Register, LxKeyTypeKeyring, Name, &Replace);
        if (Error == 0)
        {
            Error = LxKeyringAlloc(Cred, Name, Uid, LX_KEY_INVALID_ID,
                                   (LX_KEY_POS_ALL & ~LX_KEY_POS_SETATTR) | LX_KEY_USR_VIEW | LX_KEY_USR_READ,
                                   LX_KEY_A_NOT_IN_QUOTA, Register, &Persistent);
        }
        if (Error != 0)
        {
            LxKeyPut(Dest);
            return Error;
        }
    }
    Error = LxKeyPermission(Persistent, TRUE, Cred, LX_KEY_NEED_LINK);
    if (Error == 0)
        Error = LxKeyLink(Dest, Persistent);
    if (Error == 0)
        LxKeySetExpiry(Persistent, LxKeyNow() + LxKeyPersistentExpiry);
    Error = (Error == 0) ? Persistent->Serial : Error;
    LxKeyPut(Persistent);
    LxKeyPut(Dest);
    return Error;
}

static
LONG64
LxKeyctlRestrict(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ BOOLEAN HasType,
    _In_ BOOLEAN HasRestriction,
    _In_ LONG CopyError,
    _In_opt_ PCSTR TypeName)
{
    LX_KEY_TYPE_ID Type;
    PLX_KEY Keyring;
    LONG Error = LxKeyLookup(Cred, Id, 0, LX_KEY_NEED_SETATTR, &Keyring, NULL);

    if (Error != 0)
        return Error;
    if (HasType != HasRestriction)
        Error = -LX_EINVAL;
    else if (CopyError != 0)
        Error = CopyError;
    else if (Keyring->Type != LxKeyTypeKeyring)
        Error = -LX_ENOTDIR;
    else if (HasType)
        Error = (LxKeyTypeLookup(TypeName, &Type) != 0) ? -LX_ENOKEY : -LX_ENOENT;
    else if (Keyring->Restricted)
        Error = -LX_EEXIST;
    else
        Keyring->Restricted = TRUE;
    LxKeyPut(Keyring);
    return Error;
}

static
LONG64
LxKeyctlMove(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_ LONG FromId,
    _In_ LONG ToId,
    _In_ ULONG Flags)
{
    PLX_KEY Key, From = NULL, To = NULL;
    BOOLEAN Replace;
    LONG Error, Index;

    if (Flags & ~LX_KEYCTL_MOVE_EXCL)
        return -LX_EINVAL;
    Error = LxKeyLookup(Cred, Id, LX_KL_CREATE, LX_KEY_NEED_LINK, &Key, NULL);
    if (Error != 0)
        return Error;
    Error = LxKeyLookup(Cred, FromId, 0, LX_KEY_NEED_WRITE, &From, NULL);
    if (Error == 0)
        Error = LxKeyLookup(Cred, ToId, LX_KL_CREATE, LX_KEY_NEED_WRITE, &To, NULL);
    if ((Error == 0) && (From != To))
    {
        Index = (From->Type == LxKeyTypeKeyring) ? LxKeyringIndexOf(From, Key->Type, Key->Description) : -1;
        if (From->Type != LxKeyTypeKeyring)
            Error = -LX_ENOTDIR;
        else if (Index < 0)
            Error = -LX_ENOENT;
        if (Error == 0)
            Error = LxKeyLinkCheck(To, Key->Type, Key->Description, &Replace);
        if ((Error == 0) && Replace && (Flags & LX_KEYCTL_MOVE_EXCL))
            Error = -LX_EEXIST;
        if ((Error == 0) && To->Restricted)
            Error = -LX_EPERM;
        if ((Error == 0) && (Key->Type == LxKeyTypeKeyring))
            Error = LxKeyDetectCycle(To, Key);
        if (Error == 0)
        {
            LxKeyGet(Key);
            LxKeyringRemoveAt(From, (ULONG)Index);
            Error = LxKeyLinkCommit(To, Key);
            LxKeyPut(Key);
        }
    }
    LxKeyPut(To);
    LxKeyPut(From);
    LxKeyPut(Key);
    return Error;
}

static
LONG
LxKeyInstantiationKeyring(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG RingId,
    _In_ PLX_KEY_AUTH Auth,
    _Out_ PLX_KEY *Dest)
{
    *Dest = NULL;
    if (RingId == 0)
        return 0;
    if (RingId > 0)
        return LxKeyLookup(Cred, RingId, LX_KL_CREATE, LX_KEY_NEED_WRITE, Dest, NULL);
    if (RingId == LX_KEY_SPEC_REQKEY_AUTH_KEY)
        return -LX_EINVAL;
    if (RingId >= LX_KEY_SPEC_REQUESTOR_KEYRING)
    {
        *Dest = Auth->Dest;
        if (*Dest != NULL)
            LxKeyGet(*Dest);
        return 0;
    }
    return -LX_ENOKEY;
}

static
LONG
LxKeyAuthorityCheck(
    _In_ PLX_THREAD Thread,
    _In_ LONG Id)
{
    PLX_KEY Auth = Thread->RequestKeyAuth;

    if ((Auth == NULL) || (Auth->Auth == NULL) || (Auth->Auth->Target->Serial != Id))
        return -LX_EPERM;
    return 0;
}

static
LONG64
LxKeyctlInstantiate(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LONG Id,
    _In_opt_ PUCHAR Payload,
    _In_ ULONG64 Length,
    _In_ LONG RingId,
    _In_ BOOLEAN Reject,
    _In_ ULONG Timeout,
    _In_ LONG RejectError)
{
    PLX_KEY Auth, Dest, Target;
    LONG Error;

    LxKeyCredSync(Cred);
    Error = LxKeyAuthorityCheck(Cred->Thread, Id);
    if (Error != 0)
        return Error;
    Auth = Cred->Thread->RequestKeyAuth;
    Target = Auth->Auth->Target;
    LxKeyGet(Auth);
    LxKeyGet(Target);
    Error = LxKeyInstantiationKeyring(Cred, RingId, Auth->Auth, &Dest);
    if (Error == 0)
    {
        if (Reject)
            Error = LxKeyRejectAndLink(Target, Timeout, RejectError, Dest, Auth);
        else
            Error = LxKeyInstantiateAndLink(Target, Payload, Length, Dest, Auth);
        LxKeyPut(Dest);
        if (Error == 0)
            LxKeyInstall(Cred, &Cred->Thread->RequestKeyAuth, NULL);
    }
    LxKeyPut(Target);
    LxKeyPut(Auth);
    return Error;
}

static
LONG
LxKeyDefaultDestKeyring(
    _Inout_ PLX_KEY_CRED Cred,
    _Out_ PLX_KEY *Dest)
{
    PLX_THREAD Thread = Cred->Thread;
    PLX_KEY Keyring = NULL, Auth = Thread->RequestKeyAuth;
    ULONG Stage;
    LONG Error;

    *Dest = NULL;
    switch (Thread->JitKeyring)
    {
        case LX_KEY_REQKEY_DEFL_DEFAULT:
        case LX_KEY_REQKEY_DEFL_REQUESTOR_KEYRING:
            Stage = 0;
            break;
        case LX_KEY_REQKEY_DEFL_THREAD_KEYRING:
            Stage = 1;
            break;
        case LX_KEY_REQKEY_DEFL_PROCESS_KEYRING:
            Stage = 2;
            break;
        case LX_KEY_REQKEY_DEFL_SESSION_KEYRING:
            Stage = 3;
            break;
        case LX_KEY_REQKEY_DEFL_USER_SESSION_KEYRING:
            Stage = 4;
            break;
        case LX_KEY_REQKEY_DEFL_USER_KEYRING:
            Stage = 5;
            break;
        default:
            return -LX_EINVAL;
    }
    if ((Stage == 0) && (Auth != NULL) && !(Auth->Flags & LX_KEY_F_REVOKED) && (Auth->Auth != NULL) &&
        (Auth->Auth->Dest != NULL))
    {
        *Dest = Auth->Auth->Dest;
        LxKeyGet(*Dest);
        return 0;
    }
    if ((Stage <= 1) && (Thread->ThreadKeyring != NULL))
    {
        Keyring = Thread->ThreadKeyring;
        LxKeyGet(Keyring);
    }
    else if ((Stage <= 2) && (Thread->ProcessKeyring != NULL))
    {
        Keyring = Thread->ProcessKeyring;
        LxKeyGet(Keyring);
    }
    else if ((Stage <= 3) && (Thread->SessionKeyring != NULL))
    {
        Keyring = Thread->SessionKeyring;
        LxKeyGet(Keyring);
    }
    else
    {
        Error = (Stage <= 4) ? LxKeyLookUpUserKeyrings(Cred, NULL, &Keyring) : LxKeyLookUpUserKeyrings(Cred, &Keyring, NULL);
        if (Error != 0)
            return Error;
    }
    Error = LxKeyPermission(Keyring, TRUE, Cred, LX_KEY_NEED_WRITE);
    if (Error != 0)
    {
        LxKeyPut(Keyring);
        return Error;
    }
    *Dest = Keyring;
    return 0;
}

static
VOID
LxKeyCredSnapshot(
    _Out_ PLX_KEY_CRED Snapshot,
    _In_ PLX_KEY_CRED Cred)
{
    *Snapshot = *Cred;
    Snapshot->Thread = NULL;
    Snapshot->Auth = NULL;
    if (Snapshot->ThreadKeyring != NULL)
        LxKeyGet(Snapshot->ThreadKeyring);
    if (Snapshot->ProcessKeyring != NULL)
        LxKeyGet(Snapshot->ProcessKeyring);
    if (Snapshot->SessionKeyring != NULL)
        LxKeyGet(Snapshot->SessionKeyring);
    LxNsReference(Snapshot->UserNs);
}

static
LONG
LxKeyAuthNew(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ PLX_KEY Target,
    _In_opt_ PUCHAR Callout,
    _In_ ULONG CalloutLength,
    _In_ PLX_KEY Dest,
    _Out_ PLX_KEY *Result)
{
    PLX_KEY_AUTH Auth;
    PLX_KEY Key;
    CHAR Description[16];
    LONG Error;

    *Result = NULL;
    if ((Cred->Auth != NULL) && ((Cred->Auth->Flags & LX_KEY_F_REVOKED) || (Cred->Auth->Auth == NULL)))
        return -LX_EKEYREVOKED;
    Auth = LxAlloc(sizeof(*Auth));
    if (Auth == NULL)
        return -LX_ENOMEM;
    RtlZeroMemory(Auth, sizeof(*Auth));
    if (CalloutLength != 0)
    {
        Auth->Callout = LxAlloc(CalloutLength);
        if (Auth->Callout == NULL)
        {
            LxFree(Auth);
            return -LX_ENOMEM;
        }
        RtlCopyMemory(Auth->Callout, Callout, CalloutLength);
    }
    Auth->CalloutLength = CalloutLength;
    if (Cred->Auth != NULL)
    {
        LxKeyCredSnapshot(&Auth->Cred, &Cred->Auth->Auth->Cred);
    }
    else
    {
        LxKeyCredSnapshot(&Auth->Cred, Cred);
    }
    RtlStringCbPrintfA(Description, sizeof(Description), "%lx", (ULONG)Target->Serial);
    Error = LxKeyAlloc(Cred, LxKeyTypeAuth, Description, Cred->Fsuid, Cred->Fsgid,
                       LX_KEY_POS_VIEW | LX_KEY_POS_READ | LX_KEY_POS_SEARCH | LX_KEY_POS_LINK | LX_KEY_USR_VIEW,
                       LX_KEY_A_NOT_IN_QUOTA, &Key);
    if (Error != 0)
    {
        LxKeyCredReleaseRefs(&Auth->Cred);
        LxNsDereference(Auth->Cred.UserNs);
        LxFree(Auth->Callout);
        LxFree(Auth);
        return Error;
    }
    Auth->Target = Target;
    LxKeyGet(Target);
    Auth->Dest = Dest;
    LxKeyGet(Dest);
    Key->Auth = Auth;
    LxKeyMarkInstantiated(Key, LX_KEY_POSITIVE);
    *Result = Key;
    return 0;
}

typedef struct _LX_KEY_UPCALL
{
    PLX_KEY Session;
} LX_KEY_UPCALL, *PLX_KEY_UPCALL;

static
VOID
LxKeyUpcallInit(
    _Inout_ PLX_THREAD Thread,
    _In_opt_ PVOID Context)
{
    PLX_KEY_UPCALL Upcall = Context;

    LxKeyAcquire();
    LxKeySwap(&Thread->SessionKeyring, Upcall->Session);
    LxKeyRelease();
}

static
LONG
LxKeyCallRequestKey(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ PLX_KEY Auth)
{
    PLX_KEY Target = Auth->Auth->Target, UserSession, Session, Keyring;
    LX_KEY_UPCALL Upcall;
    CHAR Name[24], KeyText[16], UidText[16], GidText[16], Rings[3][16];
    PCSTR Argv[8], Envp[2];
    LONG WaitStatus, Error;

    LxKeyCredSync(Cred);
    Error = LxKeyLookUpUserKeyrings(Cred, NULL, &UserSession);
    if (Error != 0)
        return Error;
    RtlStringCbPrintfA(Name, sizeof(Name), "_req.%lu", (ULONG)Target->Serial);
    Error = LxKeyringAlloc(Cred, Name, Cred->Fsuid, Cred->Fsgid, LX_KEY_POS_ALL | LX_KEY_USR_VIEW | LX_KEY_USR_READ,
                           LX_KEY_A_QUOTA_OVERRUN, NULL, &Keyring);
    if (Error == 0)
    {
        Error = LxKeyLink(Keyring, Auth);
        if (Error != 0)
            LxKeyPut(Keyring);
    }
    if (Error != 0)
    {
        LxKeyPut(UserSession);
        return Error;
    }

    Session = (Cred->SessionKeyring != NULL) ? Cred->SessionKeyring : UserSession;
    RtlStringCbPrintfA(UidText, sizeof(UidText), "%lu", Cred->Fsuid);
    RtlStringCbPrintfA(GidText, sizeof(GidText), "%lu", Cred->Fsgid);
    RtlStringCbPrintfA(KeyText, sizeof(KeyText), "%ld", Target->Serial);
    RtlStringCbPrintfA(Rings[0], sizeof(Rings[0]), "%ld", Cred->ThreadKeyring ? Cred->ThreadKeyring->Serial : 0);
    RtlStringCbPrintfA(Rings[1], sizeof(Rings[1]), "%ld", Cred->ProcessKeyring ? Cred->ProcessKeyring->Serial : 0);
    RtlStringCbPrintfA(Rings[2], sizeof(Rings[2]), "%ld", Session->Serial);
    Argv[0] = "/sbin/request-key";
    Argv[1] = "create";
    Argv[2] = KeyText;
    Argv[3] = UidText;
    Argv[4] = GidText;
    Argv[5] = Rings[0];
    Argv[6] = Rings[1];
    Argv[7] = Rings[2];
    Envp[0] = "HOME=/";
    Envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
    Upcall.Session = Keyring;

    LxKeyGet(Target);
    ExReleaseFastMutex(&LxKeyLock);
    Error = LxUserModeHelper(Cred->Instance, Argv[0], Argv, RTL_NUMBER_OF(Argv), Envp, RTL_NUMBER_OF(Envp),
                             LxKeyUpcallInit, &Upcall, &WaitStatus);
    ExAcquireFastMutex(&LxKeyLock);
    if ((Error >= 0) && ((Target->Flags & LX_KEY_F_CONSTRUCT) || (LxKeyValidate(Target) < 0)))
        Error = -LX_ENOKEY;
    LxKeyPut(Target);
    LxKeyPut(Keyring);
    LxKeyPut(UserSession);
    return (Error >= 0) ? 0 : Error;
}

static
LONG
LxKeyConstruct(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ LX_KEY_TYPE_ID Type,
    _In_ PCSTR Description,
    _In_ PUCHAR Callout,
    _In_ ULONG CalloutLength,
    _In_opt_ PLX_KEY Requested,
    _Out_ PLX_KEY *Result)
{
    PLX_KEY Dest, Key, Auth;
    BOOLEAN Replace;
    LONG Error;

    *Result = NULL;
    if (Type == LxKeyTypeKeyring)
        return -LX_EPERM;
    if (Requested != NULL)
    {
        Dest = Requested;
        LxKeyGet(Dest);
    }
    else
    {
        Error = LxKeyDefaultDestKeyring(Cred, &Dest);
        if (Error != 0)
            return Error;
    }

    Error = LxKeyLinkCheck(Dest, Type, Description, &Replace);
    if ((Error == 0) && Dest->Restricted)
        Error = -LX_EPERM;
    if (Error == 0)
        Error = LxKeyAlloc(Cred, Type, Description, Cred->Fsuid, Cred->Fsgid, LxKeyDefaultPerm(Type), LX_KEY_A_IN_QUOTA, &Key);
    if (Error == 0)
    {
        Key->Flags |= LX_KEY_F_CONSTRUCT;
        KeClearEvent(&Key->Constructed);
        Error = LxKeyLinkCommit(Dest, Key);
        if (Error != 0)
            LxKeyPut(Key);
    }
    if (Error != 0)
    {
        LxKeyPut(Dest);
        return Error;
    }

    Error = LxKeyAuthNew(Cred, Key, Callout, CalloutLength, Dest, &Auth);
    if (Error == 0)
    {
        Error = LxKeyCallRequestKey(Cred, Auth);
        if (Error < 0)
            LxKeyRejectAndLink(Key, LX_KEY_NEGATIVE_TIMEOUT, LX_ENOKEY, NULL, Auth);
        else
            LxKeyRevoke(Auth);
        LxKeyAuthFree(Auth);
        LxKeyPut(Auth);
    }
    else
    {
        LxKeyRejectAndLink(Key, LX_KEY_NEGATIVE_TIMEOUT, LX_ENOKEY, NULL, NULL);
    }
    LxKeyPut(Dest);
    if (Error < 0)
    {
        LxKeyPut(Key);
        return Error;
    }
    *Result = Key;
    return 0;
}

static
LONG64
LxKeyRequest(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ PCSTR TypeName,
    _In_ PCSTR Description,
    _In_opt_ PUCHAR Callout,
    _In_ ULONG CalloutLength,
    _In_ LONG DestId)
{
    PLX_KEY Dest = NULL, Key = NULL, Found;
    LX_KEY_SEARCH Search;
    LX_KEY_TYPE_ID Type;
    LONG Error, Serial;

    if (DestId != 0)
    {
        Error = LxKeyLookup(Cred, DestId, LX_KL_CREATE, LX_KEY_NEED_WRITE, &Dest, NULL);
        if (Error != 0)
            return Error;
    }
    Error = LxKeyTypeLookup(TypeName, &Type);
    if (Error != 0)
    {
        LxKeyPut(Dest);
        return Error;
    }

    RtlZeroMemory(&Search, sizeof(Search));
    Search.Cred = Cred;
    Search.Type = Type;
    Search.Description = Description;
    Search.Flags = LX_KS_STATE_CHECK | LX_KS_SKIP_EXPIRED | LX_KS_RECURSE;
    LxKeyCredSync(Cred);
    Error = LxKeySearchProcessKeyrings(&Search, &Found);
    if (Error == 0)
    {
        Key = Found;
        LxKeyGet(Key);
        if (Dest != NULL)
        {
            Error = LxKeyPermission(Key, TRUE, Cred, LX_KEY_NEED_LINK);
            if (Error == 0)
                Error = LxKeyLink(Dest, Key);
        }
    }
    else if (Error == -LX_EAGAIN)
    {
        Error = -LX_ENOKEY;
        if (Callout != NULL)
            Error = LxKeyConstruct(Cred, Type, Description, Callout, CalloutLength, Dest, &Key);
    }
    LxKeyPut(Dest);
    if (Error != 0)
    {
        LxKeyPut(Key);
        return Error;
    }
    Error = LxKeyWaitConstruction(Key);
    Serial = Key->Serial;
    LxKeyPut(Key);
    return (Error != 0) ? Error : Serial;
}

LX_SYSCALL(LxSysAddKey)
{
    PSTR Type = NULL, Description = NULL;
    PUCHAR Payload = NULL;
    ULONG64 Length = Call->Args[3];
    PLX_KEY Keyring;
    BOOLEAN Possessed;
    LX_KEY_CRED Cred;
    LONG64 Result;

    if (Length > LX_KEY_MAX_PAYLOAD)
        return -LX_EINVAL;
    Result = LxKeyCopyType(Call->Args[0], &Type);
    if ((Result == 0) && (Call->Args[1] != 0))
    {
        Result = LxKeyCopyString(Call->Args[1], LX_KEY_MAX_DESC, &Description);
        if ((Result == 0) && (Description[0] == '\0'))
        {
            LxFree(Description);
            Description = NULL;
        }
        else if ((Result == 0) && (Description[0] == '.') && (strncmp(Type, "keyring", 7) == 0))
        {
            Result = -LX_EPERM;
        }
    }
    if (Result == 0)
        Result = LxKeyCopyPayload(Call->Args[2], Length, &Payload);
    if (Result == 0)
    {
        LxKeyCredLoad(&Cred, Call->Thread);
        LxKeyAcquire();
        LxKeyCollect();
        Result = LxKeyLookup(&Cred, (LONG)Call->Args[4], LX_KL_CREATE, LX_KEY_NEED_WRITE, &Keyring, &Possessed);
        if (Result == 0)
        {
            Result = LxKeyCreateOrUpdate(&Cred, Keyring, Possessed, Type, Description, Payload, Length);
            LxKeyPut(Keyring);
        }
        LxKeyRelease();
    }
    LxKeyFreePayload(Payload, Length);
    LxFree(Description);
    LxFree(Type);
    return Result;
}

LX_SYSCALL(LxSysRequestKey)
{
    PSTR Type = NULL, Description = NULL, Callout = NULL;
    LX_KEY_CRED Cred;
    LONG64 Result;

    Result = LxKeyCopyType(Call->Args[0], &Type);
    if (Result == 0)
        Result = LxKeyCopyString(Call->Args[1], LX_KEY_MAX_DESC, &Description);
    if ((Result == 0) && (Call->Args[2] != 0))
        Result = LxKeyCopyString(Call->Args[2], LX_KEY_PAGE, &Callout);
    if (Result == 0)
    {
        LxKeyCredLoad(&Cred, Call->Thread);
        LxKeyAcquire();
        LxKeyCollect();
        Result = LxKeyRequest(&Cred, Type, Description, (PUCHAR)Callout, Callout ? (ULONG)strlen(Callout) : 0,
                              (LONG)Call->Args[3]);
        LxKeyRelease();
    }
    LxFree(Callout);
    LxFree(Description);
    LxFree(Type);
    return Result;
}

static
LONG64
LxKeyCopyOut(
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length,
    _In_ LONG64 Result,
    _In_opt_ const VOID *Data)
{
    if ((Result > 0) && (Data != NULL) && (Buffer != 0) && ((ULONG64)Result <= Length) &&
        (LxCopyToUser(Buffer, Data, (SIZE_T)Result) != 0))
    {
        return -LX_EFAULT;
    }
    return Result;
}

static
LONG64
LxKeyGatherIov(
    _In_ ULONG64 Vectors,
    _In_ ULONG Count,
    _Out_ PUCHAR *Payload,
    _Out_ PULONG64 Total)
{
    LX_IOVEC Vector;
    ULONG64 Length = 0, Offset = 0;
    ULONG Index;

    *Payload = NULL;
    *Total = 0;
    if (Count > LX_KEY_UIO_MAXIOV)
        return -LX_EINVAL;
    for (Index = 0; Index < Count; Index++)
    {
        if (LxCopyFromUser(&Vector, Vectors + Index * sizeof(Vector), sizeof(Vector)) != 0)
            return -LX_EFAULT;
        if ((LONG64)Vector.Length < 0)
            return -LX_EINVAL;
        Length += Vector.Length;
        if (Length > MAXLONG)
            return -LX_EINVAL;
    }
    if (Length > LX_KEY_MAX_PAYLOAD)
        return -LX_EINVAL;
    if (Length == 0)
        return 0;
    *Payload = LxAllocPaged((SIZE_T)Length);
    if (*Payload == NULL)
        return -LX_ENOMEM;
    for (Index = 0; Index < Count; Index++)
    {
        LxCopyFromUser(&Vector, Vectors + Index * sizeof(Vector), sizeof(Vector));
        if ((Vector.Length != 0) && (LxCopyFromUser(*Payload + Offset, Vector.Base, (SIZE_T)Vector.Length) != 0))
        {
            LxKeyFreePayload(*Payload, Length);
            *Payload = NULL;
            return -LX_EFAULT;
        }
        Offset += Vector.Length;
    }
    *Total = Length;
    return 0;
}

static
LONG64
LxKeyctlCapabilities(
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length)
{
    /* TODO: KEYCTL_CAPS0_DIFFIE_HELLMAN, KEYCTL_CAPS0_PUBLIC_KEY and KEYCTL_CAPS1_NOTIFICATIONS stay clear until KEYCTL_DH_COMPUTE, the asymmetric key type and key watches exist. */
    static const UCHAR Capabilities[2] =
    {
        LX_KEYCTL_CAPS0_CAPABILITIES | LX_KEYCTL_CAPS0_PERSISTENT_KEYRINGS | LX_KEYCTL_CAPS0_BIG_KEY |
            LX_KEYCTL_CAPS0_INVALIDATE | LX_KEYCTL_CAPS0_RESTRICT_KEYRING | LX_KEYCTL_CAPS0_MOVE,
        LX_KEYCTL_CAPS1_NS_KEYRING_NAME | LX_KEYCTL_CAPS1_NS_KEY_TAG
    };
    ULONG64 Size = min(Length, sizeof(Capabilities));

    if (Length != 0)
    {
        if (LxCopyToUser(Buffer, Capabilities, (SIZE_T)Size) != 0)
            return -LX_EFAULT;
        if ((Size < Length) && (LxZeroUser(Buffer + Size, (SIZE_T)(Length - Size)) != 0))
            return -LX_EFAULT;
    }
    return sizeof(Capabilities);
}

static
LONG64
LxKeyctlTextOut(
    _In_ LONG64 Result,
    _In_ PLX_KEY_TEXT Text,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Length)
{
    if ((Result > 0) && (Buffer != 0) && ((ULONG64)Result <= Length) &&
        (LxCopyToUser(Buffer, Text->Buffer, (SIZE_T)Result) != 0))
    {
        Result = -LX_EFAULT;
    }
    LxFree(Text->Buffer);
    return Result;
}

static
LONG64
LxKeyctlLocked(
    _Inout_ PLX_KEY_CRED Cred,
    _In_ PLX_CALL Call,
    _In_ ULONG Operation)
{
    LONG Id = (LONG)Call->Args[1];

    switch (Operation)
    {
        case LX_KEYCTL_GET_KEYRING_ID:
            return LxKeyctlGetKeyringId(Cred, Id, (LONG)Call->Args[2]);
        case LX_KEYCTL_REVOKE:
            return LxKeyctlRevoke(Cred, Id);
        case LX_KEYCTL_CHOWN:
            return LxKeyctlChown(Cred, Id, (ULONG)Call->Args[2], (ULONG)Call->Args[3]);
        case LX_KEYCTL_SETPERM:
            return LxKeyctlSetperm(Cred, Id, (ULONG)Call->Args[2]);
        case LX_KEYCTL_CLEAR:
            return LxKeyctlClear(Cred, Id);
        case LX_KEYCTL_LINK:
            return LxKeyctlLink(Cred, Id, (LONG)Call->Args[2]);
        case LX_KEYCTL_UNLINK:
            return LxKeyctlUnlink(Cred, Id, (LONG)Call->Args[2]);
        case LX_KEYCTL_SET_REQKEY_KEYRING:
            return LxKeyctlSetReqkeyKeyring(Cred, (LONG)Call->Args[1]);
        case LX_KEYCTL_SET_TIMEOUT:
            return LxKeyctlSetTimeout(Cred, Id, (ULONG)Call->Args[2]);
        case LX_KEYCTL_ASSUME_AUTHORITY:
            return LxKeyctlAssumeAuthority(Cred, Id);
        case LX_KEYCTL_SESSION_TO_PARENT:
            return LxKeyctlSessionToParent(Cred);
        case LX_KEYCTL_INVALIDATE:
            return LxKeyctlInvalidate(Cred, Id);
        case LX_KEYCTL_GET_PERSISTENT:
            return LxKeyctlGetPersistent(Cred, (ULONG)Call->Args[1], (LONG)Call->Args[2]);
        case LX_KEYCTL_MOVE:
            return LxKeyctlMove(Cred, Id, (LONG)Call->Args[2], (LONG)Call->Args[3], (ULONG)Call->Args[4]);
        default:
            return -LX_EOPNOTSUPP;
    }
}

LX_SYSCALL(LxSysKeyctl)
{
    ULONG Operation = (ULONG)Call->Args[0];
    LONG Id = (LONG)Call->Args[1];
    PSTR Type = NULL, Description = NULL;
    PUCHAR Payload = NULL, Copy = NULL;
    ULONG64 Length = 0;
    LX_KEY_TEXT Text;
    LX_KEY_CRED Cred;
    LONG64 Result = 0;
    LONG Error;

    RtlZeroMemory(&Text, sizeof(Text));
    LxKeyCredLoad(&Cred, Call->Thread);
    switch (Operation)
    {
        case LX_KEYCTL_JOIN_SESSION_KEYRING:
            if (Call->Args[1] != 0)
                Result = LxKeyCopyString(Call->Args[1], LX_KEY_MAX_DESC, &Description);
            if (Result != 0)
                return Result;
            LxKeyAcquire();
            LxKeyCollect();
            LxKeyCredSync(&Cred);
            Result = LxKeyJoinSession(&Cred, Description);
            LxKeyRelease();
            LxFree(Description);
            return Result;

        case LX_KEYCTL_UPDATE:
            Length = Call->Args[3];
            if (Length > LX_KEY_PAGE)
                return -LX_EINVAL;
            Result = LxKeyCopyPayload(Call->Args[2], Length, &Payload);
            if (Result != 0)
                return Result;
            LxKeyAcquire();
            LxKeyCollect();
            Result = LxKeyctlUpdate(&Cred, Id, Payload, Length);
            LxKeyRelease();
            LxKeyFreePayload(Payload, Length);
            return Result;

        case LX_KEYCTL_DESCRIBE:
            LxKeyAcquire();
            LxKeyCollect();
            Result = LxKeyctlDescribe(&Cred, Id, &Text);
            LxKeyRelease();
            return LxKeyctlTextOut(Result, &Text, Call->Args[2], Call->Args[3]);

        case LX_KEYCTL_GET_SECURITY:
            LxKeyAcquire();
            LxKeyCollect();
            Result = LxKeyctlGetSecurity(&Cred, Id);
            LxKeyRelease();
            if ((Result > 0) && (Call->Args[2] != 0) && (Call->Args[3] != 0) && (LxCopyToUser(Call->Args[2], "", 1) != 0))
                return -LX_EFAULT;
            return Result;

        case LX_KEYCTL_SEARCH:
            Result = LxKeyCopyType(Call->Args[2], &Type);
            if (Result == 0)
                Result = LxKeyCopyString(Call->Args[3], LX_KEY_MAX_DESC, &Description);
            if (Result == 0)
            {
                LxKeyAcquire();
                LxKeyCollect();
                Result = LxKeyctlSearch(&Cred, Id, Type, Description, (LONG)Call->Args[4]);
                LxKeyRelease();
            }
            LxFree(Description);
            LxFree(Type);
            return Result;

        case LX_KEYCTL_READ:
            LxKeyAcquire();
            LxKeyCollect();
            Result = LxKeyctlRead(&Cred, Id, (Call->Args[2] != 0) ? Call->Args[3] : 0, &Copy);
            LxKeyRelease();
            Length = (Result > 0) ? (ULONG64)Result : 0;
            Result = LxKeyCopyOut(Call->Args[2], Call->Args[3], Result, Copy);
            LxKeyFreePayload(Copy, Length);
            return Result;

        case LX_KEYCTL_INSTANTIATE:
        case LX_KEYCTL_INSTANTIATE_IOV:
            if (Operation == LX_KEYCTL_INSTANTIATE)
            {
                Length = (Call->Args[2] != 0) ? Call->Args[3] : 0;
                if (Length > LX_KEY_MAX_PAYLOAD)
                    return -LX_EINVAL;
            }
            LxKeyAcquire();
            Result = LxKeyAuthorityCheck(Call->Thread, Id);
            LxKeyRelease();
            if (Result != 0)
                return Result;
            if (Operation == LX_KEYCTL_INSTANTIATE)
            {
                Result = LxKeyCopyPayload(Call->Args[2], Length, &Payload);
            }
            else
            {
                Result = LxKeyGatherIov(Call->Args[2], (Call->Args[2] != 0) ? (ULONG)Call->Args[3] : 0, &Payload, &Length);
            }
            if (Result != 0)
                return Result;
            LxKeyAcquire();
            LxKeyCollect();
            Result = LxKeyctlInstantiate(&Cred, Id, Payload, Length, (LONG)Call->Args[4], FALSE, 0, 0);
            LxKeyRelease();
            LxKeyFreePayload(Payload, Length);
            return Result;

        case LX_KEYCTL_NEGATE:
        case LX_KEYCTL_REJECT:
            Error = (Operation == LX_KEYCTL_NEGATE) ? LX_ENOKEY : (LONG)(ULONG)Call->Args[3];
            if ((Error <= 0) || (Error >= 4095) || (Error == LX_ERESTARTSYS) || (Error == LX_ERESTARTNOINTR) ||
                (Error == LX_ERESTARTNOHAND) || (Error == 516))
            {
                return -LX_EINVAL;
            }
            LxKeyAcquire();
            LxKeyCollect();
            Result = LxKeyctlInstantiate(&Cred, Id, NULL, 0, (LONG)Call->Args[(Operation == LX_KEYCTL_NEGATE) ? 3 : 4], TRUE,
                                         (ULONG)Call->Args[2], Error);
            LxKeyRelease();
            return Result;

        case LX_KEYCTL_RESTRICT_KEYRING:
            Error = 0;
            if ((Call->Args[2] != 0) && (Call->Args[3] != 0))
            {
                Error = LxKeyCopyType(Call->Args[2], &Type);
                if (Error == 0)
                    Error = LxKeyCopyString(Call->Args[3], LX_KEY_PAGE, &Description);
            }
            LxKeyAcquire();
            LxKeyCollect();
            Result = LxKeyctlRestrict(&Cred, Id, Call->Args[2] != 0, Call->Args[3] != 0, Error, Type);
            LxKeyRelease();
            LxFree(Description);
            LxFree(Type);
            return Result;

        case LX_KEYCTL_CAPABILITIES:
            return LxKeyctlCapabilities(Call->Args[1], Call->Args[2]);

        /* TODO: KEYCTL_DH_COMPUTE and the KEYCTL_PKEY_* operations need the Diffie-Hellman and asymmetric key support; they return EOPNOTSUPP as on kernels built without them. */
        case LX_KEYCTL_DH_COMPUTE:
        case LX_KEYCTL_PKEY_QUERY:
        case LX_KEYCTL_PKEY_ENCRYPT:
        case LX_KEYCTL_PKEY_DECRYPT:
        case LX_KEYCTL_PKEY_SIGN:
        case LX_KEYCTL_PKEY_VERIFY:
        /* TODO: KEYCTL_WATCH_KEY needs notification pipes (O_NOTIFICATION_PIPE); it returns EOPNOTSUPP as without CONFIG_KEY_NOTIFICATIONS. */
        case LX_KEYCTL_WATCH_KEY:
            return -LX_EOPNOTSUPP;

        default:
            LxKeyAcquire();
            LxKeyCollect();
            Result = LxKeyctlLocked(&Cred, Call, Operation);
            LxKeyRelease();
            return Result;
    }
}

LONG
LxKeyThreadInherit(
    _Inout_ PLX_THREAD Thread,
    _In_ PLX_THREAD Parent,
    _In_ ULONG Mode)
{
    LX_KEY_CRED Cred;
    LONG Error = 0;

    LxKeyCredLoad(&Cred, Thread);
    LxKeyAcquire();
    LxKeySwap(&Thread->SessionKeyring, Parent->SessionKeyring);
    LxKeySwap(&Thread->RequestKeyAuth, Parent->RequestKeyAuth);
    Thread->JitKeyring = Parent->JitKeyring;
    if (Mode != LX_KEY_INHERIT_PROCESS)
        LxKeySwap(&Thread->ProcessKeyring, Parent->ProcessKeyring);
    if (Mode == LX_KEY_INHERIT_TASK)
        LxKeySwap(&Thread->ThreadKeyring, Parent->ThreadKeyring);
    else if ((Mode == LX_KEY_INHERIT_THREAD) && (Parent->ThreadKeyring != NULL))
        Error = LxKeyInstallThreadKeyring(&Cred);
    LxKeyRelease();
    return Error;
}

VOID
LxKeyThreadRelease(
    _Inout_ PLX_THREAD Thread)
{
    LxKeyAcquire();
    LxKeySwap(&Thread->ThreadKeyring, NULL);
    LxKeySwap(&Thread->ProcessKeyring, NULL);
    LxKeySwap(&Thread->SessionKeyring, NULL);
    LxKeySwap(&Thread->RequestKeyAuth, NULL);
    LxKeyRelease();
}

VOID
LxKeyExec(
    _Inout_ PLX_THREAD Thread)
{
    LxKeyAcquire();
    LxKeySwap(&Thread->ThreadKeyring, NULL);
    LxKeySwap(&Thread->ProcessKeyring, NULL);
    LxKeyRelease();
}

VOID
LxKeyUserNsRelease(
    _Inout_ PLX_NS UserNs)
{
    LxKeyAcquire();
    LxKeySwap(&UserNs->User.KeyRegister, NULL);
    LxKeySwap(&UserNs->User.PersistentRegister, NULL);
    LxKeyRelease();
}

PSTR
LxKeyFormatProcKeys(
    _In_ PLX_THREAD Reader,
    _Out_ PULONG Length)
{
    LX_KEY_TEXT Text;
    LX_KEY_CRED Cred;
    PLX_KEY Key;
    LONG64 Now;
    LONG Serial = 0;

    RtlZeroMemory(&Text, sizeof(Text));
    LxKeyCredLoad(&Cred, Reader);
    LxKeyAcquire();
    LxKeyCollect();
    LxKeyCredSync(&Cred);
    Now = LxKeyNow();
    for (Key = LxKeyNextSerial(0); Key != NULL; Key = LxKeyNextSerial(Serial))
    {
        BOOLEAN Possessed = FALSE;
        CHAR Expiry[16];
        LONG64 Left;

        Serial = Key->Serial;
        if ((Key->Usage == 0) || (Key->Instance != Cred.Instance) ||
            (LxIdFromKernel(Cred.UserNs, FALSE, Key->User->Uid) == MAXULONG))
        {
            continue;
        }
        if (Key->Perm & LX_KEY_POS_VIEW)
            Possessed = LxKeyPossessed(&Cred, Key, FALSE);
        if (LxKeyPermission(Key, Possessed, &Cred, LX_KEY_NEED_VIEW) != 0)
            continue;
        Left = Key->Expiry - Now;
        if (Key->Expiry == LX_KEY_PERMANENT)
            RtlStringCbCopyA(Expiry, sizeof(Expiry), "perm");
        else if (Now >= Key->Expiry)
            RtlStringCbCopyA(Expiry, sizeof(Expiry), "expd");
        else if (Left < 60)
            RtlStringCbPrintfA(Expiry, sizeof(Expiry), "%llus", Left);
        else if (Left < 60 * 60)
            RtlStringCbPrintfA(Expiry, sizeof(Expiry), "%llum", Left / 60);
        else if (Left < 60 * 60 * 24)
            RtlStringCbPrintfA(Expiry, sizeof(Expiry), "%lluh", Left / (60 * 60));
        else if (Left < 60 * 60 * 24 * 7)
            RtlStringCbPrintfA(Expiry, sizeof(Expiry), "%llud", Left / (60 * 60 * 24));
        else
            RtlStringCbPrintfA(Expiry, sizeof(Expiry), "%lluw", Left / (60 * 60 * 24 * 7));
        LxKeyTextAppend(&Text, "%08lx %c%c%c%c%c%c%c %5ld %4s %08lx %5ld %5ld %-9.9s ", Key->Serial,
                        (Key->State != LX_KEY_UNINSTANTIATED) ? 'I' : '-', (Key->Flags & LX_KEY_F_REVOKED) ? 'R' : '-',
                        '-', (Key->Flags & LX_KEY_F_IN_QUOTA) ? 'Q' : '-', (Key->Flags & LX_KEY_F_CONSTRUCT) ? 'U' : '-',
                        (Key->State < 0) ? 'N' : '-', (Key->Flags & LX_KEY_F_INVALIDATED) ? 'i' : '-', Key->Usage, Expiry,
                        Key->Perm, (LONG)LxKeyViewUid(Cred.UserNs, Key->Uid), (LONG)LxKeyViewGid(Cred.UserNs, Key->Gid),
                        LxKeyTypes[Key->Type].Name);
        LxKeyDescribeType(&Text, Key);
        LxKeyTextAppend(&Text, "\n");
    }
    LxKeyRelease();
    *Length = Text.Length;
    if (Text.Failed)
    {
        LxFree(Text.Buffer);
        return NULL;
    }
    return (Text.Buffer != NULL) ? Text.Buffer : LxStrDup("");
}

PSTR
LxKeyFormatProcKeyUsers(
    _In_ PLX_PROCESS Reader,
    _Out_ PULONG Length)
{
    PLX_NS UserNs = LxCredUserNs(Reader);
    LX_KEY_TEXT Text;
    PLIST_ENTRY Entry;

    RtlZeroMemory(&Text, sizeof(Text));
    LxKeyAcquire();
    for (Entry = LxKeyUsers.Flink; Entry != &LxKeyUsers; Entry = Entry->Flink)
    {
        PLX_KEY_USER User = CONTAINING_RECORD(Entry, LX_KEY_USER, Link);

        if ((User->Instance != Reader->Instance) || (LxIdFromKernel(UserNs, FALSE, User->Uid) == MAXULONG))
            continue;
        LxKeyTextAppend(&Text, "%5lu: %5ld %ld/%ld %lu/%lu %lu/%lu\n", LxIdFromKernelMunged(UserNs, FALSE, User->Uid),
                        User->NKeys, User->NKeys, User->NIKeys, User->QnKeys, LxKeyQuotaKeys(User->Uid), User->QnBytes,
                        LxKeyQuotaBytes(User->Uid));
    }
    LxKeyRelease();
    *Length = Text.Length;
    if (Text.Failed)
    {
        LxFree(Text.Buffer);
        return NULL;
    }
    return (Text.Buffer != NULL) ? Text.Buffer : LxStrDup("");
}

static
PLONG
LxKeySysctl(
    _In_ PCSTR Name)
{
    if (strcmp(Name, "kernel/keys/gc_delay") == 0)
        return &LxKeyGcDelay;
    if (strcmp(Name, "kernel/keys/maxbytes") == 0)
        return &LxKeyMaxBytes;
    if (strcmp(Name, "kernel/keys/maxkeys") == 0)
        return &LxKeyMaxKeys;
    if (strcmp(Name, "kernel/keys/persistent_keyring_expiry") == 0)
        return &LxKeyPersistentExpiry;
    if (strcmp(Name, "kernel/keys/root_maxbytes") == 0)
        return &LxKeyRootMaxBytes;
    if (strcmp(Name, "kernel/keys/root_maxkeys") == 0)
        return &LxKeyRootMaxKeys;
    return NULL;
}

BOOLEAN
LxKeySysctlGet(
    _In_ PCSTR Name,
    _Out_ PLONG Value)
{
    PLONG Variable = LxKeySysctl(Name);

    *Value = 0;
    if (Variable == NULL)
        return FALSE;
    LxKeyAcquire();
    *Value = *Variable;
    LxKeyRelease();
    return TRUE;
}

LONG
LxKeySysctlSet(
    _In_ PCSTR Name,
    _In_ LONG64 Value)
{
    PLONG Variable = LxKeySysctl(Name);

    if (Variable == NULL)
        return -LX_ENOENT;
    if ((Value < 0) || (Value > MAXLONG))
        return -LX_EINVAL;
    LxKeyAcquire();
    *Variable = (LONG)Value;
    LxKeyRelease();
    return 0;
}

VOID
LxKeyInitialize(VOID)
{
    ExInitializeFastMutex(&LxKeyLock);
    RtlZeroMemory(&LxKeyTree, sizeof(LxKeyTree));
    InitializeListHead(&LxKeyList);
    InitializeListHead(&LxKeyNames);
    InitializeListHead(&LxKeyDead);
    InitializeListHead(&LxKeyUsers);
}
