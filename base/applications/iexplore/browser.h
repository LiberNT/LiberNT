/*
 * PROJECT:     LiberNT Web Browser
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Address, character set, title and generated page helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>

typedef enum _BROWSER_CONTENT
{
    BrowserContentDownload,
    BrowserContentHtml,
    BrowserContentText,
    BrowserContentImage
} BROWSER_CONTENT;

int BrowserNormalizeUrl(const char *Input, char *Url, size_t Size);
BROWSER_CONTENT BrowserContentKind(const char *ContentType);
int BrowserCharset(const char *ContentType, const char *Body, size_t Length, char *Charset, size_t Size);
unsigned BrowserCodePage(const char *Charset);
size_t BrowserTitle(const char *Html, size_t Length, char *Title, size_t Size);
void BrowserFileName(const char *Disposition, const char *Url, char *Name, size_t Size);
char *BrowserTextPage(const char *Text, size_t Length, size_t *PageLength);
char *BrowserImagePage(const char *Url, size_t *PageLength);
char *BrowserMessagePage(const char *Heading, const char *Detail, size_t *PageLength);
