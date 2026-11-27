/*
 * PROJECT:     LiberNT Web Browser
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HTML page reader and form encoder
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>

typedef enum _HTML_ITEM_KIND
{
    HtmlItemText,
    HtmlItemLink,
    HtmlItemInput,
    HtmlItemSelect,
    HtmlItemTextArea,
    HtmlItemButton
} HTML_ITEM_KIND;

typedef struct _HTML_OPTION
{
    char *Value;
    char *Text;
    int Selected;
} HTML_OPTION;

typedef struct _HTML_ITEM
{
    HTML_ITEM_KIND Kind;
    int Form;
    int Heading;
    int Checked;
    int Disabled;
    int Hidden;
    char *Text;
    char *Href;
    char *Type;
    char *Name;
    char *Value;
    char *Id;
    char *Placeholder;
    HTML_OPTION *Options;
    size_t OptionCount;
} HTML_ITEM;

typedef struct _HTML_FORM
{
    char *Action;
    char *Method;
    char *Enctype;
} HTML_FORM;

typedef struct _HTML_DOCUMENT
{
    char *Title;
    char *BaseHref;
    char *RefreshUrl;
    int HasRefresh;
    int RefreshSeconds;
    int HasScript;
    HTML_ITEM *Items;
    size_t ItemCount;
    size_t ItemCapacity;
    HTML_FORM *Forms;
    size_t FormCount;
    size_t FormCapacity;
} HTML_DOCUMENT;

int
HtmlParse(
    const char *Data,
    size_t Length,
    HTML_DOCUMENT *Document);

void
HtmlFree(
    HTML_DOCUMENT *Document);

int
HtmlIsVisibleItem(
    const HTML_ITEM *Item);

int
HtmlIsSubmitItem(
    const HTML_ITEM *Item);

int
HtmlSetValue(
    HTML_ITEM *Item,
    const char *Value);

int
HtmlFormIsPost(
    const HTML_DOCUMENT *Document,
    size_t Form);

int
HtmlFormIsMultipart(
    const HTML_DOCUMENT *Document,
    size_t Form);

char *
HtmlEncodeForm(
    const HTML_DOCUMENT *Document,
    size_t Form,
    size_t Submitter,
    const char *Boundary,
    size_t *Length);

char *
HtmlFindCharset(
    const char *Data,
    size_t Length);
