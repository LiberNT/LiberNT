/*
 * PROJECT:     LiberNT Console Host
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Virtual terminal sequences for console output and input
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>

#include "conhost.h"

#define VT_MAX_PARAMS 32
#define VT_MAX_STRING 512

enum vt_parse_state
{
    VT_GROUND,
    VT_ESCAPE,
    VT_CHARSET,
    VT_CSI,
    VT_OSC,
    VT_STRING,
};

struct vt_pen
{
    int      fg;
    int      bg;
    COLORREF fg_rgb;
    COLORREF bg_rgb;
    BOOL bold;
    BOOL reverse;
    BOOL underline;
    BOOL line_drawing;
};

struct vt_state
{
    enum vt_parse_state state;
    BOOL                string_escape;
    WCHAR               prefix;
    WCHAR               intermediate;
    unsigned int        params[VT_MAX_PARAMS];
    unsigned int        param_count;
    BOOL                param_started;
    WCHAR               string[VT_MAX_STRING];
    unsigned int        string_len;
    unsigned short      default_attr;
    struct vt_pen       pen;
    struct cell_color   cell;
    BOOL                cursor_keys_application;
    BOOL                has_margins;
    unsigned int        margin_top;
    unsigned int        margin_bottom;
    BOOL                cursor_saved;
    unsigned int        saved_x;
    unsigned int        saved_y;
    struct vt_pen       saved_pen;
    char_info_t        *main_data;
    struct cell_color  *main_colors;
    unsigned int        main_width;
    unsigned int        main_height;
    unsigned int        main_cursor_x;
    unsigned int        main_cursor_y;
    RECT                main_win;
};

static const WCHAR line_drawing_map[] =
{
    0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
    0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
    0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
    0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,
};

static struct vt_state *vt_get_state( struct screen_buffer *screen_buffer )
{
    struct vt_state *vt = screen_buffer->vt;

    if (vt) return vt;
    if (!(vt = calloc( 1, sizeof(*vt) ))) return NULL;
    vt->default_attr = screen_buffer->attr;
    vt->pen.fg = -1;
    vt->pen.bg = -1;
    vt->pen.fg_rgb = vt->pen.bg_rgb = CLR_INVALID;
    vt->cell.fg = vt->cell.bg = CLR_INVALID;
    screen_buffer->vt = vt;
    return vt;
}

void vt_destroy( struct screen_buffer *screen_buffer )
{
    free( screen_buffer->colors );
    screen_buffer->colors = NULL;
    if (!screen_buffer->vt) return;
    free( screen_buffer->vt->main_data );
    free( screen_buffer->vt->main_colors );
    free( screen_buffer->vt );
    screen_buffer->vt = NULL;
}

void resize_cell_colors( struct screen_buffer *screen_buffer, unsigned int new_width, unsigned int new_height )
{
    unsigned int copy_width = min( screen_buffer->width, new_width );
    unsigned int copy_height = min( screen_buffer->height, new_height ), y;
    struct cell_color *colors;

    if (!screen_buffer->colors) return;
    if ((colors = malloc( new_width * new_height * sizeof(*colors) )))
    {
        memset( colors, 0xff, new_width * new_height * sizeof(*colors) );
        for (y = 0; y < copy_height; y++)
            memcpy( &colors[y * new_width], &screen_buffer->colors[y * screen_buffer->width],
                    copy_width * sizeof(*colors) );
    }
    free( screen_buffer->colors );
    screen_buffer->colors = colors;
}

static void vt_color_cells( struct screen_buffer *screen_buffer, unsigned int first, unsigned int count )
{
    struct vt_state *vt = screen_buffer->vt;
    unsigned int size = screen_buffer->width * screen_buffer->height, i;

    if (vt->cell.fg == CLR_INVALID && vt->cell.bg == CLR_INVALID)
    {
        reset_cell_colors( screen_buffer, first, count );
        return;
    }
    if (!screen_buffer->colors)
    {
        if (!(screen_buffer->colors = malloc( size * sizeof(*screen_buffer->colors) ))) return;
        memset( screen_buffer->colors, 0xff, size * sizeof(*screen_buffer->colors) );
    }
    for (i = 0; i < count; i++) screen_buffer->colors[first + i] = vt->cell;
}

static void vt_touch( RECT *rect, int left, int top, int right, int bottom )
{
    rect->left   = min( rect->left, left );
    rect->top    = min( rect->top, top );
    rect->right  = max( rect->right, right );
    rect->bottom = max( rect->bottom, bottom );
}

static void vt_fill( struct screen_buffer *screen_buffer, unsigned int y, unsigned int first,
                     unsigned int last, RECT *rect )
{
    char_info_t *row;
    unsigned int x;

    if (y >= screen_buffer->height || first >= screen_buffer->width) return;
    last = min( last, screen_buffer->width - 1 );
    if (first > last) return;
    row = &screen_buffer->data[y * screen_buffer->width];
    for (x = first; x <= last; x++)
    {
        row[x].ch = ' ';
        row[x].attr = screen_buffer->attr;
    }
    vt_color_cells( screen_buffer, y * screen_buffer->width + first, last - first + 1 );
    vt_touch( rect, first, y, last, y );
}

static void vt_scroll_rows( struct screen_buffer *screen_buffer, unsigned int top, unsigned int bottom,
                            int count, RECT *rect )
{
    unsigned int rows, width = screen_buffer->width, n, y;
    char_info_t *data = screen_buffer->data;

    if (top > bottom || bottom >= screen_buffer->height || !count) return;
    rows = bottom - top + 1;
    n = count > 0 ? count : -count;
    if (n > rows) n = rows;
    if (count > 0)
    {
        memmove( &data[top * width], &data[(top + n) * width], (rows - n) * width * sizeof(*data) );
        move_cell_colors( screen_buffer, top * width, (top + n) * width, (rows - n) * width );
        for (y = bottom - n + 1; y <= bottom; y++) vt_fill( screen_buffer, y, 0, width - 1, rect );
    }
    else
    {
        memmove( &data[(top + n) * width], &data[top * width], (rows - n) * width * sizeof(*data) );
        move_cell_colors( screen_buffer, (top + n) * width, top * width, (rows - n) * width );
        for (y = top; y < top + n; y++) vt_fill( screen_buffer, y, 0, width - 1, rect );
    }
    vt_touch( rect, 0, top, width - 1, bottom );
}

static void vt_region( struct screen_buffer *screen_buffer, struct vt_state *vt,
                       unsigned int *top, unsigned int *bottom )
{
    *top = screen_buffer->win.top;
    *bottom = min( (unsigned int)screen_buffer->win.bottom, screen_buffer->height - 1 );
    if (vt->has_margins)
    {
        *bottom = min( *top + vt->margin_bottom, *bottom );
        *top = min( *top + vt->margin_top, *bottom );
    }
}

static void vt_index( struct screen_buffer *screen_buffer, struct vt_state *vt, RECT *rect )
{
    unsigned int top, bottom;

    vt_region( screen_buffer, vt, &top, &bottom );
    if (vt->has_margins)
    {
        if (screen_buffer->cursor_y == bottom)
            vt_scroll_rows( screen_buffer, top, bottom, 1, rect );
        else if (screen_buffer->cursor_y < (unsigned int)screen_buffer->win.bottom &&
                 screen_buffer->cursor_y + 1 < screen_buffer->height)
            screen_buffer->cursor_y++;
        return;
    }
    if (screen_buffer->cursor_y + 1 < screen_buffer->height)
    {
        screen_buffer->cursor_y++;
        if (screen_buffer->cursor_y > (unsigned int)screen_buffer->win.bottom)
        {
            screen_buffer->win.top++;
            screen_buffer->win.bottom++;
        }
        return;
    }
    vt_scroll_rows( screen_buffer, 0, screen_buffer->height - 1, 1, rect );
}

static void vt_reverse_index( struct screen_buffer *screen_buffer, struct vt_state *vt, RECT *rect )
{
    unsigned int top, bottom;

    vt_region( screen_buffer, vt, &top, &bottom );
    if (screen_buffer->cursor_y == top)
        vt_scroll_rows( screen_buffer, top, bottom, -1, rect );
    else if (screen_buffer->cursor_y > (unsigned int)screen_buffer->win.top)
        screen_buffer->cursor_y--;
}

static void vt_put( struct screen_buffer *screen_buffer, struct vt_state *vt, WCHAR ch, RECT *rect )
{
    char_info_t *cell;

    if (screen_buffer->cursor_x >= screen_buffer->width)
    {
        if (screen_buffer->mode & ENABLE_WRAP_AT_EOL_OUTPUT)
        {
            screen_buffer->cursor_x = 0;
            vt_index( screen_buffer, vt, rect );
        }
        else screen_buffer->cursor_x = screen_buffer->width - 1;
    }
    if (vt->pen.line_drawing && ch >= 0x60 && ch <= 0x7e) ch = line_drawing_map[ch - 0x60];
    cell = &screen_buffer->data[screen_buffer->cursor_y * screen_buffer->width + screen_buffer->cursor_x];
    cell->ch = ch;
    cell->attr = screen_buffer->attr;
    vt_color_cells( screen_buffer, cell - screen_buffer->data, 1 );
    vt_touch( rect, screen_buffer->cursor_x, screen_buffer->cursor_y,
              screen_buffer->cursor_x, screen_buffer->cursor_y );
    screen_buffer->cursor_x++;
}

static unsigned int vt_nearest_color( struct screen_buffer *screen_buffer, unsigned int r,
                                      unsigned int g, unsigned int b )
{
    unsigned int i, best = 0, best_distance = ~0u;

    for (i = 0; i < ARRAY_SIZE(screen_buffer->color_map); i++)
    {
        int dr = (int)GetRValue( screen_buffer->color_map[i] ) - (int)r;
        int dg = (int)GetGValue( screen_buffer->color_map[i] ) - (int)g;
        int db = (int)GetBValue( screen_buffer->color_map[i] ) - (int)b;
        unsigned int distance = dr * dr + dg * dg + db * db;

        if (distance < best_distance)
        {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

static unsigned int vt_ansi_color( unsigned int index )
{
    return ((index & 1) << 2) | (index & 2) | ((index & 4) >> 2) | (index & 8);
}

static unsigned int vt_indexed_color( struct screen_buffer *screen_buffer, unsigned int index, COLORREF *rgb )
{
    static const unsigned char levels[] = { 0, 95, 135, 175, 215, 255 };
    unsigned int grey;

    if (index < 16)
    {
        *rgb = CLR_INVALID;
        return vt_ansi_color( index );
    }
    if (index < 232)
    {
        index -= 16;
        *rgb = RGB( levels[index / 36], levels[(index / 6) % 6], levels[index % 6] );
    }
    else
    {
        grey = 8 + (min( index, 255 ) - 232) * 10;
        *rgb = RGB( grey, grey, grey );
    }
    return vt_nearest_color( screen_buffer, GetRValue( *rgb ), GetGValue( *rgb ), GetBValue( *rgb ) );
}

static void vt_apply_pen( struct screen_buffer *screen_buffer, struct vt_state *vt )
{
    unsigned int fg = vt->pen.fg < 0 ? (vt->default_attr & 0x0f) : (unsigned int)vt->pen.fg;
    unsigned int bg = vt->pen.bg < 0 ? ((vt->default_attr >> 4) & 0x0f) : (unsigned int)vt->pen.bg;
    unsigned int swap;

    if (vt->pen.bold) fg |= FOREGROUND_INTENSITY;
    vt->cell.fg = vt->pen.fg_rgb;
    vt->cell.bg = vt->pen.bg_rgb;
    if (vt->pen.reverse)
    {
        swap = fg;
        fg = bg;
        bg = swap;
        vt->cell.fg = vt->pen.bg_rgb;
        vt->cell.bg = vt->pen.fg_rgb;
    }
    screen_buffer->attr = fg | (bg << 4) | (vt->pen.underline ? COMMON_LVB_UNDERSCORE : 0);
}

static unsigned int vt_extended_color( struct screen_buffer *screen_buffer, struct vt_state *vt,
                                       unsigned int i, int *color, COLORREF *rgb )
{
    if (i + 1 >= vt->param_count) return vt->param_count;
    if (vt->params[i + 1] == 5)
    {
        if (i + 2 < vt->param_count) *color = vt_indexed_color( screen_buffer, vt->params[i + 2], rgb );
        return i + 2;
    }
    if (vt->params[i + 1] == 2)
    {
        if (i + 4 < vt->param_count)
        {
            *rgb = RGB( min( vt->params[i + 2], 255 ), min( vt->params[i + 3], 255 ), min( vt->params[i + 4], 255 ) );
            *color = vt_nearest_color( screen_buffer, GetRValue( *rgb ), GetGValue( *rgb ), GetBValue( *rgb ) );
        }
        return i + 4;
    }
    return i + 1;
}

static void vt_sgr( struct screen_buffer *screen_buffer, struct vt_state *vt )
{
    unsigned int i, param;

    if (!vt->param_count)
    {
        vt->params[0] = 0;
        vt->param_count = 1;
    }
    for (i = 0; i < vt->param_count; i++)
    {
        param = vt->params[i];
        if (!param)
        {
            vt->pen.fg = vt->pen.bg = -1;
            vt->pen.fg_rgb = vt->pen.bg_rgb = CLR_INVALID;
            vt->pen.bold = vt->pen.reverse = vt->pen.underline = FALSE;
        }
        else if (param == 1) vt->pen.bold = TRUE;
        else if (param == 22) vt->pen.bold = FALSE;
        else if (param == 4) vt->pen.underline = TRUE;
        else if (param == 24) vt->pen.underline = FALSE;
        else if (param == 7) vt->pen.reverse = TRUE;
        else if (param == 27) vt->pen.reverse = FALSE;
        else if (param == 38) i = vt_extended_color( screen_buffer, vt, i, &vt->pen.fg, &vt->pen.fg_rgb );
        else if (param == 48) i = vt_extended_color( screen_buffer, vt, i, &vt->pen.bg, &vt->pen.bg_rgb );
        else if ((param >= 30 && param <= 39) || (param >= 90 && param <= 97))
        {
            vt->pen.fg_rgb = CLR_INVALID;
            if (param == 39) vt->pen.fg = -1;
            else vt->pen.fg = vt_ansi_color( param < 90 ? param - 30 : param - 90 + 8 );
        }
        else if ((param >= 40 && param <= 49) || (param >= 100 && param <= 107))
        {
            vt->pen.bg_rgb = CLR_INVALID;
            if (param == 49) vt->pen.bg = -1;
            else vt->pen.bg = vt_ansi_color( param < 100 ? param - 40 : param - 100 + 8 );
        }
    }
    vt_apply_pen( screen_buffer, vt );
}

static void vt_input( struct console *console, const WCHAR *text, unsigned int count )
{
    INPUT_RECORD records[32];
    unsigned int i;

    count = min( count, ARRAY_SIZE(records) );
    for (i = 0; i < count; i++)
    {
        records[i].EventType = KEY_EVENT;
        records[i].Event.KeyEvent.bKeyDown = TRUE;
        records[i].Event.KeyEvent.wRepeatCount = 1;
        records[i].Event.KeyEvent.wVirtualKeyCode = 0;
        records[i].Event.KeyEvent.wVirtualScanCode = 0;
        records[i].Event.KeyEvent.uChar.UnicodeChar = text[i];
        records[i].Event.KeyEvent.dwControlKeyState = 0;
    }
    write_console_input( console, records, count, TRUE );
}

static void vt_reply( struct console *console, const char *text )
{
    WCHAR buffer[32];
    unsigned int count;

    for (count = 0; text[count] && count < ARRAY_SIZE(buffer); count++)
        buffer[count] = (unsigned char)text[count];
    vt_input( console, buffer, count );
}

static void vt_set_cursor( struct screen_buffer *screen_buffer, unsigned int row, unsigned int column )
{
    unsigned int bottom = min( (unsigned int)screen_buffer->win.bottom, screen_buffer->height - 1 );

    screen_buffer->cursor_y = min( screen_buffer->win.top + row, bottom );
    screen_buffer->cursor_x = min( column, screen_buffer->width - 1 );
}

static void vt_save_cursor( struct screen_buffer *screen_buffer, struct vt_state *vt )
{
    vt->cursor_saved = TRUE;
    vt->saved_x = get_bounded_cursor_x( screen_buffer );
    vt->saved_y = screen_buffer->cursor_y - min( screen_buffer->cursor_y, (unsigned int)screen_buffer->win.top );
    vt->saved_pen = vt->pen;
}

static void vt_restore_cursor( struct screen_buffer *screen_buffer, struct vt_state *vt )
{
    if (!vt->cursor_saved)
    {
        vt_set_cursor( screen_buffer, 0, 0 );
        return;
    }
    vt_set_cursor( screen_buffer, vt->saved_y, vt->saved_x );
    vt->pen = vt->saved_pen;
    vt_apply_pen( screen_buffer, vt );
}

static void vt_erase_display( struct screen_buffer *screen_buffer, unsigned int mode, RECT *rect )
{
    unsigned int top = screen_buffer->win.top;
    unsigned int bottom = min( (unsigned int)screen_buffer->win.bottom, screen_buffer->height - 1 );
    unsigned int x = get_bounded_cursor_x( screen_buffer ), y;

    switch (mode)
    {
    case 0:
        vt_fill( screen_buffer, screen_buffer->cursor_y, x, screen_buffer->width - 1, rect );
        for (y = screen_buffer->cursor_y + 1; y <= bottom; y++)
            vt_fill( screen_buffer, y, 0, screen_buffer->width - 1, rect );
        break;
    case 1:
        for (y = top; y < screen_buffer->cursor_y; y++)
            vt_fill( screen_buffer, y, 0, screen_buffer->width - 1, rect );
        vt_fill( screen_buffer, screen_buffer->cursor_y, 0, x, rect );
        break;
    case 2:
        for (y = top; y <= bottom; y++) vt_fill( screen_buffer, y, 0, screen_buffer->width - 1, rect );
        break;
    case 3:
        for (y = 0; y < top; y++) vt_fill( screen_buffer, y, 0, screen_buffer->width - 1, rect );
        break;
    }
}

static void vt_erase_line( struct screen_buffer *screen_buffer, unsigned int mode, RECT *rect )
{
    unsigned int x = get_bounded_cursor_x( screen_buffer );

    switch (mode)
    {
    case 0:
        vt_fill( screen_buffer, screen_buffer->cursor_y, x, screen_buffer->width - 1, rect );
        break;
    case 1:
        vt_fill( screen_buffer, screen_buffer->cursor_y, 0, x, rect );
        break;
    case 2:
        vt_fill( screen_buffer, screen_buffer->cursor_y, 0, screen_buffer->width - 1, rect );
        break;
    }
}

static void vt_shift_chars( struct screen_buffer *screen_buffer, unsigned int count, BOOL insert, RECT *rect )
{
    unsigned int x = get_bounded_cursor_x( screen_buffer ), width = screen_buffer->width;
    char_info_t *row = &screen_buffer->data[screen_buffer->cursor_y * width];

    count = min( count, width - x );
    if (!count) return;
    if (insert)
    {
        memmove( &row[x + count], &row[x], (width - x - count) * sizeof(*row) );
        move_cell_colors( screen_buffer, screen_buffer->cursor_y * width + x + count,
                          screen_buffer->cursor_y * width + x, width - x - count );
        vt_fill( screen_buffer, screen_buffer->cursor_y, x, x + count - 1, rect );
    }
    else
    {
        memmove( &row[x], &row[x + count], (width - x - count) * sizeof(*row) );
        move_cell_colors( screen_buffer, screen_buffer->cursor_y * width + x,
                          screen_buffer->cursor_y * width + x + count, width - x - count );
        vt_fill( screen_buffer, screen_buffer->cursor_y, width - count, width - 1, rect );
    }
    vt_touch( rect, x, screen_buffer->cursor_y, width - 1, screen_buffer->cursor_y );
}

static void vt_shift_lines( struct screen_buffer *screen_buffer, struct vt_state *vt, int count, RECT *rect )
{
    unsigned int top, bottom;

    vt_region( screen_buffer, vt, &top, &bottom );
    if (screen_buffer->cursor_y < top || screen_buffer->cursor_y > bottom) return;
    vt_scroll_rows( screen_buffer, screen_buffer->cursor_y, bottom, count, rect );
    screen_buffer->cursor_x = 0;
}

static void vt_alternate_buffer( struct screen_buffer *screen_buffer, struct vt_state *vt, BOOL enable, RECT *rect )
{
    unsigned int width, height, i;
    char_info_t *data;

    if (enable == (vt->main_data != NULL)) return;
    if (enable)
    {
        width = screen_buffer->win.right - screen_buffer->win.left + 1;
        height = screen_buffer->win.bottom - screen_buffer->win.top + 1;
        if (!(data = malloc( width * height * sizeof(*data) ))) return;
        for (i = 0; i < width * height; i++)
        {
            data[i].ch = ' ';
            data[i].attr = screen_buffer->attr;
        }
        vt->main_data = screen_buffer->data;
        vt->main_colors = screen_buffer->colors;
        vt->main_width = screen_buffer->width;
        vt->main_height = screen_buffer->height;
        vt->main_cursor_x = screen_buffer->cursor_x;
        vt->main_cursor_y = screen_buffer->cursor_y;
        vt->main_win = screen_buffer->win;
        screen_buffer->data = data;
        screen_buffer->colors = NULL;
        screen_buffer->width = width;
        screen_buffer->height = height;
        screen_buffer->cursor_x = 0;
        screen_buffer->cursor_y = 0;
        SetRect( &screen_buffer->win, 0, 0, width - 1, height - 1 );
        vt_color_cells( screen_buffer, 0, width * height );
    }
    else
    {
        width = screen_buffer->win.right - screen_buffer->win.left + 1;
        height = screen_buffer->win.bottom - screen_buffer->win.top + 1;
        free( screen_buffer->data );
        free( screen_buffer->colors );
        screen_buffer->data = vt->main_data;
        screen_buffer->colors = vt->main_colors;
        screen_buffer->width = vt->main_width;
        screen_buffer->height = vt->main_height;
        screen_buffer->cursor_x = vt->main_cursor_x;
        screen_buffer->cursor_y = vt->main_cursor_y;
        screen_buffer->win = vt->main_win;
        screen_buffer->win.right = min( screen_buffer->win.left + width, screen_buffer->width ) - 1;
        screen_buffer->win.bottom = min( screen_buffer->win.top + height, screen_buffer->height ) - 1;
        vt->main_data = NULL;
        vt->main_colors = NULL;
    }
    vt->has_margins = FALSE;
    SetRect( rect, 0, 0, screen_buffer->width - 1, screen_buffer->height - 1 );
    notify_screen_buffer_size( screen_buffer );
}

static void vt_private_mode( struct screen_buffer *screen_buffer, struct vt_state *vt, BOOL enable, RECT *rect )
{
    unsigned int i;

    for (i = 0; i < vt->param_count; i++)
    {
        switch (vt->params[i])
        {
        case 1:
            vt->cursor_keys_application = enable;
            break;
        case 25:
            screen_buffer->cursor_visible = enable;
            break;
        case 47:
        case 1047:
            vt_alternate_buffer( screen_buffer, vt, enable, rect );
            break;
        case 1049:
            if (enable == (vt->main_data != NULL)) break;
            if (enable) vt_save_cursor( screen_buffer, vt );
            vt_alternate_buffer( screen_buffer, vt, enable, rect );
            if (!enable) vt_restore_cursor( screen_buffer, vt );
            break;
        }
    }
}

static void vt_soft_reset( struct screen_buffer *screen_buffer, struct vt_state *vt )
{
    vt->pen.fg = vt->pen.bg = -1;
    vt->pen.fg_rgb = vt->pen.bg_rgb = CLR_INVALID;
    vt->pen.bold = vt->pen.reverse = vt->pen.underline = vt->pen.line_drawing = FALSE;
    vt->has_margins = FALSE;
    vt->cursor_saved = FALSE;
    vt->cursor_keys_application = FALSE;
    screen_buffer->cursor_visible = TRUE;
    vt_apply_pen( screen_buffer, vt );
}

static unsigned int vt_param( struct vt_state *vt, unsigned int index, unsigned int fallback )
{
    if (index >= vt->param_count || !vt->params[index]) return fallback;
    return vt->params[index];
}

static void vt_csi( struct screen_buffer *screen_buffer, struct vt_state *vt, WCHAR final, RECT *rect )
{
    unsigned int top = screen_buffer->win.top;
    unsigned int bottom = min( (unsigned int)screen_buffer->win.bottom, screen_buffer->height - 1 );
    unsigned int x = get_bounded_cursor_x( screen_buffer ), n = vt_param( vt, 0, 1 );
    unsigned int region_top, region_bottom;
    char reply[32];

    if (vt->prefix == '?')
    {
        if (final == 'h' || final == 'l') vt_private_mode( screen_buffer, vt, final == 'h', rect );
        return;
    }
    if (vt->prefix) return;
    if (vt->intermediate)
    {
        if (vt->intermediate == '!' && final == 'p') vt_soft_reset( screen_buffer, vt );
        else if (vt->intermediate == ' ' && final == 'q')
            screen_buffer->cursor_size = vt_param( vt, 0, 1 ) <= 2 ? 100 : 25;
        return;
    }

    switch (final)
    {
    case 'A':
        screen_buffer->cursor_y -= min( n, screen_buffer->cursor_y - min( screen_buffer->cursor_y, top ) );
        screen_buffer->cursor_x = x;
        break;
    case 'B':
    case 'e':
        screen_buffer->cursor_y = min( screen_buffer->cursor_y + n, max( bottom, screen_buffer->cursor_y ) );
        screen_buffer->cursor_x = x;
        break;
    case 'C':
    case 'a':
        screen_buffer->cursor_x = min( x + n, screen_buffer->width - 1 );
        break;
    case 'D':
        screen_buffer->cursor_x = x - min( n, x );
        break;
    case 'E':
        screen_buffer->cursor_y = min( screen_buffer->cursor_y + n, max( bottom, screen_buffer->cursor_y ) );
        screen_buffer->cursor_x = 0;
        break;
    case 'F':
        screen_buffer->cursor_y -= min( n, screen_buffer->cursor_y - min( screen_buffer->cursor_y, top ) );
        screen_buffer->cursor_x = 0;
        break;
    case 'G':
    case '`':
        screen_buffer->cursor_x = min( n, screen_buffer->width ) - 1;
        break;
    case 'd':
        screen_buffer->cursor_y = min( top + n - 1, bottom );
        screen_buffer->cursor_x = x;
        break;
    case 'H':
    case 'f':
        vt_set_cursor( screen_buffer, n - 1, vt_param( vt, 1, 1 ) - 1 );
        break;
    case 'J':
        vt_erase_display( screen_buffer, vt_param( vt, 0, 0 ), rect );
        break;
    case 'K':
        vt_erase_line( screen_buffer, vt_param( vt, 0, 0 ), rect );
        break;
    case 'L':
        vt_shift_lines( screen_buffer, vt, -(int)min( n, screen_buffer->height ), rect );
        break;
    case 'M':
        vt_shift_lines( screen_buffer, vt, min( n, screen_buffer->height ), rect );
        break;
    case '@':
        vt_shift_chars( screen_buffer, n, TRUE, rect );
        break;
    case 'P':
        vt_shift_chars( screen_buffer, n, FALSE, rect );
        break;
    case 'X':
        vt_fill( screen_buffer, screen_buffer->cursor_y, x, x + min( n, screen_buffer->width ) - 1, rect );
        break;
    case 'S':
        vt_region( screen_buffer, vt, &region_top, &region_bottom );
        vt_scroll_rows( screen_buffer, region_top, region_bottom, min( n, screen_buffer->height ), rect );
        break;
    case 'T':
        vt_region( screen_buffer, vt, &region_top, &region_bottom );
        vt_scroll_rows( screen_buffer, region_top, region_bottom, -(int)min( n, screen_buffer->height ), rect );
        break;
    case 'm':
        vt_sgr( screen_buffer, vt );
        break;
    case 'r':
        region_top = n - 1;
        region_bottom = vt_param( vt, 1, bottom - top + 1 ) - 1;
        region_bottom = min( region_bottom, bottom - top );
        vt->has_margins = region_top < region_bottom && (region_top || region_bottom != bottom - top);
        vt->margin_top = region_top;
        vt->margin_bottom = region_bottom;
        vt_set_cursor( screen_buffer, 0, 0 );
        break;
    case 's':
        vt_save_cursor( screen_buffer, vt );
        break;
    case 'u':
        vt_restore_cursor( screen_buffer, vt );
        break;
    case 'n':
        if (vt_param( vt, 0, 0 ) == 5) vt_reply( screen_buffer->console, "\x1b[0n" );
        else if (vt_param( vt, 0, 0 ) == 6)
        {
            sprintf( reply, "\x1b[%u;%uR", screen_buffer->cursor_y - min( screen_buffer->cursor_y, top ) + 1, x + 1 );
            vt_reply( screen_buffer->console, reply );
        }
        break;
    case 'c':
        if (!vt_param( vt, 0, 0 )) vt_reply( screen_buffer->console, "\x1b[?1;0c" );
        break;
    }
}

static void vt_escape( struct screen_buffer *screen_buffer, struct vt_state *vt, WCHAR ch, RECT *rect )
{
    unsigned int top = screen_buffer->win.top;
    unsigned int bottom = min( (unsigned int)screen_buffer->win.bottom, screen_buffer->height - 1 );
    unsigned int y;

    vt->state = VT_GROUND;
    switch (ch)
    {
    case '[':
        vt->state = VT_CSI;
        vt->prefix = 0;
        vt->intermediate = 0;
        vt->param_count = 0;
        vt->param_started = FALSE;
        break;
    case ']':
        vt->state = VT_OSC;
        vt->string_len = 0;
        vt->string_escape = FALSE;
        break;
    case 'P':
    case 'X':
    case '^':
    case '_':
        vt->state = VT_STRING;
        vt->string_escape = FALSE;
        break;
    case '(':
    case ')':
    case '*':
    case '+':
        vt->state = VT_CHARSET;
        vt->intermediate = ch;
        break;
    case '7':
        vt_save_cursor( screen_buffer, vt );
        break;
    case '8':
        vt_restore_cursor( screen_buffer, vt );
        break;
    case 'D':
        vt_index( screen_buffer, vt, rect );
        break;
    case 'E':
        screen_buffer->cursor_x = 0;
        vt_index( screen_buffer, vt, rect );
        break;
    case 'M':
        vt_reverse_index( screen_buffer, vt, rect );
        break;
    case 'c':
        vt_soft_reset( screen_buffer, vt );
        for (y = top; y <= bottom; y++) vt_fill( screen_buffer, y, 0, screen_buffer->width - 1, rect );
        vt_set_cursor( screen_buffer, 0, 0 );
        break;
    }
}

static void vt_osc( struct screen_buffer *screen_buffer, struct vt_state *vt )
{
    unsigned int i = 0, number = 0;

    while (i < vt->string_len && vt->string[i] >= '0' && vt->string[i] <= '9')
        number = number * 10 + vt->string[i++] - '0';
    if (i == vt->string_len || vt->string[i] != ';') return;
    i++;
    if (number == 0 || number == 2)
        set_vt_console_title( screen_buffer->console, &vt->string[i], (vt->string_len - i) * sizeof(WCHAR) );
}

static BOOL vt_control( struct screen_buffer *screen_buffer, struct vt_state *vt, WCHAR ch, RECT *rect )
{
    unsigned int x;

    switch (ch)
    {
    case '\b':
        x = get_bounded_cursor_x( screen_buffer );
        screen_buffer->cursor_x = x ? x - 1 : 0;
        return TRUE;
    case '\t':
        x = get_bounded_cursor_x( screen_buffer );
        screen_buffer->cursor_x = min( (x | 7) + 1, screen_buffer->width - 1 );
        return TRUE;
    case '\n':
    case '\v':
    case '\f':
        if (!(screen_buffer->mode & DISABLE_NEWLINE_AUTO_RETURN)) screen_buffer->cursor_x = 0;
        else screen_buffer->cursor_x = get_bounded_cursor_x( screen_buffer );
        vt_index( screen_buffer, vt, rect );
        return TRUE;
    case '\r':
        screen_buffer->cursor_x = 0;
        return TRUE;
    case 0x1b:
        vt->state = VT_ESCAPE;
        return TRUE;
    }
    return ch < 0x20 || ch == 0x7f;
}

void vt_write( struct screen_buffer *screen_buffer, const WCHAR *buffer, size_t len, RECT *update_rect )
{
    struct vt_state *vt = vt_get_state( screen_buffer );
    size_t i;
    WCHAR ch;

    if (!vt) return;
    for (i = 0; i < len; i++)
    {
        ch = buffer[i];
        switch (vt->state)
        {
        case VT_GROUND:
            if ((ch >= 0x20 && ch != 0x7f) || !vt_control( screen_buffer, vt, ch, update_rect ))
                vt_put( screen_buffer, vt, ch, update_rect );
            break;
        case VT_ESCAPE:
            if (ch == 0x18 || ch == 0x1a) vt->state = VT_GROUND;
            else if (ch < 0x20) vt_control( screen_buffer, vt, ch, update_rect );
            else vt_escape( screen_buffer, vt, ch, update_rect );
            break;
        case VT_CHARSET:
            if (vt->intermediate == '(') vt->pen.line_drawing = ch == '0';
            vt->state = VT_GROUND;
            break;
        case VT_CSI:
            if (ch == 0x18 || ch == 0x1a) vt->state = VT_GROUND;
            else if (ch < 0x20) vt_control( screen_buffer, vt, ch, update_rect );
            else if (ch >= '0' && ch <= '9')
            {
                if (!vt->param_started)
                {
                    if (vt->param_count < VT_MAX_PARAMS) vt->params[vt->param_count++] = 0;
                    vt->param_started = TRUE;
                }
                if (vt->param_count && vt->params[vt->param_count - 1] < 100000)
                    vt->params[vt->param_count - 1] = vt->params[vt->param_count - 1] * 10 + ch - '0';
            }
            else if (ch == ';' || ch == ':')
            {
                if (!vt->param_started && vt->param_count < VT_MAX_PARAMS) vt->params[vt->param_count++] = 0;
                vt->param_started = FALSE;
            }
            else if (ch >= '<' && ch <= '?') vt->prefix = ch;
            else if (ch >= 0x20 && ch <= 0x2f) vt->intermediate = ch;
            else if (ch >= 0x40 && ch <= 0x7e)
            {
                vt->state = VT_GROUND;
                vt_csi( screen_buffer, vt, ch, update_rect );
            }
            else vt->state = VT_GROUND;
            break;
        case VT_OSC:
        case VT_STRING:
            if (vt->string_escape)
            {
                vt->string_escape = FALSE;
                if (vt->state == VT_OSC) vt_osc( screen_buffer, vt );
                vt->state = VT_GROUND;
                if (ch != '\\') vt_escape( screen_buffer, vt, ch, update_rect );
            }
            else if (ch == 0x1b) vt->string_escape = TRUE;
            else if (ch == 0x07 || ch == 0x18 || ch == 0x1a)
            {
                if (vt->state == VT_OSC && ch == 0x07) vt_osc( screen_buffer, vt );
                vt->state = VT_GROUND;
            }
            else if (vt->state == VT_OSC && vt->string_len < VT_MAX_STRING) vt->string[vt->string_len++] = ch;
            break;
        }
    }
}

static void vt_send_key( struct console *console, const char *text, unsigned int repeat )
{
    while (repeat--) vt_reply( console, text );
}

BOOL vt_translate_key( struct console *console, const INPUT_RECORD *record )
{
    const KEY_EVENT_RECORD *key = &record->Event.KeyEvent;
    struct vt_state *vt = console->active ? console->active->vt : NULL;
    BOOL alt = (key->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
    BOOL ctrl = (key->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    unsigned int modifier = 0, number = 0;
    char final = 0, text[16];

    if (key->dwControlKeyState & SHIFT_PRESSED) modifier |= 1;
    if (alt) modifier |= 2;
    if (ctrl) modifier |= 4;

    switch (key->wVirtualKeyCode)
    {
    case VK_UP:     final = 'A'; break;
    case VK_DOWN:   final = 'B'; break;
    case VK_RIGHT:  final = 'C'; break;
    case VK_LEFT:   final = 'D'; break;
    case VK_HOME:   final = 'H'; break;
    case VK_END:    final = 'F'; break;
    case VK_F1:     final = 'P'; break;
    case VK_F2:     final = 'Q'; break;
    case VK_F3:     final = 'R'; break;
    case VK_F4:     final = 'S'; break;
    case VK_INSERT: number = 2; break;
    case VK_DELETE: number = 3; break;
    case VK_PRIOR:  number = 5; break;
    case VK_NEXT:   number = 6; break;
    case VK_F5:     number = 15; break;
    case VK_F6:     number = 17; break;
    case VK_F7:     number = 18; break;
    case VK_F8:     number = 19; break;
    case VK_F9:     number = 20; break;
    case VK_F10:    number = 21; break;
    case VK_F11:    number = 23; break;
    case VK_F12:    number = 24; break;
    case VK_BACK:
        if (!key->bKeyDown) return TRUE;
        sprintf( text, "%s%c", alt ? "\x1b" : "", ctrl ? 0x08 : 0x7f );
        vt_send_key( console, text, max( key->wRepeatCount, 1 ) );
        return TRUE;
    case VK_TAB:
        if (!(modifier & 1)) return FALSE;
        if (key->bKeyDown) vt_send_key( console, "\x1b[Z", max( key->wRepeatCount, 1 ) );
        return TRUE;
    default:
        if (!alt || ctrl || key->uChar.UnicodeChar < 0x20) return FALSE;
        if (key->bKeyDown)
        {
            WCHAR chars[2] = { 0x1b, key->uChar.UnicodeChar };
            unsigned int repeat = max( key->wRepeatCount, 1 );

            while (repeat--) vt_input( console, chars, 2 );
        }
        return TRUE;
    }

    if (!key->bKeyDown) return TRUE;
    if (final)
    {
        if (modifier) sprintf( text, "\x1b[1;%u%c", modifier + 1, final );
        else if (final >= 'P' || (vt && vt->cursor_keys_application)) sprintf( text, "\x1bO%c", final );
        else sprintf( text, "\x1b[%c", final );
    }
    else if (modifier) sprintf( text, "\x1b[%u;%u~", number, modifier + 1 );
    else sprintf( text, "\x1b[%u~", number );
    vt_send_key( console, text, max( key->wRepeatCount, 1 ) );
    return TRUE;
}
