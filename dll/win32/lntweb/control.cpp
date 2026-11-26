/*
 * PROJECT:     LiberNT Web View
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Form fields laid out as replaced elements and shown as native controls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntwebp.h"

#include <render_image.h>
#include <cmath>
#include <cstring>

namespace lntweb
{

class ControlRender : public litehtml::render_item_image
{
public:
    explicit ControlRender(std::shared_ptr<litehtml::element> Element) : render_item_image(std::move(Element)) {}

    std::shared_ptr<litehtml::render_item> clone() override { return std::make_shared<ControlRender>(src_el()); }

protected:
    litehtml::rendered_width _render(litehtml::pixel_t X, litehtml::pixel_t Y,
                                     const litehtml::containing_block_context &Containing,
                                     litehtml::formatting_context *, bool) override;
};

litehtml::rendered_width
ControlRender::_render(litehtml::pixel_t X, litehtml::pixel_t Y, const litehtml::containing_block_context &Containing,
                       litehtml::formatting_context *, bool)
{
    typedef litehtml::containing_block_context Context;
    Context Self = calculate_containing_block_context(Containing);
    const litehtml::css_properties &Css = src_el()->css();
    litehtml::size Natural;
    litehtml::rendered_width Result;

    calc_outlines(Containing.width);
    m_pos.move_to(X, Y);
    src_el()->get_content_size(Natural, Containing.width);

    m_pos.width = Self.width.type != Context::cbc_value_type_auto ? (litehtml::pixel_t)Self.render_width : Natural.width;
    if (Self.min_width.type != Context::cbc_value_type_none)
        m_pos.width = std::max<litehtml::pixel_t>(m_pos.width, Self.min_width);
    if (Self.max_width.type != Context::cbc_value_type_none)
        m_pos.width = std::min<litehtml::pixel_t>(m_pos.width, Self.max_width);

    if (Self.height.type != Context::cbc_value_type_auto && Self.height > 0)
        m_pos.height = Self.height - box_sizing_height();
    else
        m_pos.height = Natural.height;
    if (Self.min_height.type != Context::cbc_value_type_none)
        m_pos.height = std::max<litehtml::pixel_t>(m_pos.height, Self.min_height);
    if (Self.max_height.type != Context::cbc_value_type_none)
        m_pos.height = std::min<litehtml::pixel_t>(m_pos.height, Self.max_height);
    m_pos.width = std::max<litehtml::pixel_t>(m_pos.width, 0);
    m_pos.height = std::max<litehtml::pixel_t>(m_pos.height, 0);

    src_el()->css_w().line_height_w().computed_value = height();
    m_pos.x += content_offset_left();
    m_pos.y += content_offset_top();

    Result.natural_width = m_pos.width + content_offset_width();
    bool Compressible = Css.get_width().units() == litehtml::css_units_percentage ||
                        Css.get_max_width().units() == litehtml::css_units_percentage;
    Result.min_width = Compressible ? content_offset_width() : Result.natural_width;
    return Result;
}

static std::string
Lower(const char *Text)
{
    std::string Result = Text ? Text : "";
    for (auto &Character : Result)
        Character = (char)tolower((unsigned char)Character);
    return Result;
}

std::string
Control::Type() const
{
    const char *Tag = get_tagName();

    if (Tag && strcmp(Tag, "select") == 0)
        return "select";
    if (Tag && strcmp(Tag, "textarea") == 0)
        return "textarea";
    std::string Kind = Lower(get_attr("type", "text"));
    return Kind.empty() ? "text" : Kind;
}

bool
Control::IsButton() const
{
    std::string Kind = Type();
    return Kind == "submit" || Kind == "button" || Kind == "reset" || Kind == "image";
}

bool
Control::IsField() const
{
    std::string Kind = Type();
    return Kind != "hidden" && !IsButton() && Kind != "file";
}

std::string
Control::Caption() const
{
    const char *Value = get_attr("value");
    std::string Kind = Type();

    if (Value && *Value)
        return Value;
    if (Kind == "image")
        return get_attr("alt", "Submit");
    if (Kind == "reset")
        return "Reset";
    return Kind == "submit" ? "Submit" : "";
}

bool
Control::is_replaced() const
{
    return true;
}

void
Control::get_content_size(litehtml::size &Size, litehtml::pixel_t)
{
    const litehtml::font_metrics &Metrics = css().get_font_metrics();
    litehtml::document::ptr Document = get_document();
    litehtml::pixel_t Height = Metrics.height > 0 ? Metrics.height : 16;
    litehtml::pixel_t Char = Metrics.ch_width > 0 ? Metrics.ch_width : Height / 2;
    std::string Kind = Type();

    if (Kind == "checkbox" || Kind == "radio")
    {
        Size.width = Size.height = std::max<litehtml::pixel_t>(13, std::round(Metrics.font_size * 0.9f));
    }
    else if (Kind == "select")
    {
        litehtml::pixel_t Widest = 4 * Char;
        for (const auto &Child : children())
        {
            std::string Text;
            Child->get_text(Text);
            if (Document && css().get_font())
                Widest = std::max(Widest, Document->container()->text_width(Text.c_str(), css().get_font()));
        }
        Size.width = Widest + Height + 12;
        Size.height = Height + 6;
    }
    else if (Kind == "textarea")
    {
        int Columns = atoi(get_attr("cols", "20")), Rows = atoi(get_attr("rows", "2"));
        Size.width = std::max(Columns, 1) * Char + 8;
        Size.height = std::max(Rows, 1) * Height + 6;
    }
    else if (IsButton())
    {
        std::string Text = Caption();
        litehtml::pixel_t Width = (Document && css().get_font())
                                      ? Document->container()->text_width(Text.c_str(), css().get_font())
                                      : Text.size() * Char;
        Size.width = Width + 16;
        Size.height = Height + 6;
    }
    else
    {
        int Columns = atoi(get_attr("size", "20"));
        Size.width = std::max(Columns, 1) * Char + 8;
        Size.height = Height + 6;
    }
}

void
Control::draw(litehtml::uint_ptr Hdc, litehtml::pixel_t X, litehtml::pixel_t Y, const litehtml::position *Clip,
              const std::shared_ptr<litehtml::render_item> &RenderItem)
{
    litehtml::html_tag::draw(Hdc, X, Y, Clip, RenderItem);
    if (!IsButton() || !css().get_font())
        return;

    litehtml::document::ptr Document = get_document();
    litehtml::position Box = RenderItem->pos();
    std::string Text = Caption();
    litehtml::pixel_t Width = Document->container()->text_width(Text.c_str(), css().get_font());
    const litehtml::font_metrics &Metrics = css().get_font_metrics();

    Box.x += X;
    Box.y += Y;
    litehtml::position Line(Box.x + (Box.width - Width) / 2, Box.y + (Box.height - Metrics.height) / 2, Width,
                            Metrics.height);
    Document->container()->draw_text(Hdc, Text.c_str(), css().get_font(), css().get_color(), Line);
}

std::shared_ptr<litehtml::render_item>
Control::create_render_item(const std::shared_ptr<litehtml::render_item> &Parent)
{
    auto Item = std::make_shared<ControlRender>(shared_from_this());
    Item->parent(Parent);
    return Item;
}

}
