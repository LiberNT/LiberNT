/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Network Location Awareness Internet probe request and response classification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <string.h>
#include "probe.h"

static int Failures;

#define EXPECTED_CONTENT "Microsoft Connect Test"

static void
CheckResult(const char *Name, const char *Response, NLA_PROBE_RESULT Expected)
{
    NLA_PROBE_RESULT Result = NlaClassifyProbeResponse(Response, strlen(Response), EXPECTED_CONTENT);

    if (Result != Expected)
    {
        printf("FAIL %s: got %d, expected %d\n", Name, (int)Result, (int)Expected);
        Failures++;
    }
}

static void
TestRequest(void)
{
    char Buffer[256];
    size_t Length;

    Length = NlaBuildProbeRequest(Buffer, sizeof(Buffer), "www.msftconnecttest.com", "connecttest.txt");
    if (Length != strlen(Buffer) ||
        strcmp(Buffer, "GET /connecttest.txt HTTP/1.0\r\nHost: www.msftconnecttest.com\r\nConnection: close\r\n\r\n") != 0)
    {
        printf("FAIL request: \"%s\"\n", Buffer);
        Failures++;
    }

    Length = NlaBuildProbeRequest(Buffer, sizeof(Buffer), "10.0.2.2", "/probe/ok.txt");
    if (Length == 0 || strncmp(Buffer, "GET /probe/ok.txt HTTP/1.0\r\nHost: 10.0.2.2\r\n", 44) != 0)
    {
        printf("FAIL request with absolute path: \"%s\"\n", Buffer);
        Failures++;
    }

    if (NlaBuildProbeRequest(Buffer, 16, "www.msftconnecttest.com", "connecttest.txt") != 0 || Buffer[0] != 0)
    {
        printf("FAIL truncated request was not refused\n");
        Failures++;
    }
}

static void
TestClassification(void)
{
    CheckResult("expected content",
                "HTTP/1.1 200 OK\r\nContent-Length: 22\r\nContent-Type: text/plain\r\n\r\nMicrosoft Connect Test",
                NlaProbeInternet);
    CheckResult("expected content, HTTP/1.0 close delimited",
                "HTTP/1.0 200 OK\r\n\r\nMicrosoft Connect Test", NlaProbeInternet);
    CheckResult("expected content with trailing line break",
                "HTTP/1.1 200 OK\r\n\r\nMicrosoft Connect Test\r\n", NlaProbeInternet);
    CheckResult("expected content, chunked",
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n16\r\nMicrosoft Connect Test\r\n0\r\n\r\n",
                NlaProbeInternet);
    CheckResult("expected content, two chunks",
                "HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\n\r\n9;x=1\r\nMicrosoft\r\nd\r\n Connect Test\r\n0\r\n\r\n",
                NlaProbeInternet);
    CheckResult("bare line feeds",
                "HTTP/1.1 200 OK\nContent-Length: 22\n\nMicrosoft Connect Test", NlaProbeInternet);
    CheckResult("redirect to a portal",
                "HTTP/1.1 302 Found\r\nLocation: http://192.168.1.1/login\r\nContent-Length: 0\r\n\r\n",
                NlaProbeCaptivePortal);
    CheckResult("not modified",
                "HTTP/1.1 304 Not Modified\r\n\r\n", NlaProbeCaptivePortal);
    CheckResult("portal page served in place",
                "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html><body>Sign in</body></html>",
                NlaProbeCaptivePortal);
    CheckResult("empty 200 body", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", NlaProbeCaptivePortal);
    CheckResult("content cut by Content-Length",
                "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nMicrosoft Connect Test", NlaProbeCaptivePortal);
    CheckResult("expected content followed by more text",
                "HTTP/1.1 200 OK\r\n\r\nMicrosoft Connect Test - hotspot", NlaProbeCaptivePortal);
    CheckResult("different case", "HTTP/1.1 200 OK\r\n\r\nmicrosoft connect test", NlaProbeCaptivePortal);
    CheckResult("headers never finished", "HTTP/1.1 200 OK\r\nContent-Type: text/plain", NlaProbeCaptivePortal);
    CheckResult("broken chunk", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n", NlaProbeCaptivePortal);
    CheckResult("not HTTP", "SSH-2.0-OpenSSH_9.0\r\n", NlaProbeCaptivePortal);
    CheckResult("proxy refusal", "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n", NlaProbeNoInternet);
    CheckResult("nothing received", "", NlaProbeNoInternet);
}

int
main(void)
{
    TestRequest();
    TestClassification();
    if (Failures)
    {
        printf("%d failure(s)\n", Failures);
        return 1;
    }
    printf("nlasvc probe: all checks passed\n");
    return 0;
}
