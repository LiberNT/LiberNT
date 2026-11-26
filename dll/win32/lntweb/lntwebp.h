/*
 * PROJECT:     LiberNT Web View
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <litehtml.h>
#include <plutovg.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <libernt/lntweb.h>

namespace lntweb
{

struct Image
{
    plutovg_surface_t *Surface = nullptr;
    int Width = 0;
    int Height = 0;
};

struct DrawContext
{
    HDC Dc;
    plutovg_canvas_t *Canvas;
};

class Container : public litehtml::document_container
{
public:
    Container(const LNTWEB_HOST &Host, const std::string &Url, int ViewportWidth, int ViewportHeight, int Dpi);
    ~Container() override;

    void SetViewport(int Width, int Height) { m_ViewportWidth = Width; m_ViewportHeight = Height; }
    std::string Resolve(const std::string &Reference, const std::string &Base = std::string()) const;
    const std::string &BaseUrl() const { return m_BaseUrl; }
    HCURSOR Cursor() const { return m_Cursor; }
    void SetDrawContext(DrawContext *Context) { m_Draw = Context; }
    void SetClickHandler(void (*Handler)(void *, const std::string &), void *Context)
    {
        m_ClickHandler = Handler;
        m_ClickContext = Context;
    }

    litehtml::uint_ptr create_font(const litehtml::font_description &Description, const litehtml::document *Document,
                                   litehtml::font_metrics *Metrics) override;
    void delete_font(litehtml::uint_ptr Font) override;
    litehtml::pixel_t text_width(const char *Text, litehtml::uint_ptr Font) override;
    void draw_text(litehtml::uint_ptr Hdc, const char *Text, litehtml::uint_ptr Font, litehtml::web_color Color,
                   const litehtml::position &Position) override;
    litehtml::pixel_t pt_to_px(float Points) const override;
    litehtml::pixel_t get_default_font_size() const override;
    const char *get_default_font_name() const override;
    void draw_list_marker(litehtml::uint_ptr Hdc, const litehtml::list_marker &Marker) override;
    void load_image(const char *Source, const char *BaseUrl, bool RedrawOnReady) override;
    void get_image_size(const char *Source, const char *BaseUrl, litehtml::size &Size) override;
    void draw_image(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer, const std::string &Url,
                    const std::string &BaseUrl) override;
    void draw_solid_fill(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer,
                         const litehtml::web_color &Color) override;
    void draw_linear_gradient(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer,
                              const litehtml::background_layer::linear_gradient &Gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer,
                              const litehtml::background_layer::radial_gradient &Gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr Hdc, const litehtml::background_layer &Layer,
                             const litehtml::background_layer::conic_gradient &Gradient) override;
    void draw_borders(litehtml::uint_ptr Hdc, const litehtml::borders &Borders, const litehtml::position &Position,
                      bool Root) override;
    void set_caption(const char *Caption) override;
    void set_base_url(const char *BaseUrl) override;
    void link(const std::shared_ptr<litehtml::document> &Document, const litehtml::element::ptr &Element) override;
    void on_anchor_click(const char *Url, const litehtml::element::ptr &Element) override;
    void on_mouse_event(const litehtml::element::ptr &Element, litehtml::mouse_event Event) override;
    void set_cursor(const char *Cursor) override;
    void transform_text(litehtml::string &Text, litehtml::text_transform Transform) override;
    void import_css(litehtml::string &Text, const litehtml::string &Url, litehtml::string &BaseUrl) override;
    void set_clip(const litehtml::position &Position, const litehtml::border_radiuses &Radius) override;
    void del_clip() override;
    void get_viewport(litehtml::position &Viewport) const override;
    litehtml::element::ptr create_element(const char *TagName, const litehtml::string_map &Attributes,
                                          const std::shared_ptr<litehtml::document> &Document) override;
    void get_media_features(litehtml::media_features &Media) const override;
    void get_language(litehtml::string &Language, litehtml::string &Culture) const override;

private:
    bool Fetch(const std::string &Url, std::string &Data);
    const Image *FindImage(const std::string &Url) const;
    std::wstring PickFace(const std::string &Families);

    LNTWEB_HOST m_Host;
    std::string m_DocumentUrl;
    std::string m_BaseUrl;
    std::string m_DefaultFont;
    std::map<std::string, Image> m_Images;
    std::map<std::string, std::wstring> m_Faces;
    std::map<litehtml::uint_ptr, int> m_Descents;
    HDC m_MeasureDc;
    HCURSOR m_Cursor;
    int m_ViewportWidth;
    int m_ViewportHeight;
    int m_Dpi;
    void (*m_ClickHandler)(void *, const std::string &) = nullptr;
    void *m_ClickContext = nullptr;
    DrawContext *m_Draw = nullptr;
};

class Control : public litehtml::html_tag
{
public:
    explicit Control(const std::shared_ptr<litehtml::document> &Document) : litehtml::html_tag(Document) {}

    bool is_replaced() const override;
    void get_content_size(litehtml::size &Size, litehtml::pixel_t MaxWidth) override;
    void draw(litehtml::uint_ptr Hdc, litehtml::pixel_t X, litehtml::pixel_t Y, const litehtml::position *Clip,
              const std::shared_ptr<litehtml::render_item> &RenderItem) override;
    std::shared_ptr<litehtml::render_item> create_render_item(
        const std::shared_ptr<litehtml::render_item> &Parent) override;

    std::string Type() const;
    std::string Caption() const;
    bool IsButton() const;
    bool IsField() const;

    HWND Window = nullptr;
};

std::wstring Utf8ToWide(const std::string &Text);
std::string WideToUtf8(const std::wstring &Text);

}
