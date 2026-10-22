/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Toast notification popups shown on the desktop
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <mmsystem.h>
#include <wincodec.h>

#define TIMER_DISMISS 1
#define TIMER_SCHEDULE 2
#define TOAST_WIDTH 364
#define TOAST_MARGIN 12
#define TOAST_SPACING 8
#define TOAST_PADDING 12
#define TOAST_HEADER 20
#define TOAST_LOGO 48
#define TOAST_ROW 32
#define TOAST_MAX_VISIBLE 3
#define TOAST_MAX_BUTTONS 5
#define TOAST_MAX_INPUTS 5
#define TOAST_LONG_DURATION 25000
#define TOAST_TITLE_LINES 2
#define TOAST_BODY_LINES 4

struct toast_button
{
    WCHAR *content;
    WCHAR *arguments;
    WCHAR *activation_type;
    RECT rect;
};

struct toast_field
{
    WCHAR *id;
    BOOL selection;
    HWND control;
    UINT choice_count;
    WCHAR **choice_ids;
};

struct toast_progress
{
    BOOL present;
    BOOL indeterminate;
    double value;
    WCHAR *title;
    WCHAR *value_text;
    WCHAR *status;
};

struct popup
{
    struct list entry;
    HWND hwnd;
    IToastNotification *toast;
    WCHAR *app_id;
    WCHAR key[STORE_NAME_LENGTH];
    struct app_identity identity;
    HICON app_icon;
    WCHAR *launch;
    WCHAR *activation_type;
    WCHAR *title;
    WCHAR *body;
    WCHAR *attribution;
    HBITMAP logo;
    HBITMAP hero;
    struct toast_progress progress;
    struct toast_button buttons[TOAST_MAX_BUTTONS];
    UINT button_count;
    struct toast_field fields[TOAST_MAX_INPUTS];
    UINT field_count;
    WCHAR *audio;
    BOOL silent;
    UINT duration;
    BOOL persistent;
    BOOL hover;
    DWORD data_revision;
    UINT dpi;
    BOOL dark;
    HFONT caption_font;
    HFONT title_font;
    HFONT body_font;
    HFONT glyph_font;
    HBRUSH field_brush;
    RECT close_rect;
    int width;
    int height;
};

static const WCHAR host_class[] = L"LiberNTToastHost";
static const WCHAR popup_class[] = L"LiberNTToastPopup";
static struct list visible_popups = LIST_INIT(visible_popups);
static struct list pending_popups = LIST_INIT(pending_popups);
static HWND presenter_window;

static int scale(struct popup *popup, int value)
{
    return MulDiv(value, popup->dpi, 96);
}

static WCHAR *element_attribute(IXmlElement *element, const WCHAR *name)
{
    HSTRING name_string, value;
    WCHAR *result = NULL;

    if (FAILED(WindowsCreateString(name, wcslen(name), &name_string)))
        return NULL;
    if (SUCCEEDED(IXmlElement_GetAttribute(element, name_string, &value)))
    {
        hstring_to_wide(value, &result);
        WindowsDeleteString(value);
    }
    WindowsDeleteString(name_string);
    if (result && !result[0])
    {
        free(result);
        result = NULL;
    }
    return result;
}

static WCHAR *node_text(IXmlNode *node)
{
    IXmlNodeSerializer *serializer;
    WCHAR *result = NULL;
    HSTRING value;

    if (SUCCEEDED(IXmlNode_QueryInterface(node, &IID_IXmlNodeSerializer, (void **)&serializer)))
    {
        if (SUCCEEDED(IXmlNodeSerializer_get_InnerText(serializer, &value)))
        {
            hstring_to_wide(value, &result);
            WindowsDeleteString(value);
        }
        IXmlNodeSerializer_Release(serializer);
    }
    return result;
}

static UINT32 elements_by_tag(IXmlDocument *document, const WCHAR *tag, IXmlNodeList **list)
{
    HSTRING name;
    UINT32 length = 0;

    *list = NULL;
    if (FAILED(WindowsCreateString(tag, wcslen(tag), &name)))
        return 0;
    if (SUCCEEDED(IXmlDocument_GetElementsByTagName(document, name, list)))
        IXmlNodeList_get_Length(*list, &length);
    WindowsDeleteString(name);
    return length;
}

static IXmlElement *list_element(IXmlNodeList *list, UINT32 index)
{
    IXmlElement *element = NULL;
    IXmlNode *node;

    if (SUCCEEDED(IXmlNodeList_Item(list, index, &node)))
    {
        IXmlNode_QueryInterface(node, &IID_IXmlElement, (void **)&element);
        IXmlNode_Release(node);
    }
    return element;
}

static WCHAR *bind_value(INotificationData *data, WCHAR *text)
{
    IMap_HSTRING_HSTRING *values;
    HSTRING key, value;
    WCHAR *result = NULL;
    size_t length;

    if (!data || !text || text[0] != L'{' || (length = wcslen(text)) < 3 || text[length - 1] != L'}')
        return text;
    if (FAILED(INotificationData_get_Values(data, &values)))
        return text;
    if (SUCCEEDED(WindowsCreateString(text + 1, (UINT32)(length - 2), &key)))
    {
        if (SUCCEEDED(IMap_HSTRING_HSTRING_Lookup(values, key, &value)))
        {
            hstring_to_wide(value, &result);
            WindowsDeleteString(value);
        }
        WindowsDeleteString(key);
    }
    IMap_HSTRING_HSTRING_Release(values);
    if (!result)
        return text;
    free(text);
    return result;
}

static BOOL source_path(const WCHAR *source, WCHAR *path, DWORD size)
{
    DWORD length = size;

    if (!wcsnicmp(source, L"file:", 5))
        return SUCCEEDED(PathCreateFromUrlW(source, path, &length, 0));
    if ((source[0] && source[1] == L':') || (source[0] == L'\\' && source[1] == L'\\'))
    {
        lstrcpynW(path, source, size);
        return TRUE;
    }
    return FALSE;
}

static HBITMAP load_image(const WCHAR *source, UINT width, UINT height, BOOL crop)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    IWICBitmapClipper *clipper = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICBitmapSource *current;
    BITMAPINFO info = {{sizeof(info.bmiHeader)}};
    HBITMAP bitmap = NULL;
    WCHAR path[MAX_PATH];
    UINT source_width, source_height, target_width = width, target_height = height;
    WICRect rect;
    void *bits;

    if (!source || !source_path(source, path, ARRAYSIZE(path)))
        return NULL;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                (void **)&factory)))
        return NULL;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ,
                                                            WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) ||
        FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) ||
        FAILED(IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppPBGRA,
                                              WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)) ||
        FAILED(IWICFormatConverter_GetSize(converter, &source_width, &source_height)) ||
        !source_width || !source_height)
        goto done;

    current = (IWICBitmapSource *)converter;
    rect.X = rect.Y = 0;
    rect.Width = source_width;
    rect.Height = source_height;
    if (crop)
    {
        if ((UINT64)source_width * height > (UINT64)source_height * width)
        {
            rect.Width = MulDiv(source_height, width, height);
            rect.X = (source_width - rect.Width) / 2;
        }
        else
        {
            rect.Height = MulDiv(source_width, height, width);
            rect.Y = (source_height - rect.Height) / 2;
        }
        if (SUCCEEDED(IWICImagingFactory_CreateBitmapClipper(factory, &clipper)) &&
            SUCCEEDED(IWICBitmapClipper_Initialize(clipper, current, &rect)))
            current = (IWICBitmapSource *)clipper;
    }
    else if ((UINT64)source_width * height > (UINT64)source_height * width)
    {
        target_height = max(1, MulDiv(source_height, width, source_width));
    }
    else
    {
        target_width = max(1, MulDiv(source_width, height, source_height));
    }
    if (FAILED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) ||
        FAILED(IWICBitmapScaler_Initialize(scaler, current, target_width, target_height,
                                           WICBitmapInterpolationModeFant)))
        goto done;

    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -(LONG)height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    if (!(bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0)))
        goto done;
    memset(bits, 0, width * height * 4);
    rect.X = rect.Y = 0;
    rect.Width = target_width;
    rect.Height = target_height;
    if (FAILED(IWICBitmapScaler_CopyPixels(scaler, &rect, width * 4, width * height * 4,
                                           (BYTE *)bits + ((height - target_height) / 2) * width * 4 +
                                           ((width - target_width) / 2) * 4)))
    {
        DeleteObject(bitmap);
        bitmap = NULL;
    }

done:
    if (scaler)
        IWICBitmapScaler_Release(scaler);
    if (clipper)
        IWICBitmapClipper_Release(clipper);
    if (converter)
        IWICFormatConverter_Release(converter);
    if (frame)
        IWICBitmapFrameDecode_Release(frame);
    if (decoder)
        IWICBitmapDecoder_Release(decoder);
    IWICImagingFactory_Release(factory);
    return bitmap;
}

static void append_line(WCHAR **target, const WCHAR *line)
{
    size_t old_length = *target ? wcslen(*target) : 0;
    WCHAR *joined = realloc(*target, (old_length + wcslen(line) + 2) * sizeof(WCHAR));

    if (!joined)
        return;
    if (old_length)
        joined[old_length++] = L'\n';
    wcscpy(joined + old_length, line);
    *target = joined;
}

static void parse_content(struct popup *popup)
{
    IToastNotification4 *toast4;
    INotificationData *data = NULL;
    IXmlElement *root = NULL, *element;
    IXmlDocument *document;
    IXmlNodeList *list;
    UINT32 count, i, j;
    BOOL legacy_image;
    WCHAR *value, *placement, *template_name = NULL;
    HSTRING selection_tag;

    if (FAILED(IToastNotification_get_Content(popup->toast, &document)))
        return;
    if (!(data = history_entry_data(popup->app_id, popup->key, &popup->data_revision)) &&
        SUCCEEDED(IToastNotification_QueryInterface(popup->toast, &IID_IToastNotification4, (void **)&toast4)))
    {
        IToastNotification4_get_Data(toast4, &data);
        IToastNotification4_Release(toast4);
    }
    if (SUCCEEDED(IXmlDocument_get_DocumentElement(document, &root)) && root)
    {
        popup->launch = element_attribute(root, L"launch");
        popup->activation_type = element_attribute(root, L"activationType");
        if ((value = element_attribute(root, L"duration")))
        {
            if (!wcsicmp(value, L"long"))
                popup->duration = TOAST_LONG_DURATION;
            free(value);
        }
        if ((value = element_attribute(root, L"scenario")))
        {
            popup->persistent = !wcsicmp(value, L"reminder") || !wcsicmp(value, L"alarm") ||
                                !wcsicmp(value, L"incomingCall") || !wcsicmp(value, L"urgent");
            free(value);
        }
        IXmlElement_Release(root);
    }

    if ((count = elements_by_tag(document, L"binding", &list)))
    {
        if ((element = list_element(list, 0)))
        {
            template_name = element_attribute(element, L"template");
            IXmlElement_Release(element);
        }
    }
    if (list)
        IXmlNodeList_Release(list);
    legacy_image = template_name && !wcsnicmp(template_name, L"ToastImageAndText", 17);
    free(template_name);

    count = elements_by_tag(document, L"text", &list);
    for (i = 0; i < count; i++)
    {
        IXmlNode *node;

        if (FAILED(IXmlNodeList_Item(list, i, &node)))
            continue;
        value = bind_value(data, node_text(node));
        placement = NULL;
        if (SUCCEEDED(IXmlNode_QueryInterface(node, &IID_IXmlElement, (void **)&element)))
        {
            placement = element_attribute(element, L"placement");
            IXmlElement_Release(element);
        }
        IXmlNode_Release(node);
        if (value && value[0])
        {
            if (placement && !wcsicmp(placement, L"attribution"))
                append_line(&popup->attribution, value);
            else if (!popup->title)
                popup->title = wcsdup(value);
            else
                append_line(&popup->body, value);
        }
        free(value);
        free(placement);
    }
    if (list)
        IXmlNodeList_Release(list);

    count = elements_by_tag(document, L"image", &list);
    for (i = 0; i < count; i++)
    {
        if (!(element = list_element(list, i)))
            continue;
        placement = element_attribute(element, L"placement");
        value = element_attribute(element, L"src");
        if (placement && !wcsicmp(placement, L"hero"))
        {
            if (!popup->hero)
                popup->hero = load_image(value, scale(popup, TOAST_WIDTH), scale(popup, TOAST_WIDTH / 2), TRUE);
        }
        else if ((placement && !wcsicmp(placement, L"appLogoOverride")) || (!placement && legacy_image))
        {
            if (!popup->logo)
                popup->logo = load_image(value, scale(popup, TOAST_LOGO), scale(popup, TOAST_LOGO), FALSE);
        }
        free(placement);
        free(value);
        IXmlElement_Release(element);
    }
    if (list)
        IXmlNodeList_Release(list);

    if (elements_by_tag(document, L"progress", &list) && (element = list_element(list, 0)))
    {
        popup->progress.present = TRUE;
        popup->progress.title = bind_value(data, element_attribute(element, L"title"));
        popup->progress.value_text = bind_value(data, element_attribute(element, L"valueStringOverride"));
        popup->progress.status = bind_value(data, element_attribute(element, L"status"));
        value = bind_value(data, element_attribute(element, L"value"));
        if (!value || !wcsicmp(value, L"indeterminate"))
            popup->progress.indeterminate = TRUE;
        else
            popup->progress.value = min(1.0, max(0.0, wcstod(value, NULL)));
        free(value);
        IXmlElement_Release(element);
    }
    if (list)
        IXmlNodeList_Release(list);

    count = elements_by_tag(document, L"input", &list);
    for (i = 0; i < count && popup->field_count < TOAST_MAX_INPUTS; i++)
    {
        struct toast_field *field = &popup->fields[popup->field_count];
        IXmlNodeList *choices;
        IXmlElement *choice;
        UINT32 choice_count;

        if (!(element = list_element(list, i)))
            continue;
        field->id = element_attribute(element, L"id");
        value = element_attribute(element, L"type");
        field->selection = value && !wcsicmp(value, L"selection");
        free(value);
        field->control = NULL;
        if (field->selection && SUCCEEDED(WindowsCreateString(L"selection", 9, &selection_tag)))
        {
            if (SUCCEEDED(IXmlElement_GetElementsByTagName(element, selection_tag, &choices)))
            {
                IXmlNodeList_get_Length(choices, &choice_count);
                field->choice_ids = calloc(choice_count ? choice_count : 1, sizeof(*field->choice_ids));
                for (j = 0; field->choice_ids && j < choice_count; j++)
                {
                    if ((choice = list_element(choices, j)))
                    {
                        field->choice_ids[field->choice_count++] = element_attribute(choice, L"id");
                        IXmlElement_Release(choice);
                    }
                }
                IXmlNodeList_Release(choices);
            }
            WindowsDeleteString(selection_tag);
        }
        IXmlElement_Release(element);
        popup->field_count++;
    }
    if (list)
        IXmlNodeList_Release(list);

    count = elements_by_tag(document, L"action", &list);
    for (i = 0; i < count && popup->button_count < TOAST_MAX_BUTTONS; i++)
    {
        struct toast_button *button = &popup->buttons[popup->button_count];

        if (!(element = list_element(list, i)))
            continue;
        placement = element_attribute(element, L"placement");
        if (!placement || wcsicmp(placement, L"contextMenu"))
        {
            button->content = element_attribute(element, L"content");
            button->arguments = element_attribute(element, L"arguments");
            button->activation_type = element_attribute(element, L"activationType");
            popup->button_count++;
        }
        free(placement);
        IXmlElement_Release(element);
    }
    if (list)
        IXmlNodeList_Release(list);

    if (elements_by_tag(document, L"audio", &list) && (element = list_element(list, 0)))
    {
        popup->audio = element_attribute(element, L"src");
        value = element_attribute(element, L"silent");
        popup->silent = value && !wcsicmp(value, L"true");
        free(value);
        IXmlElement_Release(element);
    }
    if (list)
        IXmlNodeList_Release(list);

    if (data)
        INotificationData_Release(data);
    IXmlDocument_Release(document);
}

static void free_content(struct popup *popup)
{
    UINT i, j;

    free(popup->launch);
    free(popup->activation_type);
    free(popup->title);
    free(popup->body);
    free(popup->attribution);
    free(popup->audio);
    free(popup->progress.title);
    free(popup->progress.value_text);
    free(popup->progress.status);
    if (popup->logo)
        DeleteObject(popup->logo);
    if (popup->hero)
        DeleteObject(popup->hero);
    for (i = 0; i < popup->button_count; i++)
    {
        free(popup->buttons[i].content);
        free(popup->buttons[i].arguments);
        free(popup->buttons[i].activation_type);
    }
    for (i = 0; i < popup->field_count; i++)
    {
        if (popup->fields[i].control)
            DestroyWindow(popup->fields[i].control);
        free(popup->fields[i].id);
        for (j = 0; j < popup->fields[i].choice_count; j++)
            free(popup->fields[i].choice_ids[j]);
        free(popup->fields[i].choice_ids);
    }
    popup->launch = popup->activation_type = popup->title = popup->body = popup->attribution = popup->audio = NULL;
    memset(&popup->progress, 0, sizeof(popup->progress));
    popup->logo = popup->hero = NULL;
    popup->button_count = popup->field_count = 0;
}

static void free_popup(struct popup *popup)
{
    free_content(popup);
    if (popup->app_icon)
        DestroyIcon(popup->app_icon);
    if (popup->caption_font)
        DeleteObject(popup->caption_font);
    if (popup->title_font)
        DeleteObject(popup->title_font);
    if (popup->body_font)
        DeleteObject(popup->body_font);
    if (popup->glyph_font)
        DeleteObject(popup->glyph_font);
    if (popup->field_brush)
        DeleteObject(popup->field_brush);
    IToastNotification_Release(popup->toast);
    free(popup->app_id);
    free(popup);
}

static void colors(struct popup *popup, COLORREF *background, COLORREF *border, COLORREF *text,
                   COLORREF *secondary, COLORREF *button)
{
    if (popup->dark)
    {
        *background = RGB(43, 43, 43);
        *border = RGB(72, 72, 72);
        *text = RGB(255, 255, 255);
        *secondary = RGB(194, 194, 194);
        *button = RGB(61, 61, 61);
    }
    else
    {
        *background = RGB(243, 243, 243);
        *border = RGB(204, 204, 204);
        *text = RGB(0, 0, 0);
        *secondary = RGB(92, 92, 92);
        *button = RGB(226, 226, 226);
    }
}

static int text_height(struct popup *popup, HDC dc, HFONT font, const WCHAR *text, int width, UINT max_lines)
{
    TEXTMETRICW metrics;
    RECT rect = {0, 0, width, 0};
    HFONT old;
    int limit;

    if (!text || !text[0])
        return 0;
    old = SelectObject(dc, font);
    GetTextMetricsW(dc, &metrics);
    DrawTextW(dc, text, -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(dc, old);
    limit = metrics.tmHeight * max_lines;
    return min(rect.bottom, limit);
}

static void create_fonts(struct popup *popup)
{
    NONCLIENTMETRICSW metrics = {sizeof(metrics)};
    LOGFONTW font;

    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    font = metrics.lfMessageFont;
    font.lfHeight = -scale(popup, 12);
    font.lfWeight = FW_NORMAL;
    popup->caption_font = CreateFontIndirectW(&font);
    font.lfHeight = -scale(popup, 14);
    font.lfWeight = FW_SEMIBOLD;
    popup->title_font = CreateFontIndirectW(&font);
    font.lfWeight = FW_NORMAL;
    popup->body_font = CreateFontIndirectW(&font);
    memset(&font, 0, sizeof(font));
    font.lfHeight = -scale(popup, 10);
    font.lfCharSet = SYMBOL_CHARSET;
    wcscpy(font.lfFaceName, L"Marlett");
    popup->glyph_font = CreateFontIndirectW(&font);
}

static int content_left(struct popup *popup)
{
    return scale(popup, TOAST_PADDING) + (popup->logo ? scale(popup, TOAST_LOGO + TOAST_PADDING) : 0);
}

static int hero_height(struct popup *popup)
{
    return popup->hero ? scale(popup, TOAST_WIDTH / 2) : 0;
}

static void layout_popup(struct popup *popup)
{
    int padding = scale(popup, TOAST_PADDING), row = scale(popup, TOAST_ROW), spacing = scale(popup, TOAST_SPACING);
    int text_width, y, text_bottom, gap, i, x, button_width;
    HDC dc = GetDC(NULL);

    popup->width = scale(popup, TOAST_WIDTH);
    text_width = popup->width - content_left(popup) - padding;
    y = hero_height(popup) + padding + scale(popup, TOAST_HEADER) + spacing;
    text_bottom = y + text_height(popup, dc, popup->title_font, popup->title, text_width, TOAST_TITLE_LINES) +
                  text_height(popup, dc, popup->body_font, popup->body, text_width, TOAST_BODY_LINES) +
                  text_height(popup, dc, popup->caption_font, popup->attribution, text_width, 1);
    if (popup->progress.present)
        text_bottom += 3 * text_height(popup, dc, popup->caption_font, L"0", text_width, 1) + spacing;
    y = max(text_bottom, popup->logo ? y + scale(popup, TOAST_LOGO) : text_bottom);
    for (i = 0; i < (int)popup->field_count; i++)
    {
        y += spacing;
        if (popup->fields[i].control)
            SetWindowPos(popup->fields[i].control, NULL, padding, y, popup->width - 2 * padding,
                         popup->fields[i].selection ? row * 6 : row, SWP_NOZORDER | SWP_NOACTIVATE);
        y += row;
    }
    if (popup->button_count)
    {
        y += spacing;
        gap = spacing;
        button_width = (popup->width - 2 * padding - gap * ((int)popup->button_count - 1)) / (int)popup->button_count;
        x = padding;
        for (i = 0; i < (int)popup->button_count; i++)
        {
            SetRect(&popup->buttons[i].rect, x, y, x + button_width, y + row);
            x += button_width + gap;
        }
        y += row;
    }
    popup->height = y + padding;
    SetRect(&popup->close_rect, popup->width - padding - scale(popup, 20), hero_height(popup) + padding / 2,
            popup->width - padding / 2, hero_height(popup) + padding / 2 + scale(popup, 24));
    ReleaseDC(NULL, dc);
}

static void layout_stack(void)
{
    struct popup *popup;
    RECT work;
    int y;

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    y = work.bottom;
    LIST_FOR_EACH_ENTRY_REV(popup, &visible_popups, struct popup, entry)
    {
        y -= scale(popup, TOAST_MARGIN) + popup->height;
        SetWindowPos(popup->hwnd, HWND_TOPMOST, work.right - scale(popup, TOAST_MARGIN) - popup->width, y,
                     popup->width, popup->height, SWP_NOACTIVATE);
        y += scale(popup, TOAST_MARGIN) - scale(popup, TOAST_SPACING);
    }
}

static void draw_text(HDC dc, HFONT font, COLORREF color, const WCHAR *text, RECT *rect, UINT max_lines, UINT flags)
{
    TEXTMETRICW metrics;
    RECT bounds = *rect;
    HFONT old;

    if (!text || !text[0])
        return;
    old = SelectObject(dc, font);
    GetTextMetricsW(dc, &metrics);
    bounds.bottom = bounds.top + metrics.tmHeight * max_lines;
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &bounds, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS | flags);
    bounds = *rect;
    DrawTextW(dc, text, -1, &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    rect->top += min(bounds.bottom - bounds.top, metrics.tmHeight * (int)max_lines);
    SelectObject(dc, old);
}

static void draw_bitmap(HDC dc, HBITMAP bitmap, int x, int y)
{
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC source = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(source, bitmap);
    BITMAP info;

    GetObjectW(bitmap, sizeof(info), &info);
    GdiAlphaBlend(dc, x, y, info.bmWidth, info.bmHeight, source, 0, 0, info.bmWidth, info.bmHeight, blend);
    SelectObject(source, old);
    DeleteDC(source);
}

static void paint_popup(struct popup *popup, HDC target)
{
    COLORREF background, border, text, secondary, button;
    int padding = scale(popup, TOAST_PADDING), spacing = scale(popup, TOAST_SPACING);
    RECT client, rect;
    HBITMAP bitmap;
    HBRUSH brush;
    HGDIOBJ old;
    HDC dc;
    UINT i;
    int top, x;

    GetClientRect(popup->hwnd, &client);
    dc = CreateCompatibleDC(target);
    bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    old = SelectObject(dc, bitmap);
    colors(popup, &background, &border, &text, &secondary, &button);
    SetBkMode(dc, TRANSPARENT);

    brush = CreateSolidBrush(background);
    FillRect(dc, &client, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush(border);
    FrameRect(dc, &client, brush);
    DeleteObject(brush);

    if (popup->hero)
        draw_bitmap(dc, popup->hero, 0, 0);
    top = hero_height(popup) + padding;
    x = padding;
    if (popup->app_icon)
    {
        DrawIconEx(dc, x, top + (scale(popup, TOAST_HEADER) - scale(popup, 16)) / 2, popup->app_icon,
                   scale(popup, 16), scale(popup, 16), 0, NULL, DI_NORMAL);
        x += scale(popup, 16) + spacing;
    }
    SetRect(&rect, x, top, popup->close_rect.left - spacing, top + scale(popup, TOAST_HEADER));
    draw_text(dc, popup->caption_font, secondary, popup->identity.display_name, &rect, 1, DT_SINGLELINE | DT_VCENTER);
    rect = popup->close_rect;
    SelectObject(dc, popup->glyph_font);
    SetTextColor(dc, secondary);
    DrawTextW(dc, L"r", 1, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    top += scale(popup, TOAST_HEADER) + spacing;
    if (popup->logo)
        draw_bitmap(dc, popup->logo, padding, top);
    SetRect(&rect, content_left(popup), top, client.right - padding, client.bottom);
    draw_text(dc, popup->title_font, text, popup->title, &rect, TOAST_TITLE_LINES, 0);
    draw_text(dc, popup->body_font, secondary, popup->body, &rect, TOAST_BODY_LINES, 0);
    draw_text(dc, popup->caption_font, secondary, popup->attribution, &rect, 1, 0);
    if (popup->progress.present)
    {
        RECT bar, line = rect;
        WCHAR percent[16];

        rect.top += spacing;
        line = rect;
        draw_text(dc, popup->caption_font, secondary, popup->progress.title, &line, 1, DT_LEFT);
        if (popup->progress.value_text)
            draw_text(dc, popup->caption_font, secondary, popup->progress.value_text, &rect, 1, DT_RIGHT);
        else if (!popup->progress.indeterminate)
        {
            swprintf(percent, ARRAYSIZE(percent), L"%u%%", (UINT)(popup->progress.value * 100.0 + 0.5));
            draw_text(dc, popup->caption_font, secondary, percent, &rect, 1, DT_RIGHT);
        }
        else
            rect.top = line.top;
        SetRect(&bar, rect.left, rect.top + scale(popup, 4), rect.right, rect.top + scale(popup, 8));
        brush = CreateSolidBrush(button);
        FillRect(dc, &bar, brush);
        DeleteObject(brush);
        if (!popup->progress.indeterminate)
            bar.right = bar.left + (LONG)((bar.right - bar.left) * popup->progress.value);
        brush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        FillRect(dc, &bar, brush);
        DeleteObject(brush);
        rect.top = bar.bottom + scale(popup, 4);
        draw_text(dc, popup->caption_font, secondary, popup->progress.status, &rect, 1, 0);
    }

    for (i = 0; i < popup->button_count; i++)
    {
        rect = popup->buttons[i].rect;
        brush = CreateSolidBrush(button);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        SelectObject(dc, popup->body_font);
        SetTextColor(dc, text);
        DrawTextW(dc, popup->buttons[i].content ? popup->buttons[i].content : L"", -1, &rect,
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

static void start_timer(struct popup *popup)
{
    if (!popup->persistent || !popup->button_count)
        SetTimer(popup->hwnd, TIMER_DISMISS, popup->duration, NULL);
}

static void create_fields(struct popup *popup)
{
    COLORREF background, border, text, secondary, button;
    UINT i, j;

    colors(popup, &background, &border, &text, &secondary, &button);
    if (!popup->field_brush)
        popup->field_brush = CreateSolidBrush(button);
    for (i = 0; i < popup->field_count; i++)
    {
        struct toast_field *field = &popup->fields[i];

        if (field->selection)
        {
            field->control = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                             0, 0, 0, 0, popup->hwnd, NULL, wpnapps_instance, NULL);
            for (j = 0; field->control && j < field->choice_count; j++)
                SendMessageW(field->control, CB_ADDSTRING, 0, (LPARAM)(field->choice_ids[j] ? field->choice_ids[j] : L""));
            if (field->control && field->choice_count)
                SendMessageW(field->control, CB_SETCURSEL, 0, 0);
        }
        else
        {
            field->control = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                             0, 0, 0, 0, popup->hwnd, NULL, wpnapps_instance, NULL);
        }
        if (field->control)
            SendMessageW(field->control, WM_SETFONT, (WPARAM)popup->body_font, FALSE);
    }
}

static struct popup *find_popup(struct list *list, const WCHAR *key)
{
    struct popup *popup;

    LIST_FOR_EACH_ENTRY(popup, list, struct popup, entry)
    {
        if (!wcscmp(popup->key, key))
            return popup;
    }
    return NULL;
}

static void show_next_pending(void);

static void close_popup(struct popup *popup)
{
    BOOL visible = popup->hwnd != NULL;

    list_remove(&popup->entry);
    if (popup->hwnd)
    {
        KillTimer(popup->hwnd, TIMER_DISMISS);
        SetWindowLongPtrW(popup->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(popup->hwnd);
        popup->hwnd = NULL;
    }
    free_popup(popup);
    if (visible)
    {
        show_next_pending();
        layout_stack();
    }
}

static void collect_inputs(struct popup *popup, struct toast_input *inputs, ULONG *count)
{
    UINT i;
    int length, selected;

    *count = 0;
    for (i = 0; i < popup->field_count; i++)
    {
        struct toast_field *field = &popup->fields[i];

        if (!field->control || !field->id)
            continue;
        inputs[*count].id = field->id;
        inputs[*count].value = NULL;
        if (field->selection)
        {
            selected = (int)SendMessageW(field->control, CB_GETCURSEL, 0, 0);
            if (selected >= 0 && (UINT)selected < field->choice_count && field->choice_ids[selected])
                inputs[*count].value = wcsdup(field->choice_ids[selected]);
        }
        else if ((length = GetWindowTextLengthW(field->control)) >= 0 &&
                 (inputs[*count].value = calloc(length + 1, sizeof(WCHAR))))
        {
            GetWindowTextW(field->control, inputs[*count].value, length + 1);
        }
        (*count)++;
    }
}

static void finish_popup(struct popup *popup, ToastDismissalReason reason, BOOL remove)
{
    WCHAR key[STORE_NAME_LENGTH], *app_id = popup->app_id;

    wcscpy(key, popup->key);
    popup->app_id = NULL;
    close_popup(popup);
    history_post_event(key, HISTORY_EVENT_DISMISSED, reason, NULL, NULL, 0);
    if (remove)
        history_entry_delete(app_id, key);
    else
        history_entry_popup_done(app_id, key);
    free(app_id);
}

static void activate_popup(struct popup *popup, const WCHAR *arguments, const WCHAR *activation_type)
{
    struct toast_input inputs[TOAST_MAX_INPUTS];
    WCHAR key[STORE_NAME_LENGTH], *app_id = popup->app_id;
    ULONG count, i;

    if (activation_type && !wcsicmp(activation_type, L"system"))
    {
        finish_popup(popup, ToastDismissalReason_UserCanceled, TRUE);
        return;
    }
    collect_inputs(popup, inputs, &count);
    wcscpy(key, popup->key);
    if (activation_type && !wcsicmp(activation_type, L"protocol") && arguments)
    {
        ShellExecuteW(NULL, L"open", arguments, NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        history_post_event(key, HISTORY_EVENT_ACTIVATED, 0, arguments ? arguments : L"", inputs, count);
        activation_invoke(app_id, arguments, inputs, count);
    }
    for (i = 0; i < count; i++)
        free(inputs[i].value);
    popup->app_id = NULL;
    close_popup(popup);
    history_entry_delete(app_id, key);
    free(app_id);
}

static LRESULT CALLBACK popup_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct popup *popup = (struct popup *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    COLORREF background, border, text, secondary, button;
    TRACKMOUSEEVENT track;
    PAINTSTRUCT paint;
    POINT point;
    UINT i;

    switch (message)
    {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lparam)->lpCreateParams);
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        BeginPaint(hwnd, &paint);
        if (popup)
            paint_popup(popup, paint.hdc);
        EndPaint(hwnd, &paint);
        return 0;

    case WM_MOUSEACTIVATE:
        return popup && popup->field_count ? MA_ACTIVATE : MA_NOACTIVATE;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (popup)
        {
            colors(popup, &background, &border, &text, &secondary, &button);
            SetTextColor((HDC)wparam, text);
            SetBkColor((HDC)wparam, button);
            return (LRESULT)popup->field_brush;
        }
        break;

    case WM_MOUSEMOVE:
        if (popup && !popup->hover)
        {
            popup->hover = TRUE;
            KillTimer(hwnd, TIMER_DISMISS);
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            track.dwHoverTime = 0;
            TrackMouseEvent(&track);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (popup)
        {
            popup->hover = FALSE;
            if (GetFocus() == NULL || GetParent(GetFocus()) != hwnd)
                start_timer(popup);
        }
        return 0;

    case WM_TIMER:
        if (popup && wparam == TIMER_DISMISS)
            finish_popup(popup, ToastDismissalReason_TimedOut, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (!popup)
            return 0;
        point.x = (short)LOWORD(lparam);
        point.y = (short)HIWORD(lparam);
        if (PtInRect(&popup->close_rect, point))
        {
            finish_popup(popup, ToastDismissalReason_UserCanceled, TRUE);
            return 0;
        }
        for (i = 0; i < popup->button_count; i++)
        {
            if (PtInRect(&popup->buttons[i].rect, point))
            {
                WCHAR *arguments = popup->buttons[i].arguments ? wcsdup(popup->buttons[i].arguments) : NULL;
                WCHAR *type = popup->buttons[i].activation_type ? wcsdup(popup->buttons[i].activation_type) : NULL;

                activate_popup(popup, arguments, type);
                free(arguments);
                free(type);
                return 0;
            }
        }
        {
            WCHAR *arguments = popup->launch ? wcsdup(popup->launch) : NULL;
            WCHAR *type = popup->activation_type ? wcsdup(popup->activation_type) : NULL;

            activate_popup(popup, arguments, type);
            free(arguments);
            free(type);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static UINT message_duration(void)
{
    DWORD seconds = 5, value, size = sizeof(value);

    if (!RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Accessibility", L"MessageDuration", RRF_RT_REG_DWORD,
                      NULL, &value, &size) && value)
        seconds = value;
    return seconds * 1000;
}

static BOOL system_dark_mode(void)
{
    DWORD value = 0, size = sizeof(value);

    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL, &value, &size))
        return TRUE;
    return !value;
}

static void play_audio(struct popup *popup)
{
    const WCHAR *alias = L"Notification.Default";

    if (popup->silent)
        return;
    if (popup->audio && !wcsnicmp(popup->audio, L"ms-winsoundevent:", 17))
        alias = popup->audio + 17;
    PlaySoundW(alias, NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
}

static void display_popup(struct popup *popup)
{
    popup->hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, popup_class, popup->title,
                                  WS_POPUP | WS_CLIPCHILDREN, 0, 0, 0, 0, NULL, NULL, wpnapps_instance, popup);
    if (!popup->hwnd)
    {
        HRESULT error = HRESULT_FROM_WIN32(GetLastError());

        list_remove(&popup->entry);
        history_post_event(popup->key, HISTORY_EVENT_FAILED, (DWORD)error, NULL, NULL, 0);
        history_entry_popup_done(popup->app_id, popup->key);
        free_popup(popup);
        return;
    }
    create_fields(popup);
    layout_popup(popup);
    layout_stack();
    ShowWindow(popup->hwnd, SW_SHOWNOACTIVATE);
    play_audio(popup);
    start_timer(popup);
}

static void show_next_pending(void)
{
    struct popup *popup;

    while (list_count(&visible_popups) < TOAST_MAX_VISIBLE && !list_empty(&pending_popups))
    {
        popup = LIST_ENTRY(list_head(&pending_popups), struct popup, entry);
        list_remove(&popup->entry);
        list_add_tail(&visible_popups, &popup->entry);
        display_popup(popup);
    }
}

static void refresh_popup(struct popup *popup)
{
    free_content(popup);
    parse_content(popup);
    if (popup->hwnd)
    {
        create_fields(popup);
        layout_popup(popup);
        layout_stack();
        InvalidateRect(popup->hwnd, NULL, FALSE);
    }
}

static void sync_popups(struct list *list)
{
    struct popup *popup, *next;
    INotificationData *data;
    DWORD revision;

    LIST_FOR_EACH_ENTRY_SAFE(popup, next, list, struct popup, entry)
    {
        if (!history_entry_exists(popup->app_id, popup->key))
        {
            close_popup(popup);
            continue;
        }
        if ((data = history_entry_data(popup->app_id, popup->key, &revision)))
            INotificationData_Release(data);
        if (revision != popup->data_revision)
            refresh_popup(popup);
    }
}

static void show_entry(const WCHAR *app_id, const WCHAR *key, void *context)
{
    struct app_identity identity;
    IToastNotification *toast;
    struct popup *popup;
    UINT icon_count;

    if ((popup = find_popup(&visible_popups, key)) || (popup = find_popup(&pending_popups, key)))
    {
        refresh_popup(popup);
        return;
    }
    if (!app_identity_lookup(app_id, &identity) || !(toast = history_entry_restore(app_id, key)))
    {
        history_entry_popup_done(app_id, key);
        return;
    }
    if (!(popup = calloc(1, sizeof(*popup))) || !(popup->app_id = wcsdup(app_id)))
    {
        free(popup);
        IToastNotification_Release(toast);
        history_entry_popup_done(app_id, key);
        return;
    }
    wcscpy(popup->key, key);
    popup->toast = toast;
    popup->identity = identity;
    popup->dpi = GetDpiForSystem();
    if (!popup->dpi)
        popup->dpi = 96;
    popup->dark = system_dark_mode();
    popup->duration = message_duration();
    if (identity.icon_path[0])
    {
        icon_count = ExtractIconExW(identity.icon_path, identity.icon_index, NULL, &popup->app_icon, 1);
        if (!icon_count)
            popup->app_icon = NULL;
    }
    create_fonts(popup);
    parse_content(popup);
    if (list_count(&visible_popups) < TOAST_MAX_VISIBLE)
    {
        list_add_tail(&visible_popups, &popup->entry);
        display_popup(popup);
    }
    else
    {
        list_add_tail(&pending_popups, &popup->entry);
    }
}

static void host_sync(void)
{
    history_collect_events();
    sync_popups(&visible_popups);
    sync_popups(&pending_popups);
    history_scan_popups(show_entry, NULL);
    schedule_host_scan(presenter_window, TIMER_SCHEDULE);
}

static LRESULT CALLBACK host_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_TIMER:
        if (wparam == TIMER_SCHEDULE)
            schedule_host_scan(hwnd, TIMER_SCHEDULE);
        return 0;

    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
        layout_stack();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void presenter_run(void)
{
    WNDCLASSEXW host = {sizeof(host)}, popup = {sizeof(popup)};
    HANDLE watch_event;
    HKEY watch_root = NULL;
    BOOL watching = FALSE;
    HRESULT init;
    MSG message;
    DWORD wait;

    init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    host.lpfnWndProc = host_proc;
    host.hInstance = wpnapps_instance;
    host.lpszClassName = host_class;
    RegisterClassExW(&host);
    popup.lpfnWndProc = popup_proc;
    popup.hInstance = wpnapps_instance;
    popup.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    popup.lpszClassName = popup_class;
    popup.style = CS_DROPSHADOW;
    RegisterClassExW(&popup);
    presenter_window = CreateWindowExW(0, host_class, NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL, wpnapps_instance, NULL);
    watch_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (presenter_window)
    {
        watching = watch_event && SUCCEEDED(store_watch(watch_event, &watch_root));
        host_sync();
        for (;;)
        {
            wait = MsgWaitForMultipleObjects(watching ? 1 : 0, &watch_event, FALSE, INFINITE, QS_ALLINPUT);
            if (watching && wait == WAIT_OBJECT_0)
            {
                watching = SUCCEEDED(store_watch(watch_event, &watch_root));
                host_sync();
                continue;
            }
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                    goto done;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
done:
    if (watch_root)
        RegCloseKey(watch_root);
    if (watch_event)
        CloseHandle(watch_event);
    if (SUCCEEDED(init))
        CoUninitialize();
}
