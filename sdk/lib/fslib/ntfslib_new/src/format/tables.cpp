/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS volume formatter: $UpCase, $AttrDef and default ACL
 */

#include "formatint.h"

typedef struct FormatUpCaseRun
{
    USHORT First;
    USHORT Last;
    LONG Delta;
    USHORT Step;
} FormatUpCaseRun;

static const FormatUpCaseRun FormatUpCaseRuns[] =
{
    { 0x0061, 0x007A, -32, 1 }, { 0x00E0, 0x00F6, -32, 1 },
    { 0x00F8, 0x00FE, -32, 1 }, { 0x00FF, 0x00FF, 121, 1 },
    { 0x0101, 0x012F, -1, 2 }, { 0x0133, 0x0137, -1, 2 },
    { 0x013A, 0x0148, -1, 2 }, { 0x014B, 0x0177, -1, 2 },
    { 0x017A, 0x017E, -1, 2 }, { 0x0180, 0x0180, 195, 1 },
    { 0x0183, 0x0185, -1, 2 }, { 0x0188, 0x0188, -1, 1 },
    { 0x018C, 0x018C, -1, 1 }, { 0x0192, 0x0192, -1, 1 },
    { 0x0195, 0x0195, 97, 1 }, { 0x0199, 0x0199, -1, 1 },
    { 0x019A, 0x019A, 163, 1 }, { 0x019E, 0x019E, 130, 1 },
    { 0x01A1, 0x01A5, -1, 2 }, { 0x01A8, 0x01A8, -1, 1 },
    { 0x01AD, 0x01AD, -1, 1 }, { 0x01B0, 0x01B0, -1, 1 },
    { 0x01B4, 0x01B6, -1, 2 }, { 0x01B9, 0x01B9, -1, 1 },
    { 0x01BD, 0x01BD, -1, 1 }, { 0x01BF, 0x01BF, 56, 1 },
    { 0x01C6, 0x01C6, -2, 1 }, { 0x01C9, 0x01C9, -2, 1 },
    { 0x01CC, 0x01CC, -2, 1 }, { 0x01CE, 0x01DC, -1, 2 },
    { 0x01DD, 0x01DD, -79, 1 }, { 0x01DF, 0x01EF, -1, 2 },
    { 0x01F3, 0x01F3, -2, 1 }, { 0x01F5, 0x01F5, -1, 1 },
    { 0x01F9, 0x021F, -1, 2 }, { 0x0223, 0x0233, -1, 2 },
    { 0x023C, 0x023C, -1, 1 }, { 0x0242, 0x0242, -1, 1 },
    { 0x0247, 0x024F, -1, 2 }, { 0x0250, 0x0250, 10783, 1 },
    { 0x0251, 0x0251, 10780, 1 }, { 0x0253, 0x0253, -210, 1 },
    { 0x0254, 0x0254, -206, 1 }, { 0x0256, 0x0257, -205, 1 },
    { 0x0259, 0x0259, -202, 1 }, { 0x025B, 0x025B, -203, 1 },
    { 0x0260, 0x0260, -205, 1 }, { 0x0263, 0x0263, -207, 1 },
    { 0x0268, 0x0268, -209, 1 }, { 0x0269, 0x0269, -211, 1 },
    { 0x026B, 0x026B, 10743, 1 }, { 0x026F, 0x026F, -211, 1 },
    { 0x0271, 0x0271, 10749, 1 }, { 0x0272, 0x0272, -213, 1 },
    { 0x0275, 0x0275, -214, 1 }, { 0x027D, 0x027D, 10727, 1 },
    { 0x0280, 0x0280, -218, 1 }, { 0x0283, 0x0283, -218, 1 },
    { 0x0288, 0x0288, -218, 1 }, { 0x0289, 0x0289, -69, 1 },
    { 0x028A, 0x028B, -217, 1 }, { 0x028C, 0x028C, -71, 1 },
    { 0x0292, 0x0292, -219, 1 }, { 0x0371, 0x0373, -1, 2 },
    { 0x0377, 0x0377, -1, 1 }, { 0x037B, 0x037D, 130, 1 },
    { 0x03AC, 0x03AC, -38, 1 }, { 0x03AD, 0x03AF, -37, 1 },
    { 0x03B1, 0x03C1, -32, 1 }, { 0x03C3, 0x03CB, -32, 1 },
    { 0x03CC, 0x03CC, -64, 1 }, { 0x03CD, 0x03CE, -63, 1 },
    { 0x03D7, 0x03D7, -8, 1 }, { 0x03D9, 0x03EF, -1, 2 },
    { 0x03F2, 0x03F2, 7, 1 }, { 0x03F8, 0x03F8, -1, 1 },
    { 0x03FB, 0x03FB, -1, 1 }, { 0x0430, 0x044F, -32, 1 },
    { 0x0450, 0x045F, -80, 1 }, { 0x0461, 0x0481, -1, 2 },
    { 0x048B, 0x04BF, -1, 2 }, { 0x04C2, 0x04CE, -1, 2 },
    { 0x04CF, 0x04CF, -15, 1 }, { 0x04D1, 0x0523, -1, 2 },
    { 0x0561, 0x0586, -48, 1 }, { 0x1D79, 0x1D79, 35332, 1 },
    { 0x1D7D, 0x1D7D, 3814, 1 }, { 0x1E01, 0x1E95, -1, 2 },
    { 0x1EA1, 0x1EFF, -1, 2 }, { 0x1F00, 0x1F07, 8, 1 },
    { 0x1F10, 0x1F15, 8, 1 }, { 0x1F20, 0x1F27, 8, 1 },
    { 0x1F30, 0x1F37, 8, 1 }, { 0x1F40, 0x1F45, 8, 1 },
    { 0x1F51, 0x1F57, 8, 2 }, { 0x1F60, 0x1F67, 8, 1 },
    { 0x1F70, 0x1F71, 74, 1 }, { 0x1F72, 0x1F75, 86, 1 },
    { 0x1F76, 0x1F77, 100, 1 }, { 0x1F78, 0x1F79, 128, 1 },
    { 0x1F7A, 0x1F7B, 112, 1 }, { 0x1F7C, 0x1F7D, 126, 1 },
    { 0x1F80, 0x1F87, 8, 1 }, { 0x1F90, 0x1F97, 8, 1 },
    { 0x1FA0, 0x1FA7, 8, 1 }, { 0x1FB0, 0x1FB1, 8, 1 },
    { 0x1FB3, 0x1FB3, 9, 1 }, { 0x1FC3, 0x1FC3, 9, 1 },
    { 0x1FD0, 0x1FD1, 8, 1 }, { 0x1FE0, 0x1FE1, 8, 1 },
    { 0x1FE5, 0x1FE5, 7, 1 }, { 0x1FF3, 0x1FF3, 9, 1 },
    { 0x214E, 0x214E, -28, 1 }, { 0x2170, 0x217F, -16, 1 },
    { 0x2184, 0x2184, -1, 1 }, { 0x24D0, 0x24E9, -26, 1 },
    { 0x2C30, 0x2C5E, -48, 1 }, { 0x2C61, 0x2C61, -1, 1 },
    { 0x2C65, 0x2C65, -10795, 1 }, { 0x2C66, 0x2C66, -10792, 1 },
    { 0x2C68, 0x2C6C, -1, 2 }, { 0x2C73, 0x2C73, -1, 1 },
    { 0x2C76, 0x2C76, -1, 1 }, { 0x2C81, 0x2CE3, -1, 2 },
    { 0x2D00, 0x2D25, -7264, 1 }, { 0xA641, 0xA65F, -1, 2 },
    { 0xA663, 0xA66D, -1, 2 }, { 0xA681, 0xA697, -1, 2 },
    { 0xA723, 0xA72F, -1, 2 }, { 0xA733, 0xA76F, -1, 2 },
    { 0xA77A, 0xA77C, -1, 2 }, { 0xA77F, 0xA787, -1, 2 },
    { 0xA78C, 0xA78C, -1, 1 }, { 0xFF41, 0xFF5A, -32, 1 },
};

void
FormatBuildUpCaseTable(_Out_ PWCHAR Table)
{
    ULONG Index;
    ULONG Character;

    for (Index = 0; Index < NTFS_UPCASE_LENGTH; Index++)
        Table[Index] = (WCHAR)Index;

    for (Index = 0; Index < RTL_NUMBER_OF(FormatUpCaseRuns); Index++)
    {
        const FormatUpCaseRun* Run = &FormatUpCaseRuns[Index];

        for (Character = Run->First;
             Character <= Run->Last;
             Character += Run->Step)
        {
            Table[Character] = (WCHAR)(Character + Run->Delta);
        }
    }
}

typedef struct FormatAttrDefTemplate
{
    PCWSTR Label;
    ULONG AttributeType;
    ULONG CollationRule;
    ULONG Flags;
    ULONGLONG MinimumSize;
    ULONGLONG MaximumSize;
} FormatAttrDefTemplate;

static const FormatAttrDefTemplate FormatAttrDefTemplates[] =
{
    { L"$STANDARD_INFORMATION", 0x10,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 48, 72 },
    { L"$ATTRIBUTE_LIST",       0x20,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, ~0ULL },
    { L"$FILE_NAME",            0x30,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_INDEXED | ATTRDEF_RESIDENT, 68, 578 },
    { L"$OBJECT_ID",            0x40,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 0, 256 },
    { L"$SECURITY_DESCRIPTOR",  0x50,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, ~0ULL },
    { L"$VOLUME_NAME",          0x60,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 2, 256 },
    { L"$VOLUME_INFORMATION",   0x70,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 12, 12 },
    { L"$DATA",                 0x80,  ATTRDEF_COLLATION_BINARY,
      0, 0, ~0ULL },
    { L"$INDEX_ROOT",           0x90,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 0, ~0ULL },
    { L"$INDEX_ALLOCATION",     0xA0,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, ~0ULL },
    { L"$BITMAP",               0xB0,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, ~0ULL },
    { L"$REPARSE_POINT",        0xC0,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, 16384 },
    { L"$EA_INFORMATION",       0xD0,  ATTRDEF_COLLATION_BINARY,
      ATTRDEF_RESIDENT, 8, 8 },
    { L"$EA",                   0xE0,  ATTRDEF_COLLATION_BINARY,
      0, 0, 65536 },
    { L"$LOGGED_UTILITY_STREAM", 0x100, ATTRDEF_COLLATION_BINARY,
      ATTRDEF_NON_RESIDENT, 0, 65536 },
};

C_ASSERT(RTL_NUMBER_OF(FormatAttrDefTemplates) == NTFS_ATTRDEF_ENTRIES - 1);

/*
 * The table is terminated by a zeroed entry: Volume::LoadAttributeDefinitions
 * stops at the first entry whose AttributeType is 0.
 */
void
FormatBuildAttrDefTable(_Out_ PAttrDefEntry Table)
{
    ULONG Index;

    RtlZeroMemory(Table, NTFS_ATTRDEF_SIZE);

    for (Index = 0; Index < RTL_NUMBER_OF(FormatAttrDefTemplates); Index++)
    {
        const FormatAttrDefTemplate* Template = &FormatAttrDefTemplates[Index];
        ULONG Character;

        for (Character = 0;
             Character < RTL_NUMBER_OF(Table[Index].Label) - 1 &&
             Template->Label[Character] != L'\0';
             Character++)
        {
            Table[Index].Label[Character] = Template->Label[Character];
        }

        Table[Index].AttributeType = Template->AttributeType;
        Table[Index].DisplayRule = 0;
        Table[Index].CollationRule = Template->CollationRule;
        Table[Index].Flags = Template->Flags;
        Table[Index].MinimumSize = Template->MinimumSize;
        Table[Index].MaximumSize = Template->MaximumSize;
    }
}

/* Self-relative security descriptor pieces, laid out by hand so that this
 * file stays free of the SID/ACL helpers, which are not available in every
 * environment ntfslib builds for. */
#define SD_CONTROL_SELF_RELATIVE 0x8000
#define SD_CONTROL_DACL_PRESENT  0x0004
#define SD_ACL_REVISION          2
#define SD_ACCESS_ALLOWED        0
#define SD_OBJECT_INHERIT        0x01
#define SD_CONTAINER_INHERIT     0x02
#define SD_FILE_ALL_ACCESS       0x001F01FF
#define SD_FILE_GENERIC_READ     0x00120089
#define SD_FILE_GENERIC_WRITE    0x00120116

static ULONG
FormatWriteSid(_Out_ PUCHAR Buffer,
               _In_ UCHAR Authority,
               _In_ const ULONG* SubAuthorities,
               _In_ UCHAR SubAuthorityCount)
{
    ULONG Offset = 0;
    UCHAR Index;

    Buffer[Offset++] = 1;                   /* SID revision */
    Buffer[Offset++] = SubAuthorityCount;

    /* 48 bit big-endian identifier authority. */
    Buffer[Offset++] = 0;
    Buffer[Offset++] = 0;
    Buffer[Offset++] = 0;
    Buffer[Offset++] = 0;
    Buffer[Offset++] = 0;
    Buffer[Offset++] = Authority;

    for (Index = 0; Index < SubAuthorityCount; Index++)
    {
        Buffer[Offset++] = (UCHAR)(SubAuthorities[Index]);
        Buffer[Offset++] = (UCHAR)(SubAuthorities[Index] >> 8);
        Buffer[Offset++] = (UCHAR)(SubAuthorities[Index] >> 16);
        Buffer[Offset++] = (UCHAR)(SubAuthorities[Index] >> 24);
    }

    return Offset;
}

static void
FormatWriteUlong(_Out_ PUCHAR Buffer,
                 _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
    Buffer[2] = (UCHAR)(Value >> 16);
    Buffer[3] = (UCHAR)(Value >> 24);
}

static void
FormatWriteUshort(_Out_ PUCHAR Buffer,
                  _In_ USHORT Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
}

/*
 * Builds "Everyone: full control, inheritable" owned by Administrators. This
 * mirrors what a freshly formatted Windows volume's root carries before any
 * ACL is applied, and gives the volume a valid descriptor to inherit from.
 */
ULONG
FormatBuildDefaultSecurityDescriptor(_Out_ PUCHAR Buffer,
                                     _In_ ULONG BufferLength)
{
    static const ULONG AdministratorsSubAuthorities[] = { 32, 544 };
    static const ULONG LocalSystemSubAuthorities[] = { 18 };
    static const ULONG WorldSubAuthorities[] = { 0 };

    const ULONG DaclOffset = 20;
    const ULONG AceOffset = DaclOffset + 8;
    const ULONG AceSidOffset = AceOffset + 8;
    ULONG OwnerOffset;
    ULONG GroupOffset;
    ULONG AceSidLength;
    ULONG Total;

    if (BufferLength < 128)
        return 0;

    RtlZeroMemory(Buffer, BufferLength);

    /* DACL: one inheritable allow-all ACE for S-1-1-0. */
    AceSidLength = FormatWriteSid(Buffer + AceSidOffset,
                                  1,
                                  WorldSubAuthorities,
                                  1);

    Buffer[AceOffset + 0] = SD_ACCESS_ALLOWED;
    Buffer[AceOffset + 1] = SD_OBJECT_INHERIT | SD_CONTAINER_INHERIT;
    FormatWriteUshort(Buffer + AceOffset + 2, (USHORT)(8 + AceSidLength));
    FormatWriteUlong(Buffer + AceOffset + 4, SD_FILE_ALL_ACCESS);

    Buffer[DaclOffset + 0] = SD_ACL_REVISION;
    Buffer[DaclOffset + 1] = 0;
    FormatWriteUshort(Buffer + DaclOffset + 2,
                      (USHORT)(8 + 8 + AceSidLength));
    FormatWriteUshort(Buffer + DaclOffset + 4, 1);   /* AceCount */
    FormatWriteUshort(Buffer + DaclOffset + 6, 0);

    OwnerOffset = AceSidOffset + AceSidLength;
    GroupOffset = OwnerOffset + FormatWriteSid(Buffer + OwnerOffset,
                                               5,
                                               AdministratorsSubAuthorities,
                                               2);
    Total = GroupOffset + FormatWriteSid(Buffer + GroupOffset,
                                         5,
                                         LocalSystemSubAuthorities,
                                         1);

    /* Header. */
    Buffer[0] = 1;      /* SECURITY_DESCRIPTOR_REVISION */
    Buffer[1] = 0;
    FormatWriteUshort(Buffer + 2,
                      SD_CONTROL_SELF_RELATIVE | SD_CONTROL_DACL_PRESENT);
    FormatWriteUlong(Buffer + 4, OwnerOffset);
    FormatWriteUlong(Buffer + 8, GroupOffset);
    FormatWriteUlong(Buffer + 12, 0);           /* no SACL */
    FormatWriteUlong(Buffer + 16, DaclOffset);

    return Total;
}

static ULONG
FormatWriteAccessAllowedAce(_Out_ PUCHAR Buffer,
                            _In_ ULONG AccessMask,
                            _In_ UCHAR Authority,
                            _In_ const ULONG* SubAuthorities,
                            _In_ UCHAR SubAuthorityCount)
{
    ULONG Length;

    Length = 8 + FormatWriteSid(Buffer + 8,
                                Authority,
                                SubAuthorities,
                                SubAuthorityCount);
    Buffer[0] = SD_ACCESS_ALLOWED;
    Buffer[1] = 0;
    FormatWriteUshort(Buffer + 2, (USHORT)Length);
    FormatWriteUlong(Buffer + 4, AccessMask);

    return Length;
}

ULONG
FormatBuildSystemSecurityDescriptor(_Out_ PUCHAR Buffer,
                                    _In_ ULONG BufferLength,
                                    _In_ BOOLEAN Writable)
{
    static const ULONG AdministratorsSubAuthorities[] = { 32, 544 };
    static const ULONG LocalSystemSubAuthorities[] = { 18 };

    const ULONG DaclOffset = 20;
    ULONG AccessMask = SD_FILE_GENERIC_READ;
    ULONG OwnerOffset;
    ULONG GroupOffset;
    ULONG Offset;

    if (BufferLength < 128)
        return 0;

    if (Writable)
        AccessMask |= SD_FILE_GENERIC_WRITE;

    RtlZeroMemory(Buffer, BufferLength);

    Offset = DaclOffset + 8;
    Offset += FormatWriteAccessAllowedAce(Buffer + Offset,
                                          AccessMask,
                                          5,
                                          LocalSystemSubAuthorities,
                                          1);
    Offset += FormatWriteAccessAllowedAce(Buffer + Offset,
                                          AccessMask,
                                          5,
                                          AdministratorsSubAuthorities,
                                          2);

    Buffer[DaclOffset + 0] = SD_ACL_REVISION;
    Buffer[DaclOffset + 1] = 0;
    FormatWriteUshort(Buffer + DaclOffset + 2, (USHORT)(Offset - DaclOffset));
    FormatWriteUshort(Buffer + DaclOffset + 4, 2);
    FormatWriteUshort(Buffer + DaclOffset + 6, 0);

    OwnerOffset = Offset;
    Offset += FormatWriteSid(Buffer + Offset, 5, LocalSystemSubAuthorities, 1);
    GroupOffset = Offset;
    Offset += FormatWriteSid(Buffer + Offset, 5, AdministratorsSubAuthorities, 2);

    Buffer[0] = 1;
    Buffer[1] = 0;
    FormatWriteUshort(Buffer + 2,
                      SD_CONTROL_SELF_RELATIVE | SD_CONTROL_DACL_PRESENT);
    FormatWriteUlong(Buffer + 4, OwnerOffset);
    FormatWriteUlong(Buffer + 8, GroupOffset);
    FormatWriteUlong(Buffer + 12, 0);
    FormatWriteUlong(Buffer + 16, DaclOffset);

    return Offset;
}

ULONG
FormatSecurityDescriptorHash(_In_ const UCHAR* Descriptor,
                             _In_ ULONG DescriptorLength)
{
    ULONG Hash = 0;
    ULONG Offset;

    for (Offset = 0; Offset + sizeof(ULONG) <= DescriptorLength; Offset += sizeof(ULONG))
    {
        ULONG Value = (ULONG)Descriptor[Offset] |
                      ((ULONG)Descriptor[Offset + 1] << 8) |
                      ((ULONG)Descriptor[Offset + 2] << 16) |
                      ((ULONG)Descriptor[Offset + 3] << 24);

        Hash = Value + ((Hash << 3) | (Hash >> 29));
    }

    return Hash;
}
