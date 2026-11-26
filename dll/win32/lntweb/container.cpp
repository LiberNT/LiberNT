/*
 * PROJECT:     LiberNT Web View
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     litehtml document container: GDI text, plutovg shapes, WIC and SVG images
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntwebp.h"

#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <lunasvg.h>
#include <litehtml/url.h>
#include <cmath>
#include <cstring>

namespace lntweb
{

std::wstring
Utf8ToWide(const std::string &Text)
{
    if (Text.empty())
        return std::wstring();
    int Length = MultiByteToWideChar(CP_UTF8, 0, Text.data(), (int)Text.size(), nullptr, 0);
    std::wstring Wide(Length > 0 ? Length : 0, L'\0');
    if (Length > 0)
        MultiByteToWideChar(CP_UTF8, 0, Text.data(), (int)Text.size(), &Wide[0], Length);
    return Wide;
}

std::string
WideToUtf8(const std::wstring &Text)
{
    if (Text.empty())
        return std::string();
    int Length = WideCharToMultiByte(CP_UTF8, 0, Text.data(), (int)Text.size(), nullptr, 0, nullptr, nullptr);
    std::string Utf8(Length > 0 ? Length : 0, '\0');
    if (Length > 0)
        WideCharToMultiByte(CP_UTF8, 0, Text.data(), (int)Text.size(), &Utf8[0], Length, nullptr, nullptr);
    return Utf8;
}

static float
Channel(litehtml::byte Value)
{
    return Value / 255.0f;
}

static void
SetColor(plutovg_canvas_t *Canvas, const litehtml::web_color &Color)
{
    plutovg_canvas_set_rgba(Canvas, Channel(Color.red), Channel(Color.green), Channel(Color.blue), Channel(Color.alpha));
}

static void
RoundedRect(plutovg_canvas_t *Canvas, const litehtml::position &Box, const litehtml::border_radiuses &Radius)
{
    const float k = 0.5522847498f;
    float x = Box.x, y = Box.y, w = Box.width, h = Box.height;
    float tlx = std::min(Radius.top_left_x, w / 2), tly = std::min(Radius.top_left_y, h / 2);
    float trx = std::min(Radius.top_right_x, w / 2), try_ = std::min(Radius.top_right_y, h / 2);
    float brx = std::min(Radius.bottom_right_x, w / 2), bry = std::min(Radius.bottom_right_y, h / 2);
    float blx = std::min(Radius.bottom_left_x, w / 2), bly = std::min(Radius.bottom_left_y, h / 2);

    if (tlx <= 0 && tly <= 0 && trx <= 0 && try_ <= 0 && brx <= 0 && bry <= 0 && blx <= 0 && bly <= 0)
    {
        plutovg_canvas_rect(Canvas, x, y, w, h);
        return;
    }
    plutovg_canvas_move_to(Canvas, x + tlx, y);
    plutovg_canvas_line_to(Canvas, x + w - trx, y);
    if (trx > 0 || try_ > 0)
        plutovg_canvas_cubic_to(Canvas, x + w - trx + trx * k, y, x + w, y + try_ - try_ * k, x + w, y + try_);
    plutovg_canvas_line_to(Canvas, x + w, y + h - bry);
    if (brx > 0 || bry > 0)
        plutovg_canvas_cubic_to(Canvas, x + w, y + h - bry + bry * k, x + w - brx + brx * k, y + h, x + w - brx, y + h);
    plutovg_canvas_line_to(Canvas, x + blx, y + h);
    if (blx > 0 || bly > 0)
        plutovg_canvas_cubic_to(Canvas, x + blx - blx * k, y + h, x, y + h - bly + bly * k, x, y + h - bly);
    plutovg_canvas_line_to(Canvas, x, y + tly);
    if (tlx > 0 || tly > 0)
        plutovg_canvas_cubic_to(Canvas, x, y + tly - tly * k, x + tlx - tlx * k, y, x + tlx, y);
    plutovg_canvas_close_path(Canvas);
}

Container::Container(const LNTWEB_HOST &Host, const std::string &Url, int ViewportWidth, int ViewportHeight, int Dpi)
    : m_Host(Host), m_DocumentUrl(Url), m_BaseUrl(Url), m_Cursor(LoadCursorW(nullptr, IDC_ARROW)),
      m_ViewportWidth(ViewportWidth), m_ViewportHeight(ViewportHeight), m_Dpi(Dpi)
{
    NONCLIENTMETRICSW Metrics = { sizeof(Metrics) };

    m_MeasureDc = CreateCompatibleDC(nullptr);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
        m_DefaultFont = WideToUtf8(Metrics.lfMessageFont.lfFaceName);
    if (m_DefaultFont.empty())
        m_DefaultFont = "Tahoma";
}

Container::~Container()
{
    for (auto &Entry : m_Images)
    {
        if (Entry.second.Surface)
            plutovg_surface_destroy(Entry.second.Surface);
    }
    if (m_MeasureDc)
        DeleteDC(m_MeasureDc);
}

std::string
Container::Resolve(const std::string &Reference, const std::string &Base) const
{
    litehtml::url BaseUrl(Base.empty() ? m_BaseUrl : Base);
    litehtml::url Relative(Reference);
    return litehtml::resolve(BaseUrl, Relative).str();
}

bool
Container::Fetch(const std::string &Url, std::string &Data)
{
    PSTR Buffer = nullptr;
    SIZE_T Length = 0;

    if (!m_Host.Fetch || (Url.compare(0, 7, "http://") != 0 && Url.compare(0, 8, "https://") != 0))
        return false;
    if (!m_Host.Fetch(m_Host.Context, Url.c_str(), &Buffer, &Length) || !Buffer)
        return false;
    Data.assign(Buffer, Length);
    if (m_Host.Free)
        m_Host.Free(m_Host.Context, Buffer);
    return true;
}

static int CALLBACK
FaceExists(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM Found)
{
    *(BOOL *)Found = TRUE;
    return 0;
}

std::wstring
Container::PickFace(const std::string &Families)
{
    auto Cached = m_Faces.find(Families);
    if (Cached != m_Faces.end())
        return Cached->second;

    std::wstring Result = Utf8ToWide(m_DefaultFont);
    size_t Start = 0;
    while (Start <= Families.size())
    {
        size_t End = Families.find(',', Start);
        std::string Name = Families.substr(Start, End == std::string::npos ? std::string::npos : End - Start);
        Start = (End == std::string::npos) ? Families.size() + 1 : End + 1;

        while (!Name.empty() && (Name.front() == ' ' || Name.front() == '"' || Name.front() == '\''))
            Name.erase(0, 1);
        while (!Name.empty() && (Name.back() == ' ' || Name.back() == '"' || Name.back() == '\''))
            Name.pop_back();
        if (Name.empty())
            continue;

        std::string Lower = Name;
        for (auto &Character : Lower)
            Character = (char)tolower((unsigned char)Character);
        if (Lower == "serif")
        {
            Result = L"Times New Roman";
            break;
        }
        if (Lower == "monospace")
        {
            Result = L"Courier New";
            break;
        }
        if (Lower == "sans-serif" || Lower == "system-ui" || Lower == "-apple-system" || Lower == "blinkmacsystemfont" ||
            Lower == "ui-sans-serif")
        {
            break;
        }

        LOGFONTW Query = {};
        BOOL Found = FALSE;
        std::wstring Face = Utf8ToWide(Name);
        if (Face.size() >= LF_FACESIZE)
            continue;
        wcscpy(Query.lfFaceName, Face.c_str());
        Query.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(m_MeasureDc, &Query, FaceExists, (LPARAM)&Found, 0);
        if (Found)
        {
            Result = Face;
            break;
        }
    }
    m_Faces[Families] = Result;
    return Result;
}

litehtml::uint_ptr
Container::create_font(const litehtml::font_description &Description, const litehtml::document *,
                       litehtml::font_metrics *Metrics)
{
    LOGFONTW Font = {};
    TEXTMETRICW Text = {};
    SIZE Zero = {}, X = {};
    std::wstring Face = PickFace(Description.family);

    Font.lfHeight = -(LONG)std::lround(Description.size);
    Font.lfWeight = Description.weight;
    Font.lfItalic = Description.style != litehtml::font_style_normal;
    Font.lfUnderline = (Description.decoration_line & litehtml::text_decoration_line_underline) != 0;
    Font.lfStrikeOut = (Description.decoration_line & litehtml::text_decoration_line_line_through) != 0;
    Font.lfCharSet = DEFAULT_CHARSET;
    Font.lfQuality = ANTIALIASED_QUALITY;
    wcsncpy(Font.lfFaceName, Face.c_str(), LF_FACESIZE - 1);

    HFONT Handle = CreateFontIndirectW(&Font);
    if (!Handle)
        return 0;

    HGDIOBJ Old = SelectObject(m_MeasureDc, Handle);
    GetTextMetricsW(m_MeasureDc, &Text);
    GetTextExtentPoint32W(m_MeasureDc, L"0", 1, &Zero);
    GetTextExtentPoint32W(m_MeasureDc, L"x", 1, &X);
    SelectObject(m_MeasureDc, Old);

    if (Metrics)
    {
        Metrics->font_size = Description.size;
        Metrics->height = (litehtml::pixel_t)Text.tmHeight;
        Metrics->ascent = (litehtml::pixel_t)Text.tmAscent;
        Metrics->descent = (litehtml::pixel_t)Text.tmDescent;
        Metrics->x_height = std::round(Text.tmAscent * 0.55f);
        Metrics->ch_width = (litehtml::pixel_t)Zero.cx;
        Metrics->draw_spaces = Font.lfUnderline || Font.lfStrikeOut;
        Metrics->sub_shift = Description.size / 5;
        Metrics->super_shift = Description.size / 3;
    }
    m_Descents[(litehtml::uint_ptr)Handle] = Text.tmDescent;
    return (litehtml::uint_ptr)Handle;
}

void
Container::delete_font(litehtml::uint_ptr Font)
{
    m_Descents.erase(Font);
    DeleteObject((HFONT)Font);
}

litehtml::pixel_t
Container::text_width(const char *Text, litehtml::uint_ptr Font)
{
    std::wstring Wide = Utf8ToWide(Text);
    SIZE Size = {};
    HGDIOBJ Old = SelectObject(m_MeasureDc, (HFONT)Font);

    GetTextExtentPoint32W(m_MeasureDc, Wide.c_str(), (int)Wide.size(), &Size);
    SelectObject(m_MeasureDc, Old);
    return (litehtml::pixel_t)Size.cx;
}

void
Container::draw_text(litehtml::uint_ptr Hdc, const char *Text, litehtml::uint_ptr Font, litehtml::web_color Color,
                     const litehtml::position &Position)
{
    DrawContext *Context = (DrawContext *)Hdc;
    std::wstring Wide = Utf8ToWide(Text);
    auto Descent = m_Descents.find(Font);

    if (Color.alpha == 0 || Wide.empty())
        return;
    HGDIOBJ Old = SelectObject(Context->Dc, (HFONT)Font);
    SetTextColor(Context->Dc, RGB(Color.red, Color.green, Color.blue));
    SetBkMode(Context->Dc, TRANSPARENT);
    SetTextAlign(Context->Dc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);
    ExtTextOutW(Context->Dc, (int)std::lround(Position.x),
                (int)std::lround(Position.bottom()) - (Descent != m_Descents.end() ? Descent->second : 0), 0, nullptr,
                Wide.c_str(), (UINT)Wide.size(), nullptr);
    SelectObject(Context->Dc, Old);
}

litehtml::pixel_t
Container::pt_to_px(float Points) const
{
    return Points * m_Dpi / 72.0f;
}

litehtml::pixel_t
Container::get_default_font_size() const
{
    return 16.0f * m_Dpi / 96.0f;
}

const char *
Container::get_default_font_name() const
{
    return m_DefaultFont.c_str();
}

void
Container::draw_list_marker(litehtml::uint_ptr Hdc, const litehtml::list_marker &Marker)
{
    GdiFlush();
    DrawContext *Context = (DrawContext *)Hdc;
    plutovg_canvas_t *Canvas = Context->Canvas;
    const litehtml::position &Box = Marker.pos;

    if (!Marker.image.empty())
    {
        litehtml::background_layer Layer;
        Layer.border_box = Layer.clip_box = Layer.origin_box = Box;
        Layer.repeat = litehtml::background_repeat_no_repeat;
        draw_image(Hdc, Layer, Marker.image, Marker.baseurl ? Marker.baseurl : "");
        return;
    }

    plutovg_canvas_save(Canvas);
    SetColor(Canvas, Marker.color);
    switch (Marker.marker_type)
    {
        case litehtml::list_style_type_circle:
            plutovg_canvas_ellipse(Canvas, Box.x + Box.width / 2, Box.y + Box.height / 2, Box.width / 2 - 0.5f,
                                   Box.height / 2 - 0.5f);
            plutovg_canvas_set_line_width(Canvas, 1.0f);
            plutovg_canvas_stroke(Canvas);
            break;
        case litehtml::list_style_type_square:
            plutovg_canvas_fill_rect(Canvas, Box.x, Box.y, Box.width, Box.height);
            break;
        case litehtml::list_style_type_none:
            break;
        case litehtml::list_style_type_disc:
            plutovg_canvas_ellipse(Canvas, Box.x + Box.width / 2, Box.y + Box.height / 2, Box.width / 2, Box.height / 2);
            plutovg_canvas_fill(Canvas);
            break;
        default:
            if (Marker.font)
            {
                std::string Text = std::to_string(Marker.index) + ".";
                litehtml::pixel_t Width = text_width(Text.c_str(), Marker.font);
                litehtml::position Position(Box.right() - Width, Box.y, Width, Box.height);
                draw_text(Hdc, Text.c_str(), Marker.font, Marker.color, Position);
            }
            break;
    }
    plutovg_canvas_restore(Canvas);
}

static bool
LooksLikeSvg(const std::string &Data, const std::string &Url)
{
    size_t Index = 0;

    while (Index < Data.size() && Index < 512 && (Data[Index] == ' ' || Data[Index] == '\r' || Data[Index] == '\n' ||
                                                   Data[Index] == '\t' || (unsigned char)Data[Index] == 0xEF ||
                                                   (unsigned char)Data[Index] == 0xBB || (unsigned char)Data[Index] == 0xBF))
    {
        Index++;
    }
    if (Data.compare(Index, 4, "<svg") == 0)
        return true;
    if (Data.compare(Index, 5, "<?xml") == 0 && Data.find("<svg", Index) != std::string::npos)
        return true;
    std::string Path = Url.substr(0, Url.find_first_of("?#"));
    return Path.size() > 4 && Path.compare(Path.size() - 4, 4, ".svg") == 0;
}

static Image
DecodeSvg(const std::string &Data, int Dpi)
{
    Image Result;
    auto Document = lunasvg::Document::loadFromData(Data.data(), Data.size());
    if (!Document)
        return Result;

    float Width = Document->width(), Height = Document->height();
    if (Width <= 0 || Height <= 0)
    {
        Width = 300;
        Height = 150;
    }
    Result.Width = (int)std::ceil(Width);
    Result.Height = (int)std::ceil(Height);

    (void)Dpi;
    lunasvg::Bitmap Bitmap = Document->renderToBitmap(Result.Width * 2, Result.Height * 2);
    if (Bitmap.isNull())
        return Image();
    Result.Surface = plutovg_surface_create(Bitmap.width(), Bitmap.height());
    if (!Result.Surface)
        return Image();
    for (int Row = 0; Row < Bitmap.height(); Row++)
    {
        memcpy(plutovg_surface_get_data(Result.Surface) + Row * plutovg_surface_get_stride(Result.Surface),
               Bitmap.data() + Row * Bitmap.stride(), Bitmap.width() * 4);
    }
    return Result;
}

static Image
DecodeRaster(const std::string &Data)
{
    Image Result;
    IWICImagingFactory *Factory = nullptr;
    IStream *Stream = nullptr;
    IWICBitmapDecoder *Decoder = nullptr;
    IWICBitmapFrameDecode *Frame = nullptr;
    IWICFormatConverter *Converter = nullptr;
    UINT Width = 0, Height = 0;

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                (void **)&Factory)))
    {
        return Result;
    }
    Stream = SHCreateMemStream((const BYTE *)Data.data(), (UINT)Data.size());
    if (Stream && SUCCEEDED(Factory->CreateDecoderFromStream(Stream, nullptr, WICDecodeMetadataCacheOnDemand, &Decoder)) &&
        SUCCEEDED(Decoder->GetFrame(0, &Frame)) && SUCCEEDED(Factory->CreateFormatConverter(&Converter)) &&
        SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(Converter->GetSize(&Width, &Height)) && Width > 0 && Height > 0 && Width <= 16384 && Height <= 16384)
    {
        Result.Surface = plutovg_surface_create((int)Width, (int)Height);
        if (Result.Surface &&
            SUCCEEDED(Converter->CopyPixels(nullptr, (UINT)plutovg_surface_get_stride(Result.Surface),
                                            (UINT)(plutovg_surface_get_stride(Result.Surface) * Height),
                                            plutovg_surface_get_data(Result.Surface))))
        {
            Result.Width = (int)Width;
            Result.Height = (int)Height;
        }
        else if (Result.Surface)
        {
            plutovg_surface_destroy(Result.Surface);
            Result.Surface = nullptr;
        }
    }
    if (Converter)
        Converter->Release();
    if (Frame)
        Frame->Release();
    if (Decoder)
        Decoder->Release();
    if (Stream)
        Stream->Release();
    Factory->Release();
    return Result;
}

void
Container::load_image(const char *Source, const char *BaseUrl, bool)
{
    std::string Url = Resolve(Source ? Source : "", BaseUrl ? BaseUrl : "");
    std::string Data;

    if (m_Images.count(Url))
        return;
    Image Result;
    if (Url.compare(0, 5, "data:") != 0 && Fetch(Url, Data) && !Data.empty())
        Result = LooksLikeSvg(Data, Url) ? DecodeSvg(Data, m_Dpi) : DecodeRaster(Data);
    m_Images[Url] = Result;
}

const Image *
Container::FindImage(const std::string &Url) const
{
    auto Entry = m_Images.find(Url);
    if (Entry == m_Images.end() || !Entry->second.Surface)
        return nullptr;
    return &Entry->second;
}

void
Container::get_image_size(const char *Source, const char *BaseUrl, litehtml::size &Size)
{
    const Image *Found = FindImage(Resolve(Source ? Source : "", BaseUrl ? BaseUrl : ""));

    Size.width = Found ? (litehtml::pixel_t)Found->Width : 0;
    Size.height = Found ? (litehtml::pixel_t)Found->Height : 0;
}

void
Container::draw_image(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer, const std::string &Url,
                      const std::string &BaseUrl)
{
    GdiFlush();
    DrawContext *Context = (DrawContext *)Hdc;
    plutovg_canvas_t *Canvas = Context->Canvas;
    const Image *Found = FindImage(Resolve(Url, BaseUrl));
    const litehtml::position &Origin = Layer.origin_box;
    plutovg_matrix_t Matrix;

    if (!Found || Origin.width <= 0 || Origin.height <= 0)
        return;

    plutovg_canvas_save(Canvas);
    plutovg_canvas_clip_rect(Canvas, Layer.clip_box.x, Layer.clip_box.y, Layer.clip_box.width, Layer.clip_box.height);
    RoundedRect(Canvas, Layer.border_box, Layer.border_radius);
    plutovg_canvas_clip(Canvas);

    plutovg_matrix_init_translate(&Matrix, Origin.x, Origin.y);
    plutovg_matrix_scale(&Matrix, Origin.width / plutovg_surface_get_width(Found->Surface),
                         Origin.height / plutovg_surface_get_height(Found->Surface));
    plutovg_canvas_set_texture(Canvas, Found->Surface,
                               Layer.repeat == litehtml::background_repeat_no_repeat ? PLUTOVG_TEXTURE_TYPE_PLAIN
                                                                                     : PLUTOVG_TEXTURE_TYPE_TILED,
                               1.0f, &Matrix);
    switch (Layer.repeat)
    {
        case litehtml::background_repeat_no_repeat:
            plutovg_canvas_fill_rect(Canvas, Origin.x, Origin.y, Origin.width, Origin.height);
            break;
        case litehtml::background_repeat_repeat_x:
            plutovg_canvas_fill_rect(Canvas, Layer.clip_box.x, Origin.y, Layer.clip_box.width, Origin.height);
            break;
        case litehtml::background_repeat_repeat_y:
            plutovg_canvas_fill_rect(Canvas, Origin.x, Layer.clip_box.y, Origin.width, Layer.clip_box.height);
            break;
        default:
            plutovg_canvas_fill_rect(Canvas, Layer.clip_box.x, Layer.clip_box.y, Layer.clip_box.width,
                                     Layer.clip_box.height);
            break;
    }
    plutovg_canvas_restore(Canvas);
}

void
Container::draw_solid_fill(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer,
                           const litehtml::web_color &Color)
{
    GdiFlush();
    plutovg_canvas_t *Canvas = ((DrawContext *)Hdc)->Canvas;

    if (Color.alpha == 0)
        return;
    plutovg_canvas_save(Canvas);
    plutovg_canvas_clip_rect(Canvas, Layer.clip_box.x, Layer.clip_box.y, Layer.clip_box.width, Layer.clip_box.height);
    SetColor(Canvas, Color);
    RoundedRect(Canvas, Layer.border_box, Layer.border_radius);
    plutovg_canvas_fill(Canvas);
    plutovg_canvas_restore(Canvas);
}

static std::vector<plutovg_gradient_stop_t>
Stops(const litehtml::background_layer::gradient_base &Gradient)
{
    std::vector<plutovg_gradient_stop_t> Result;

    for (const auto &Point : Gradient.color_points)
    {
        plutovg_gradient_stop_t Stop;
        Stop.offset = Point.offset;
        Stop.color.r = Channel(Point.color.red);
        Stop.color.g = Channel(Point.color.green);
        Stop.color.b = Channel(Point.color.blue);
        Stop.color.a = Channel(Point.color.alpha);
        Result.push_back(Stop);
    }
    return Result;
}

void
Container::draw_linear_gradient(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer,
                                const litehtml::background_layer::linear_gradient &Gradient)
{
    GdiFlush();
    plutovg_canvas_t *Canvas = ((DrawContext *)Hdc)->Canvas;
    std::vector<plutovg_gradient_stop_t> Points = Stops(Gradient);
    const litehtml::position &Origin = Layer.origin_box;

    if (Points.empty())
        return;
    plutovg_canvas_save(Canvas);
    plutovg_canvas_clip_rect(Canvas, Layer.clip_box.x, Layer.clip_box.y, Layer.clip_box.width, Layer.clip_box.height);
    plutovg_canvas_set_linear_gradient(Canvas, Origin.x + Gradient.start.x, Origin.y + Gradient.start.y,
                                       Origin.x + Gradient.end.x, Origin.y + Gradient.end.y, PLUTOVG_SPREAD_METHOD_PAD,
                                       Points.data(), (int)Points.size(), nullptr);
    RoundedRect(Canvas, Layer.border_box, Layer.border_radius);
    plutovg_canvas_fill(Canvas);
    plutovg_canvas_restore(Canvas);
}

void
Container::draw_radial_gradient(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer,
                                const litehtml::background_layer::radial_gradient &Gradient)
{
    GdiFlush();
    plutovg_canvas_t *Canvas = ((DrawContext *)Hdc)->Canvas;
    std::vector<plutovg_gradient_stop_t> Points = Stops(Gradient);
    const litehtml::position &Origin = Layer.origin_box;
    float Rx = Gradient.radius.x > 0 ? Gradient.radius.x : 1.0f;
    float Ry = Gradient.radius.y > 0 ? Gradient.radius.y : Rx;
    float Cx = Origin.x + Gradient.position.x, Cy = Origin.y + Gradient.position.y;
    plutovg_matrix_t Matrix;

    if (Points.empty())
        return;
    plutovg_matrix_init_translate(&Matrix, Cx, Cy);
    plutovg_matrix_scale(&Matrix, 1.0f, Ry / Rx);
    plutovg_matrix_translate(&Matrix, -Cx, -Cy);
    plutovg_canvas_save(Canvas);
    plutovg_canvas_clip_rect(Canvas, Layer.clip_box.x, Layer.clip_box.y, Layer.clip_box.width, Layer.clip_box.height);
    plutovg_canvas_set_radial_gradient(Canvas, Cx, Cy, Rx, Cx, Cy, 0, PLUTOVG_SPREAD_METHOD_PAD, Points.data(),
                                       (int)Points.size(), &Matrix);
    RoundedRect(Canvas, Layer.border_box, Layer.border_radius);
    plutovg_canvas_fill(Canvas);
    plutovg_canvas_restore(Canvas);
}

void
Container::draw_conic_gradient(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer,
                               const litehtml::background_layer::conic_gradient &Gradient)
{
    if (!Gradient.color_points.empty())
        draw_solid_fill(Hdc, Layer, Gradient.color_points.front().color);
}

static bool
Visible(const litehtml::border &Side)
{
    return Side.width > 0 && Side.style != litehtml::border_style_none && Side.style != litehtml::border_style_hidden &&
           Side.color.alpha > 0;
}

void
Container::draw_borders(litehtml::uint_ptr Hdc, const litehtml::borders &Borders, const litehtml::position &Position,
                        bool)
{
    GdiFlush();
    plutovg_canvas_t *Canvas = ((DrawContext *)Hdc)->Canvas;
    const litehtml::border *Sides[4] = { &Borders.top, &Borders.right, &Borders.bottom, &Borders.left };
    float x = Position.x, y = Position.y, w = Position.width, h = Position.height;
    float lw = Borders.left.width, tw = Borders.top.width, rw = Borders.right.width, bw = Borders.bottom.width;
    bool Uniform = true;

    for (int Index = 1; Index < 4; Index++)
    {
        if (Visible(*Sides[Index]) != Visible(*Sides[0]) ||
            memcmp(&Sides[Index]->color, &Sides[0]->color, sizeof(Sides[0]->color)) != 0)
        {
            Uniform = false;
        }
    }

    plutovg_canvas_save(Canvas);
    if (Uniform && Visible(Borders.top))
    {
        litehtml::position Inner(x + lw, y + tw, w - lw - rw, h - tw - bw);
        litehtml::border_radiuses InnerRadius = Borders.radius;

        InnerRadius.top_left_x = std::max(0.0f, InnerRadius.top_left_x - lw);
        InnerRadius.top_left_y = std::max(0.0f, InnerRadius.top_left_y - tw);
        InnerRadius.top_right_x = std::max(0.0f, InnerRadius.top_right_x - rw);
        InnerRadius.top_right_y = std::max(0.0f, InnerRadius.top_right_y - tw);
        InnerRadius.bottom_right_x = std::max(0.0f, InnerRadius.bottom_right_x - rw);
        InnerRadius.bottom_right_y = std::max(0.0f, InnerRadius.bottom_right_y - bw);
        InnerRadius.bottom_left_x = std::max(0.0f, InnerRadius.bottom_left_x - lw);
        InnerRadius.bottom_left_y = std::max(0.0f, InnerRadius.bottom_left_y - bw);
        SetColor(Canvas, Borders.top.color);
        plutovg_canvas_set_fill_rule(Canvas, PLUTOVG_FILL_RULE_EVEN_ODD);
        RoundedRect(Canvas, Position, Borders.radius);
        if (Inner.width > 0 && Inner.height > 0)
            RoundedRect(Canvas, Inner, InnerRadius);
        plutovg_canvas_fill(Canvas);
    }
    else
    {
        const float Quads[4][8] =
        {
            { x, y, x + w, y, x + w - rw, y + tw, x + lw, y + tw },
            { x + w, y, x + w, y + h, x + w - rw, y + h - bw, x + w - rw, y + tw },
            { x + w, y + h, x, y + h, x + lw, y + h - bw, x + w - rw, y + h - bw },
            { x, y + h, x, y, x + lw, y + tw, x + lw, y + h - bw },
        };
        for (int Index = 0; Index < 4; Index++)
        {
            if (!Visible(*Sides[Index]))
                continue;
            SetColor(Canvas, Sides[Index]->color);
            plutovg_canvas_move_to(Canvas, Quads[Index][0], Quads[Index][1]);
            plutovg_canvas_line_to(Canvas, Quads[Index][2], Quads[Index][3]);
            plutovg_canvas_line_to(Canvas, Quads[Index][4], Quads[Index][5]);
            plutovg_canvas_line_to(Canvas, Quads[Index][6], Quads[Index][7]);
            plutovg_canvas_close_path(Canvas);
            plutovg_canvas_fill(Canvas);
        }
    }
    plutovg_canvas_restore(Canvas);
}

void
Container::set_caption(const char *)
{
}

void
Container::set_base_url(const char *BaseUrl)
{
    if (BaseUrl && *BaseUrl)
        m_BaseUrl = Resolve(BaseUrl, m_DocumentUrl);
}

void
Container::link(const std::shared_ptr<litehtml::document> &, const litehtml::element::ptr &)
{
}

void
Container::on_anchor_click(const char *Url, const litehtml::element::ptr &)
{
    if (Url && m_ClickHandler)
        m_ClickHandler(m_ClickContext, Url);
}

void
Container::on_mouse_event(const litehtml::element::ptr &, litehtml::mouse_event)
{
}

void
Container::set_cursor(const char *Cursor)
{
    m_Cursor = LoadCursorW(nullptr, (Cursor && strcmp(Cursor, "pointer") == 0) ? IDC_HAND : IDC_ARROW);
}

void
Container::transform_text(litehtml::string &Text, litehtml::text_transform Transform)
{
    std::wstring Wide = Utf8ToWide(Text);

    if (Wide.empty())
        return;
    switch (Transform)
    {
        case litehtml::text_transform_uppercase:
            CharUpperBuffW(&Wide[0], (DWORD)Wide.size());
            break;
        case litehtml::text_transform_lowercase:
            CharLowerBuffW(&Wide[0], (DWORD)Wide.size());
            break;
        case litehtml::text_transform_capitalize:
            CharUpperBuffW(&Wide[0], 1);
            break;
        default:
            return;
    }
    Text = WideToUtf8(Wide);
}

void
Container::import_css(litehtml::string &Text, const litehtml::string &Url, litehtml::string &BaseUrl)
{
    std::string Resolved = Resolve(Url, BaseUrl);

    Text.clear();
    if (Fetch(Resolved, Text))
        BaseUrl = Resolved;
}

void
Container::set_clip(const litehtml::position &Position, const litehtml::border_radiuses &Radius)
{
    if (!m_Draw)
        return;
    GdiFlush();
    plutovg_canvas_save(m_Draw->Canvas);
    RoundedRect(m_Draw->Canvas, Position, Radius);
    plutovg_canvas_clip(m_Draw->Canvas);
    SaveDC(m_Draw->Dc);
    IntersectClipRect(m_Draw->Dc, (int)std::floor(Position.x), (int)std::floor(Position.y),
                      (int)std::ceil(Position.right()), (int)std::ceil(Position.bottom()));
}

void
Container::del_clip()
{
    if (!m_Draw)
        return;
    RestoreDC(m_Draw->Dc, -1);
    plutovg_canvas_restore(m_Draw->Canvas);
}

void
Container::get_viewport(litehtml::position &Viewport) const
{
    Viewport = litehtml::position(0, 0, (litehtml::pixel_t)m_ViewportWidth, (litehtml::pixel_t)m_ViewportHeight);
}

litehtml::element::ptr
Container::create_element(const char *TagName, const litehtml::string_map &,
                          const std::shared_ptr<litehtml::document> &Document)
{
    if (TagName && (strcmp(TagName, "input") == 0 || strcmp(TagName, "select") == 0 || strcmp(TagName, "textarea") == 0))
        return std::make_shared<Control>(Document);
    return nullptr;
}

void
Container::get_media_features(litehtml::media_features &Media) const
{
    Media.type = litehtml::media_type_screen;
    Media.width = (litehtml::pixel_t)m_ViewportWidth;
    Media.height = (litehtml::pixel_t)m_ViewportHeight;
    Media.device_width = (litehtml::pixel_t)GetSystemMetrics(SM_CXSCREEN);
    Media.device_height = (litehtml::pixel_t)GetSystemMetrics(SM_CYSCREEN);
    Media.color = 8;
    Media.color_index = 256;
    Media.monochrome = 0;
    Media.resolution = (litehtml::pixel_t)m_Dpi;
}

void
Container::get_language(litehtml::string &Language, litehtml::string &Culture) const
{
    WCHAR Name[LOCALE_NAME_MAX_LENGTH] = L"";
    std::string Locale;

    if (GetUserDefaultLocaleName(Name, LOCALE_NAME_MAX_LENGTH))
        Locale = WideToUtf8(Name);
    size_t Dash = Locale.find('-');
    Language = Locale.substr(0, Dash);
    Culture = Dash == std::string::npos ? std::string() : Locale.substr(Dash + 1);
}

}
