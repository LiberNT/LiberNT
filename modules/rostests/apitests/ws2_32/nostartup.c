/*
 *  PROJECT:         ReactOS api tests
 *  LICENSE:         GPLv2+ - See COPYING in the top level directory
 *  PURPOSE:         Test for WSAStartup
 *  PROGRAMMER:      Sylvain Petreolle <sylvain.petreolle@reactos.org>
 */

#include "ws2_32.h"

static void TestByteOrder(void)
{
    const ULONGLONG Host = 0x0123456789abcdefULL;
    const ULONGLONG Network = 0xefcdab8967452301ULL;
    const UINT FloatBits = 0x7fc12345;
    const ULONGLONG DoubleBits = 0x8000000000000000ULL;
    float Float;
    double Double;
    UINT ConvertedFloat;
    ULONGLONG ConvertedDouble;

    ok(htonll(Host) == Network && ntohll(Network) == Host, "64-bit byte order\n");

    memcpy(&Float, &FloatBits, sizeof(Float));
    ok(htonf(Float) == 0x4523c17f, "float NaN payload to network order\n");
    Float = ntohf(0x4523c17f);
    memcpy(&ConvertedFloat, &Float, sizeof(Float));
    ok(ConvertedFloat == FloatBits, "float NaN payload from network order\n");

    memcpy(&Double, &DoubleBits, sizeof(Double));
    ok(htond(Double) == 0x80, "negative zero to network order\n");
    Double = ntohd(0x80);
    memcpy(&ConvertedDouble, &Double, sizeof(Double));
    ok(ConvertedDouble == DoubleBits, "negative zero from network order\n");
}

// This test depends on WSAStartup not having been called
START_TEST(nostartup)
{
    int Error = 0;

    TestByteOrder();
    ok(WSASocketA(0, 0, 0, NULL, 0, 0) == INVALID_SOCKET, "WSASocketA should have failed\n");

    WSASetLastError(0xdeadbeef);
    getservbyname(NULL, NULL);
    Error = WSAGetLastError();
    ok_dec(Error, WSANOTINITIALISED);

    WSASetLastError(0xdeadbeef);
    getservbyport(0, NULL);
    Error = WSAGetLastError();
    ok_dec(Error, WSANOTINITIALISED);

    WSASetLastError(0xdeadbeef);
    gethostbyname(NULL);
    Error = WSAGetLastError();
    ok_dec(Error, WSANOTINITIALISED);

    ok_dec(inet_addr("127.0.0.1"), 0x100007f);
}
