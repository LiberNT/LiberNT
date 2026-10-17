/*
 * Copyright 1998 Alexandre Julliard
 * Copyright 2001 Eric Pouech
 * Copyright 2012 Detlef Riekenberg
 * Copyright 2020 Jacek Caban
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef RC_INVOKED

#include <stdarg.h>
#include <stdlib.h>

#include <ntstatus.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winnls.h>
#include <winternl.h>

#include "wine/condrv.h"
#include "wine/rbtree.h"

struct history_line
{
    size_t len;
    WCHAR  text[1];
};

struct font_info
{
    short int width;
    short int height;
    short int weight;
    short int pitch_family;
    WCHAR    *face_name;
    size_t    face_len;
};

struct edit_line
{
    NTSTATUS               status;              /* edit status */
    WCHAR                 *buf;                 /* the line being edited */
    unsigned int           len;                 /* number of chars in line */
    size_t                 size;                /* buffer size */
    unsigned int           cursor;              /* offset for cursor in current line */
    WCHAR                 *yanked;              /* yanked line */
    unsigned int           mark;                /* marked point (emacs mode only) */
    unsigned int           history_index;       /* history index */
    WCHAR                 *current_history;     /* buffer for the recent history entry */
    BOOL                   insert_key;          /* insert key state */
    BOOL                   insert_mode;         /* insert mode */
    unsigned int           update_begin;        /* update region */
    unsigned int           update_end;
    unsigned int           end_offset;          /* offset of the last written char */
    unsigned int           home_x;              /* home position */
    unsigned int           home_y;
    unsigned int           ctrl_mask;           /* mask for ctrl characters for completion */
};

#ifdef __REACTOS__
struct console_alias
{
    struct console_alias *next;
    WCHAR                *source;
    WCHAR                *target;
};

struct console_alias_exe
{
    struct console_alias_exe *next;
    WCHAR                    *name;
    struct console_alias     *aliases;
};
#endif

struct console
{
    HANDLE                 server;              /* console server handle */
    unsigned int           mode;                /* input mode */
    struct screen_buffer  *active;              /* active screen buffer */
    int                    is_unix;             /* UNIX terminal mode */
    int                    use_relative_cursor; /* use relative cursor positioning */
    int                    no_window;           /* don't create console window */
    INPUT_RECORD          *records;             /* input records */
    unsigned int           record_count;        /* number of input records */
    unsigned int           record_size;         /* size of input records buffer */
    int                    signaled;            /* is server in signaled state */
    WCHAR                 *read_buffer;         /* buffer of data available for read */
    size_t                 read_buffer_count;   /* size of available data */
    size_t                 read_buffer_size;    /* size of buffer */
    unsigned int           read_ioctl;          /* current read ioctl */
    size_t                 pending_read;        /* size of pending read buffer */
    struct edit_line       edit_line;           /* edit line context */
    unsigned int           key_state;
    struct console_window *window;
    WCHAR                 *title;               /* console title */
    WCHAR                 *title_orig;          /* original console title */
    struct history_line  **history;             /* lines history */
    unsigned int           history_size;        /* number of entries in history array */
    unsigned int           history_index;       /* number of used entries in history array */
    unsigned int           history_mode;        /* mode of history (non zero means remove doubled strings */
    unsigned int           edition_mode;        /* index to edition mode flavors */
    unsigned int           input_cp;            /* console input codepage */
    unsigned int           output_cp;           /* console output codepage */
    HWND                   win;                 /* window handle if backend supports it */
    HANDLE                 input_thread;        /* input thread handle */
    HANDLE                 tty_input;           /* handle to tty input stream */
    HANDLE                 tty_output;          /* handle to tty output stream */
    char                   tty_buffer[4096];    /* tty output buffer */
    size_t                 tty_buffer_count;    /* tty buffer size */
    unsigned int           tty_cursor_x;        /* tty cursor position */
    unsigned int           tty_cursor_y;
    unsigned int           tty_attr;            /* current tty char attributes */
    int                    tty_cursor_visible;  /* tty cursor visibility flag */
#ifdef __REACTOS__
    struct console_alias_exe *alias_exes;
#endif
};

#ifdef __REACTOS__
struct cell_color
{
    COLORREF fg;
    COLORREF bg;
    COLORREF underline;
};
#endif

struct screen_buffer
{
    struct console        *console;             /* console reference */
    unsigned int           id;                  /* screen buffer id */
    unsigned int           mode;                /* output mode */
    unsigned int           width;               /* size (w-h) of the screen buffer */
    unsigned int           height;
    unsigned int           cursor_size;         /* size of cursor (percentage filled) */
    unsigned int           cursor_visible;      /* cursor visibility flag */
    unsigned int           cursor_x;            /* position of cursor */
    unsigned int           cursor_y;            /* position of cursor */
    unsigned short         attr;                /* default fill attributes (screen colors) */
    unsigned short         popup_attr;          /* pop-up color attributes */
    unsigned int           max_width;           /* size (w-h) of the window given font size */
    unsigned int           max_height;
    char_info_t           *data;                /* the data for each cell - a width x height matrix */
    unsigned int           color_map[16];       /* color table */
    RECT                   win;                 /* current visible window on the screen buffer */
    struct font_info       font;                /* console font information */
    struct wine_rb_entry   entry;               /* map entry */
#ifdef __REACTOS__
    struct vt_state       *vt;
    struct cell_color     *colors;
    unsigned int           vt_cursor_size;
    unsigned char         *wrapped;
    unsigned int           wrapped_rows;
#endif
};

/* conhost.c */
NTSTATUS write_console_input( struct console *console, const INPUT_RECORD *records,
                              unsigned int count, BOOL flush );

void notify_screen_buffer_size( struct screen_buffer *screen_buffer );
NTSTATUS change_screen_buffer_size( struct screen_buffer *screen_buffer, int new_width, int new_height );
#ifdef __REACTOS__
void set_vt_console_title( struct console *console, const WCHAR *title, size_t size );

void resize_cell_colors( struct screen_buffer *screen_buffer, unsigned int new_width, unsigned int new_height );
void vt_write( struct screen_buffer *screen_buffer, const WCHAR *buffer, size_t len, RECT *update_rect );
void vt_destroy( struct screen_buffer *screen_buffer );

#define CELL_WIDE_FLAGS (COMMON_LVB_LEADING_BYTE | COMMON_LVB_TRAILING_BYTE)

unsigned int console_char_width( unsigned int ch );
#define PUT_CELLS_LEFT  1
#define PUT_CELLS_RIGHT 2

#define ROW_WRAPPED        1
#define ROW_WRAPPED_PADDED 2

void set_row_wrapped( struct screen_buffer *screen_buffer, unsigned int y, unsigned char wrapped );
void move_wrapped_rows( struct screen_buffer *screen_buffer, unsigned int dst, unsigned int src, unsigned int count );
BOOL reflow_screen_buffer( struct screen_buffer *screen_buffer, unsigned int new_width );
BOOL vt_main_screen( struct screen_buffer *screen_buffer );

int break_wide_cell( struct screen_buffer *screen_buffer, unsigned int index );
unsigned int put_char_cells( struct screen_buffer *screen_buffer, unsigned int index, WCHAR lead, WCHAR trail,
                             unsigned short attr, unsigned int width );
BOOL vt_translate_key( struct console *console, const INPUT_RECORD *record );
BOOL vt_translate_mouse( struct console *console, UINT msg, WPARAM wparam, LPARAM lparam );
void vt_focus_event( struct console *console, BOOL focused );
void vt_bracket_paste( struct console *console, BOOL begin );
#endif

/* window.c */
void update_console_font( struct console *console, const WCHAR *face_name, size_t face_name_size,
                          unsigned int height, unsigned int weight );
BOOL init_window( struct console *console );
void init_message_window( struct console *console );
void update_window_region( struct console *console, const RECT *update );
void update_window_config( struct console *console, BOOL delay );
void teardown_window( struct console *console );
#ifdef __REACTOS__
void set_window_quick_edit( struct console *console, BOOL enable );
void update_window_title( struct console *console );
#endif

static inline void empty_update_rect( struct screen_buffer *screen_buffer, RECT *rect )
{
    SetRect( rect, screen_buffer->width, screen_buffer->height, 0, 0 );
}

static inline unsigned int get_bounded_cursor_x( struct screen_buffer *screen_buffer )
{
    return min( screen_buffer->cursor_x, screen_buffer->width - 1 );
}

#ifdef __REACTOS__
static inline void reset_cell_colors( struct screen_buffer *screen_buffer, unsigned int first, unsigned int count )
{
    if (screen_buffer->colors)
        memset( &screen_buffer->colors[first], 0xff, count * sizeof(*screen_buffer->colors) );
}

static inline void move_cell_colors( struct screen_buffer *screen_buffer, unsigned int dst, unsigned int src,
                                     unsigned int count )
{
    if (screen_buffer->colors)
        memmove( &screen_buffer->colors[dst], &screen_buffer->colors[src], count * sizeof(*screen_buffer->colors) );
}
#endif

#endif /* RC_INVOKED */

/* strings */
#define IDS_EDIT                0x100
#define IDS_DEFAULT             0x101
#define IDS_PROPERTIES          0x102

#define IDS_MARK                0x110
#define IDS_COPY                0x111
#define IDS_PASTE               0x112
#define IDS_SELECTALL           0x113
#define IDS_SCROLL              0x114
#define IDS_SEARCH              0x115

#define IDS_DLG_TIT_DEFAULT     0x120
#define IDS_DLG_TIT_CURRENT     0x121
#define IDS_DLG_TIT_ERROR       0x122

#define IDS_DLG_ERR_SBWINSIZE   0x130

#define IDS_FNT_DISPLAY         0x200
#define IDS_FNT_PREVIEW         0x201

/* dialog boxes */
#define IDD_OPTION              0x0100
#define IDD_FONT                0x0200
#define IDD_CONFIG              0x0300

/* dialog boxes controls */
#define IDC_OPT_CURSOR_SMALL    0x0101
#define IDC_OPT_CURSOR_MEDIUM   0x0102
#define IDC_OPT_CURSOR_LARGE    0x0103
#define IDC_OPT_HIST_SIZE       0x0104
#define IDC_OPT_HIST_SIZE_UD    0x0105
#define IDC_OPT_HIST_NODOUBLE   0x0106
#define IDC_OPT_CONF_CTRL       0x0107
#define IDC_OPT_CONF_SHIFT      0x0108
#define IDC_OPT_QUICK_EDIT      0x0109
#define IDC_OPT_INSERT_MODE     0x0110

#define IDC_FNT_LIST_FONT       0x0201
#define IDC_FNT_LIST_SIZE       0x0202
#define IDC_FNT_COLOR_BK        0x0203
#define IDC_FNT_COLOR_FG        0x0204
#define IDC_FNT_FONT_INFO       0x0205
#define IDC_FNT_PREVIEW         0x0206

#define IDC_CNF_SB_WIDTH        0x0301
#define IDC_CNF_SB_WIDTH_UD     0x0302
#define IDC_CNF_SB_HEIGHT       0x0303
#define IDC_CNF_SB_HEIGHT_UD    0x0304
#define IDC_CNF_WIN_WIDTH       0x0305
#define IDC_CNF_WIN_WIDTH_UD    0x0306
#define IDC_CNF_WIN_HEIGHT      0x0307
#define IDC_CNF_WIN_HEIGHT_UD   0x0308
#define IDC_CNF_CLOSE_EXIT      0x0309
#define IDC_CNF_EDITION_MODE    0x030a
