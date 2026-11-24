/* LiberNT Paint: experimental composited raster layers and docked panel.
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * The original imageModel remains the bottom (background) layer.
 * Layers above it store premultiplied BGRA pixels with independent alpha.
 */
#include "precomp.h"
#include "modern_layers.h"
#include <stdint.h>
#include <string.h>

// Minimal owning array for LiberNT's -nostdinc C++ build. The array uses
// element copy constructors and assignment, never memcpy on class objects.
template<class T> class PaintArray
{
    T *items;
    size_t length, capacity;
public:
    PaintArray() : items(NULL), length(0), capacity(0) {}
    PaintArray(size_t n, const T& value = T()) : items(NULL), length(0), capacity(0)
        { resize(n, value); }
    PaintArray(const PaintArray& other) : items(NULL), length(0), capacity(0)
        { assign(other); }
    ~PaintArray() { delete[] items; }
    PaintArray& operator=(const PaintArray& other)
        { if (this != &other) { PaintArray temp(other); swap(temp); } return *this; }
    void assign(const PaintArray& other)
        { resize(other.length); for (size_t i = 0; i < length; ++i) items[i] = other.items[i]; }
    bool reserve(size_t n)
    {
        if (n <= capacity) return true;
        T *next = new T[n];
        for (size_t i = 0; i < length; ++i) next[i] = items[i];
        delete[] items;
        items = next; capacity = n;
        return true;
    }
    bool resize(size_t n, const T& value = T())
    {
        if (n > capacity) reserve(n);
        for (size_t i = length; i < n; ++i) items[i] = value;
        for (size_t i = n; i < length; ++i) items[i] = T();
        length = n;
        return true;
    }
    bool push_back(const T& value)
    {
        // Copy before expanding, since value can refer to an element here.
        T copy(value);
        if (length == capacity) reserve(capacity ? capacity * 2 : 4);
        items[length++] = copy;
        return true;
    }
    void pop_back() { if (length) { --length; items[length] = T(); } }
    void clear() { resize(0); }
    bool empty() const { return length == 0; }
    size_t size() const { return length; }
    T& operator[](size_t i) { return items[i]; }
    const T& operator[](size_t i) const { return items[i]; }
    T& back() { return items[length - 1]; }
    const T& back() const { return items[length - 1]; }
    T* begin() { return items; }
    const T* begin() const { return items; }
    T* end() { return items ? items + length : NULL; }
    const T* end() const { return items ? items + length : NULL; }
    void swap(PaintArray& other)
    {
        T *ptr = items; items = other.items; other.items = ptr;
        size_t n = length; length = other.length; other.length = n;
        n = capacity; capacity = other.capacity; other.capacity = n;
    }
    bool insert(T *position, const T& value)
    {
        size_t offset = items ? (size_t)(position - items) : 0;
        if (offset > length) return false;
        T copy(value);
        if (length == capacity) reserve(capacity ? capacity * 2 : 4);
        for (size_t i = length; i > offset; --i) items[i] = items[i - 1];
        items[offset] = copy; ++length;
        return true;
    }
    void erase(T *position)
    {
        if (!items || position < items || position >= items + length) return;
        size_t offset = (size_t)(position - items);
        for (size_t i = offset; i + 1 < length; ++i) items[i] = items[i + 1];
        --length; items[length] = T();
    }
};

namespace
{
    const WCHAR kPanelClass[] = L"LiberNTPaintLayersPanel";
    const INT kPanelWidth = 232;
    const UINT kProjectMagic = 0x544E504C; // LPNT in little endian
    const UINT kProjectVersion = 2; // adds base visibility immediately after header
    const size_t kMaxLayers = 64;

    struct Layer
    {
        CStringW name;
        PaintArray<uint32_t> pixels; // BGRA premultiplied; zero = transparent
        bool visible;
        BYTE opacity;
        Layer() : visible(true), opacity(255) { }
    };

    struct Snapshot
    {
        PaintArray<Layer> layers;
        int active;
        bool baseVisible;
    };

    struct State
    {
        HWND parent;
        HWND panel;
        HWND canvas;
        WNDPROC oldCanvasProc;
        PaintArray<Layer*> layers; // first vector entry is overlay 1; imageModel is base
        PaintArray<Snapshot> undo, redo;
        int active; // 0 = imageModel (background), i > 0 = layers[i-1]
        int width, height;
        bool shown, drawing;
        bool baseVisible, wandMode, wandGlobal;
        int tolerance;
        PaintArray<BYTE> selection;
        POINT previous;
        State() : parent(NULL), panel(NULL), canvas(NULL), oldCanvasProc(NULL),
                  active(0), width(0), height(0), shown(false), drawing(false),
                  baseVisible(true), wandMode(false), wandGlobal(false), tolerance(25)
        { previous.x = previous.y = 0; }
    } g;

    template<typename T> T Lower(T a, T b) { return a < b ? a : b; }
    template<typename T> T Upper(T a, T b) { return a > b ? a : b; }

    bool ValidSize(int width, int height)
    {
        return width > 0 && height > 0 && width <= 16384 && height <= 16384 &&
               (uint64_t)width * (uint64_t)height <= 64000000ULL;
    }

    RECT Rc(int x, int y, int w, int h)
    {
        RECT r = { x, y, x + w, y + h };
        return r;
    }

    void Fill(HDC dc, RECT rc, COLORREF color)
    {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
    }

    void Text(HDC dc, LPCWSTR text, RECT rc, COLORREF color, UINT align = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
    {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        DrawTextW(dc, text, -1, &rc, align);
    }

    void Refresh()
    {
        if (IsWindow(g.panel)) InvalidateRect(g.panel, NULL, FALSE);
        if (IsWindow(g.canvas)) InvalidateRect(g.canvas, NULL, FALSE);
    }

    void ClearLayers()
    {
        for (size_t i = 0; i < g.layers.size(); ++i) delete g.layers[i];
        g.layers.clear();
        g.undo.clear(); g.redo.clear();
        g.active = 0;
        g.selection.clear();
        g.baseVisible = true;
    }

    Snapshot TakeSnapshot()
    {
        Snapshot snapshot;
        snapshot.active = g.active;
        snapshot.baseVisible = g.baseVisible;
        for (size_t i=0; i<g.layers.size(); ++i)
            snapshot.layers.push_back(*g.layers[i]);
        return snapshot;
    }

    void RestoreSnapshot(const Snapshot& snapshot)
    {
        // Retain prior undo/redo stacks when replacing layer content.
        for (size_t i=0; i<g.layers.size(); ++i) delete g.layers[i];
        g.layers.clear();
        for (size_t i=0; i<snapshot.layers.size(); ++i)
            g.layers.push_back(new Layer(snapshot.layers[i]));
        g.active = snapshot.active;
        g.baseVisible = snapshot.baseVisible;
        g.selection.clear();
        g_imageSaved = FALSE;
        Refresh();
    }

    void PushUndo()
    {
        if (g.undo.size() >= 10) g.undo.erase(g.undo.begin());
        g.undo.push_back(TakeSnapshot());
        g.redo.clear();
    }

    void SyncSize()
    {
        const int w = imageModel.GetWidth(), h = imageModel.GetHeight();
        if (w == g.width && h == g.height) return;
        if (!ValidSize(w, h)) return;
        for (size_t i = 0; i < g.layers.size(); ++i)
        {
            Layer *layer = g.layers[i];
            PaintArray<uint32_t> resized((size_t)w * h, 0);
            const int copyW = Lower(g.width, w), copyH = Lower(g.height, h);
            for (int y = 0; y < copyH; ++y)
                memcpy(&resized[(size_t)y * w],
                       &layer->pixels[(size_t)y * g.width],
                       (size_t)copyW * sizeof(uint32_t));
            layer->pixels.swap(resized);
        }
        g.width = w; g.height = h;
        g.selection.clear();
    }

    Layer* ActiveLayer()
    {
        return g.active > 0 && (size_t)g.active <= g.layers.size() ?
                   g.layers[g.active - 1] : NULL;
    }

    uint32_t Pack(COLORREF c)
    {
        return 0xff000000U | ((uint32_t)GetRValue(c) << 16) |
               ((uint32_t)GetGValue(c) << 8) | (uint32_t)GetBValue(c);
    }

    void Plot(Layer *layer, int x, int y, bool erase)
    {
        if (!layer || x < 0 || y < 0 || x >= g.width || y >= g.height) return;
        layer->pixels[(size_t)y * g.width + x] = erase ? 0 : Pack(paletteModel.GetFgColor());
    }

    void Stroke(Layer *layer, POINT a, POINT b, bool erase, int width)
    {
        int dx = (int)(b.x - a.x), dy = (int)(b.y - a.y);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
        int err = dx - dy, x = a.x, y = a.y;
        width = Upper(1, Lower(width, 64));
        for (;;) {
            for (int yy = -(width/2); yy <= width/2; ++yy)
                for (int xx = -(width/2); xx <= width/2; ++xx)
                    if (xx*xx + yy*yy <= (width*width+1)/4)
                        Plot(layer, x+xx, y+yy, erase);
            if (x == b.x && y == b.y) break;
            int e = err * 2;
            if (e > -dy) { err -= dy; x += sx; }
            if (e < dx) { err += dx; y += sy; }
        }
    }

    void AlphaComposite(uint32_t *dst, const PaintArray<uint32_t>& src, BYTE opacity)
    {
        const size_t count = (size_t)g.width * g.height;
        for (size_t p = 0; p < count; ++p)
        {
            const uint32_t pixel = src[p];
            const unsigned a = ((pixel >> 24) & 255U) * opacity / 255U;
            if (!a) continue;
            const unsigned inv = 255U - a;
            unsigned r = (((pixel >> 16) & 255U) * opacity + 127) / 255;
            unsigned green = (((pixel >> 8) & 255U) * opacity + 127) / 255;
            unsigned blue = ((pixel & 255U) * opacity + 127) / 255;
            const uint32_t d = dst[p];
            r = Lower(255U, r + (((d >> 16) & 255U) * inv + 127) / 255);
            green = Lower(255U, green + (((d >> 8) & 255U) * inv + 127) / 255);
            blue = Lower(255U, blue + ((d & 255U) * inv + 127) / 255);
            dst[p] = 0xff000000U | (r << 16) | (green << 8) | blue;
        }
    }

    HBITMAP CreateBGRA(const uint32_t *src, int width, int height)
    {
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void *pixels = NULL;
        HBITMAP result = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pixels, NULL, 0);
        if (result && pixels)
            memcpy(pixels, src, (size_t)width * height * sizeof(uint32_t));
        return result;
    }

    bool ReadBase(PaintArray<uint32_t>& dest)
    {
        SyncSize();
        if (!ValidSize(g.width, g.height)) return false;
        dest.resize((size_t)g.width * g.height);
        HBITMAP copy = imageModel.CopyBitmap();
        if (!copy) return false;
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = g.width;
        bmi.bmiHeader.biHeight = -g.height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        HDC dc = GetDC(NULL);
        int rows = GetDIBits(dc, copy, 0, g.height, &dest[0], &bmi, DIB_RGB_COLORS);
        ReleaseDC(NULL, dc);
        DeleteObject(copy);
        if (rows != g.height) return false;
        for (size_t i = 0; i < dest.size(); ++i) dest[i] |= 0xff000000U;
        return true;
    }

    bool Composite(PaintArray<uint32_t>& result)
    {
        if (!ReadBase(result)) return false;
        for (size_t i = 0; i < g.layers.size(); ++i)
            if (g.layers[i]->visible)
                AlphaComposite(&result[0], g.layers[i]->pixels, g.layers[i]->opacity);
        return true;
    }

    // Render into Paint's own offscreen image DC. The canvas will perform
    // the final StretchBlt exactly once, avoiding a second visible repaint.
    // A premultiplied BGRA compositor that keeps destination alpha.
    void AlphaOver(uint32_t *dst, const PaintArray<uint32_t>& src, BYTE opacity)
    {
        size_t n=(size_t)g.width*g.height;
        for(size_t i=0;i<n;++i)
        {
            uint32_t s=src[i], d=dst[i];
            unsigned sa=((s>>24)&255U)*opacity/255U;
            if(!sa) continue;
            unsigned inv=255U-sa;
            unsigned a=sa+(((d>>24)&255U)*inv+127)/255U;
            unsigned r=Lower(255U, (((s>>16)&255U)*opacity+127)/255U +
                         (((d>>16)&255U)*inv+127)/255U);
            unsigned gg=Lower(255U, (((s>>8)&255U)*opacity+127)/255U +
                         (((d>>8)&255U)*inv+127)/255U);
            unsigned b=Lower(255U, ((s&255U)*opacity+127)/255U +
                         ((d&255U)*inv+127)/255U);
            dst[i]=(a<<24)|(r<<16)|(gg<<8)|b;
        }
    }

    bool CompositeRGBA(PaintArray<uint32_t>& pixels)
    {
        SyncSize();
        if(!ValidSize(g.width,g.height)) return false;
        if(g.baseVisible)
        {
            if(!ReadBase(pixels)) return false;
        }
        else pixels.resize((size_t)g.width*g.height,0U);
        for(size_t j=0;j<g.layers.size();++j)
            if(g.layers[j]->visible) AlphaOver(&pixels[0],g.layers[j]->pixels,g.layers[j]->opacity);
        return true;
    }

    bool CompositePreview(PaintArray<uint32_t>& result)
    {
        PaintArray<uint32_t> layerPixels;
        if(!CompositeRGBA(layerPixels)) return false;
        result.resize(layerPixels.size(),0U);
        for(int y=0;y<g.height;++y)
            for(int x=0;x<g.width;++x)
            {
                size_t i=(size_t)y*g.width+x;
                uint32_t p=layerPixels[i];
                unsigned a=(p>>24)&255U, inv=255U-a;
                unsigned bg=((x/12+y/12)&1)?222U:255U;
                unsigned r=Lower(255U,((p>>16)&255U)+bg*inv/255U);
                unsigned gg=Lower(255U,((p>>8)&255U)+bg*inv/255U);
                unsigned b=Lower(255U,(p&255U)+bg*inv/255U);
                result[i]=0xff000000U|(r<<16)|(gg<<8)|b;
            }
        return true;
    }

    // Selection outline is drawn onto Paint's backing DC, never the screen DC.
    void DrawSelection(HDC dc)
    {
        if(g.selection.size()!=(size_t)g.width*g.height) return;
        size_t count=0;
        for(int y=0;y<g.height;++y)
            for(int x=0;x<g.width;++x)
            {
                size_t i=(size_t)y*g.width+x;
                if(!g.selection[i]) continue;
                bool edge=x==0 || y==0 || x==g.width-1 || y==g.height-1 ||
                    !g.selection[i-1] || !g.selection[i+1] ||
                    !g.selection[i-g.width] || !g.selection[i+g.width];
                if(edge && (count++ % 2)==0)
                    SetPixelV(dc,x,y,RGB(36,86,175));
            }
    }

    void CompositeIntoDC(HDC destination)
    {
        if (!destination || (g.layers.empty() && g.baseVisible)) return;
        SyncSize();
        PaintArray<uint32_t> pixels;
        if (!CompositePreview(pixels)) return;
        HBITMAP bitmap = CreateBGRA(&pixels[0], g.width, g.height);
        if (!bitmap) return;
        HDC source = CreateCompatibleDC(destination);
        if (source)
        {
            HGDIOBJ old = SelectObject(source, bitmap);
            BitBlt(destination, 0, 0, g.width, g.height,
                   source, 0, 0, SRCCOPY);
            SelectObject(source, old);
            DeleteDC(source);
            DrawSelection(destination);
        }
        DeleteObject(bitmap);
    }

    // Wand matches an active overlay's unmodified premultiplied BGRA pixels.
    // Tolerance is the maximum per-channel difference in 0..255 units.
    bool SelectWand(POINT pt)
    {
        Layer *layer=ActiveLayer();
        if(!layer || pt.x<0 || pt.y<0 || pt.x>=g.width || pt.y>=g.height) return false;
        const size_t count=(size_t)g.width*g.height;
        if(count>16000000U || layer->pixels.size()!=count)
        {
            MessageBoxW(g.parent,L"Wand selection is limited to 16 million pixels.",L"Paint",MB_OK|MB_ICONWARNING);
            return false;
        }
        const size_t start=(size_t)pt.y*g.width+pt.x;
        const uint32_t target=layer->pixels[start];
        PaintArray<BYTE> mask(count,0);
        const unsigned limit=(unsigned)(g.tolerance*255/100);
        if(g.wandGlobal)
        {
            for(size_t i=0;i<count;++i)
            {
                uint32_t value=layer->pixels[i];
                unsigned difference=0;
                for(int sh=0;sh<=24;sh+=8)
                {
                    int d=(int)((value>>sh)&255)-(int)((target>>sh)&255);
                    if(d<0)d=-d;
                    if((unsigned)d>difference)difference=(unsigned)d;
                }
                if(difference<=limit)mask[i]=1;
            }
        }
        else
        {
            // Mark when enqueued so the queue cannot grow beyond pixel count.
            PaintArray<uint32_t> queue(count,0U);
            size_t read=0,write=0;
            queue[write++]=(uint32_t)start;
            mask[start]=1;
            while(read<write)
            {
                uint32_t i=queue[read++];
                uint32_t candidates[4];
                unsigned n=0;
                if(i>= (uint32_t)g.width)candidates[n++]=i-(uint32_t)g.width;
                if(i+(uint32_t)g.width<count)candidates[n++]=i+(uint32_t)g.width;
                if(i%(uint32_t)g.width)candidates[n++]=i-1;
                if(i%(uint32_t)g.width!=(uint32_t)g.width-1)candidates[n++]=i+1;
                for(unsigned k=0;k<n;++k)
                {
                    uint32_t j=candidates[k];
                    if(mask[j])continue;
                    uint32_t value=layer->pixels[j];
                    unsigned difference=0;
                    for(int sh=0;sh<=24;sh+=8)
                    {
                        int d=(int)((value>>sh)&255)-(int)((target>>sh)&255);
                        if(d<0)d=-d;
                        if((unsigned)d>difference)difference=(unsigned)d;
                    }
                    if(difference<=limit)
                    {
                        mask[j]=1;
                        queue[write++]=j;
                    }
                }
            }
        }
        g.selection.swap(mask);
        Refresh();
        return true;
    }

    void DeleteSelection()
    {
        Layer *layer=ActiveLayer();
        if(!layer || g.selection.size()!=layer->pixels.size())return;
        PushUndo();
        for(size_t i=0;i<g.selection.size();++i)
            if(g.selection[i])layer->pixels[i]=0U;
        g_imageSaved=FALSE;
        Refresh();
    }

    bool StartDrawing(POINT pt)
    {
        Layer *layer = ActiveLayer();
        if (!layer) return false;
        TOOLTYPE tool = toolsModel.GetActiveTool();
        if (tool != TOOL_PEN && tool != TOOL_BRUSH && tool != TOOL_RUBBER &&
            tool != TOOL_AIRBRUSH) return false; // other tools retain legacy behavior on base
        canvasWindow.CanvasToImage(pt);
        PushUndo();
        g.previous = pt;
        g.drawing = true;
        SetCapture(g.canvas);
        g_imageSaved = FALSE;
        Stroke(layer, pt, pt, tool == TOOL_RUBBER,
               tool == TOOL_BRUSH ? toolsModel.GetBrushWidth() :
               tool == TOOL_RUBBER ? toolsModel.GetRubberRadius() * 2 :
               tool == TOOL_AIRBRUSH ? 4 : toolsModel.GetPenWidth());
        Refresh();
        return true;
    }

    LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        WNDPROC original = g.oldCanvasProc;
        if (!original) return DefWindowProcW(hwnd, msg, wp, lp);
        if (msg == WM_LBUTTONDOWN && g.active > 0)
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (g.wandMode)
            {
                canvasWindow.CanvasToImage(pt);
                SelectWand(pt);
                SetFocus(hwnd);
                return 0;
            }
            if (StartDrawing(pt)) return 0;
            // Unsupported tools do not modify background accidentally.
            return 0;
        }
        if (g.active > 0 && g.drawing && msg == WM_MOUSEMOVE)
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            canvasWindow.CanvasToImage(pt);
            Layer *layer = ActiveLayer();
            TOOLTYPE tool = toolsModel.GetActiveTool();
            if (layer) Stroke(layer, g.previous, pt, tool == TOOL_RUBBER,
                 tool == TOOL_BRUSH ? toolsModel.GetBrushWidth() :
                 tool == TOOL_RUBBER ? toolsModel.GetRubberRadius() * 2 :
                 tool == TOOL_AIRBRUSH ? 4 : toolsModel.GetPenWidth());
            g.previous = pt;
            Refresh();
            return 0;
        }
        if (g.active > 0 && (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE))
        {
            if (g.drawing)
            {
                g.drawing = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                Refresh();
            }
            if (msg == WM_LBUTTONUP) return 0;
        }
        if (msg == WM_KEYDOWN && g.active > 0)
        {
            if(wp==VK_DELETE || wp==VK_BACK) { DeleteSelection(); return 0; }
            if(wp==VK_ESCAPE) { g.selection.clear(); Refresh(); return 0; }
        }
        if (msg == WM_NCDESTROY)
        {
            g.oldCanvasProc = NULL; g.canvas = NULL;
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)original);
            return CallWindowProcW(original, hwnd, msg, wp, lp);
        }
        return CallWindowProcW(original, hwnd, msg, wp, lp);
    }

    bool AddLayer()
    {
        SyncSize();
        if (g.layers.size() >= kMaxLayers || !ValidSize(g.width, g.height)) return false;
        Layer *layer = new Layer();
        if (!layer) return false;
        PushUndo();
        layer->pixels.resize((size_t)g.width * g.height, 0);
        WCHAR name[48];
        wsprintfW(name, L"Layer %u", (unsigned)(g.layers.size() + 1));
        layer->name = name;
        g.layers.push_back(layer);
        g.active = (int)g.layers.size();
        g.selection.clear();
        g_imageSaved = FALSE;
        Refresh();
        return true;
    }

    void DuplicateLayer()
    {
        if (g.active <= 0 || g.layers.size() >= kMaxLayers) return;
        Layer *copy = new Layer(*ActiveLayer());
        if (!copy) return;
        PushUndo();
        copy->name += L" copy";
        g.layers.insert(g.layers.begin() + g.active, copy);
        ++g.active;
        g_imageSaved = FALSE;
        Refresh();
    }

    void DeleteLayer()
    {
        if (g.active <= 0) return;
        PushUndo();
        delete g.layers[g.active - 1];
        g.layers.erase(g.layers.begin() + g.active - 1);
        g.active = Lower(g.active, (int)g.layers.size());
        g.selection.clear();
        g_imageSaved = FALSE;
        Refresh();
    }

    void Reorder(int direction)
    {
        int current = g.active - 1, target = current + direction;
        if (current < 0 || target < 0 || target >= (int)g.layers.size()) return;
        PushUndo();
        Layer *tmp = g.layers[current]; g.layers[current] = g.layers[target]; g.layers[target] = tmp;
        g.active = target + 1;
        g_imageSaved = FALSE;
        Refresh();
    }

    // Merge overlay down into next lower overlay; the base background cannot be
    // merged without first flattening, so reject that operation for layer 1.
    void MergeDown()
    {
        if (g.active <= 1) return;
        Layer *top = g.layers[g.active - 1];
        Layer *lower = g.layers[g.active - 2];
        if (!top->visible || !lower->visible) return;
        PushUndo();
        // Merge into premultiplied pixels, preserving destination alpha.
        for (size_t i = 0; i < top->pixels.size(); ++i)
        {
            uint32_t a = top->pixels[i], b = lower->pixels[i];
            unsigned sa = ((a >> 24) & 255U) * top->opacity / 255U;
            unsigned da = ((b >> 24) & 255U) * lower->opacity / 255U;
            unsigned outA = sa + da * (255U - sa) / 255U;
            unsigned r = (((a >> 16)&255U) * top->opacity + 127)/255U +
                         (((b >> 16)&255U) * lower->opacity * (255U-sa) + 32512)/65025U;
            unsigned green = (((a >> 8)&255U) * top->opacity + 127)/255U +
                         (((b >> 8)&255U) * lower->opacity * (255U-sa) + 32512)/65025U;
            unsigned blue = ((a&255U) * top->opacity + 127)/255U +
                         ((b&255U) * lower->opacity * (255U-sa) + 32512)/65025U;
            lower->pixels[i] = (outA<<24) | (Lower(255U,r)<<16) |
                               (Lower(255U,green)<<8) | Lower(255U,blue);
        }
        lower->opacity = 255;
        lower->visible = lower->visible || top->visible;
        delete top;
        g.layers.erase(g.layers.begin() + g.active - 1);
        --g.active;
        g.selection.clear();
        g_imageSaved = FALSE;
        Refresh();
    }

    bool WriteExact(HANDLE h, const void *bytes, DWORD count)
    {
        DWORD written = 0;
        return WriteFile(h, bytes, count, &written, NULL) && written == count;
    }
    bool ReadExact(HANDLE h, void *bytes, DWORD count)
    {
        DWORD read = 0;
        return ReadFile(h, bytes, count, &read, NULL) && read == count;
    }

    bool ProjectFile(HWND owner, bool save)
    {
        WCHAR path[MAX_PATH] = L"";
        OPENFILENAMEW dlg = { 0 };
        dlg.lStructSize = sizeof(dlg);
        dlg.hwndOwner = owner;
        dlg.lpstrFile = path;
        dlg.nMaxFile = _countof(path);
        dlg.lpstrFilter = L"LiberNT Paint layers (*.lpaint)\0*.lpaint\0All files\0*.*\0";
        dlg.lpstrDefExt = L"lpaint";
        dlg.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (!(save ? GetSaveFileNameW(&dlg) : GetOpenFileNameW(&dlg))) return false;
        HANDLE h = CreateFileW(path, save ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ,
                               NULL, save ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return false;
        bool ok = false;
        if (save)
        {
            PaintArray<uint32_t> base;
            if (ReadBase(base))
            {
                DWORD header[5] = { kProjectMagic, kProjectVersion, (DWORD)g.width,
                                    (DWORD)g.height, (DWORD)g.layers.size() };
                DWORD baseFlag=g.baseVisible?1U:0U;
                ok = WriteExact(h, header, sizeof(header)) &&
                     WriteExact(h, &baseFlag, sizeof(baseFlag)) &&
                     WriteExact(h, &base[0], (DWORD)(base.size() * sizeof(uint32_t)));
                for (size_t i = 0; ok && i < g.layers.size(); ++i)
                {
                    Layer *layer = g.layers[i];
                    DWORD meta[3] = { (DWORD)(size_t)layer->name.GetLength(), (DWORD)layer->visible,
                                      (DWORD)layer->opacity };
                    ok = meta[0] <= 255 && WriteExact(h, meta, sizeof(meta)) &&
                         WriteExact(h, layer->name.GetString(), meta[0] * sizeof(WCHAR)) &&
                         WriteExact(h, &layer->pixels[0], (DWORD)(layer->pixels.size() * 4));
                }
            }
        }
        else
        {
            DWORD header[5] = { 0 };
            ok = ReadExact(h, header, sizeof(header)) && header[0] == kProjectMagic &&
                 (header[1] == 1 || header[1] == kProjectVersion) &&
                 ValidSize(header[2], header[3]) && header[4] <= kMaxLayers;
            DWORD loadedBaseFlag=1;
            if(ok && header[1]>=2) ok=ReadExact(h,&loadedBaseFlag,sizeof(loadedBaseFlag)) && loadedBaseFlag<=1;
            PaintArray<uint32_t> base;
            PaintArray<Layer*> loaded;
            if (ok)
            {
                base.resize((size_t)header[2] * header[3]);
                ok = ReadExact(h, &base[0], (DWORD)(base.size() * 4));
            }
            for (DWORD i = 0; ok && i < header[4]; ++i)
            {
                Layer *layer = new Layer();
                if (!layer) { ok = false; break; }
                DWORD meta[3] = { 0 };
                ok = ReadExact(h, meta, sizeof(meta)) && meta[0] <= 255 &&
                     meta[2] <= 255;
                if (ok)
                {
                    PaintArray<WCHAR> name(meta[0]);
                    if (meta[0]) ok = ReadExact(h, &name[0], meta[0] * sizeof(WCHAR));
                    if (ok && meta[0]) layer->name.SetString(&name[0], (int)meta[0]);
                    layer->visible = meta[1] != 0;
                    layer->opacity = (BYTE)meta[2];
                    layer->pixels.resize((size_t)header[2] * header[3]);
                    if (ok) ok = ReadExact(h, &layer->pixels[0], (DWORD)(layer->pixels.size() * 4));
                }
                if (ok) loaded.push_back(layer); else delete layer;
            }
            if (ok)
            {
                HBITMAP bitmap = CreateBGRA(&base[0], header[2], header[3]);
                if (!bitmap) ok = false;
                else
                {
                    imageModel.PushImageForUndo(bitmap);
                    ClearLayers();
                    g.layers.swap(loaded);
                    g.baseVisible=loadedBaseFlag!=0;
                    g.width = header[2]; g.height = header[3];
                    g.active = (int)g.layers.size();
                    Refresh();
                }
            }
            for (size_t i = 0; i < loaded.size(); ++i) delete loaded[i];
        }
        CloseHandle(h);
        if (!ok) MessageBoxW(owner, L"Could not read or write the layered Paint project.",
                             L"Paint", MB_ICONERROR | MB_OK);
        return ok;
    }

    // Self-contained lossless RGBA PNG writer. DEFLATE stored blocks are valid,
    // larger than compressed output, and avoid a new zlib/CRT build dependency.
    void AppendBE(PaintArray<BYTE>& out,uint32_t v)
    {
        out.push_back((BYTE)(v>>24));out.push_back((BYTE)(v>>16));
        out.push_back((BYTE)(v>>8));out.push_back((BYTE)v);
    }
    uint32_t UpdateCRC(uint32_t crc,const BYTE *bytes,size_t len)
    {
        for(size_t i=0;i<len;++i)
        {
            crc^=bytes[i];
            for(int j=0;j<8;++j)crc=(crc>>1)^((crc&1)?0xedb88320U:0U);
        }
        return crc;
    }
    bool WriteChunk(HANDLE file,const char* type,const BYTE* bytes,DWORD size)
    {
        BYTE length[4]={(BYTE)(size>>24),(BYTE)(size>>16),(BYTE)(size>>8),(BYTE)size};
        if(!WriteExact(file,length,4) || !WriteExact(file,type,4)) return false;
        if(size && !WriteExact(file,bytes,size))return false;
        uint32_t crc=UpdateCRC(0xffffffffU,(const BYTE*)type,4);
        if(size)crc=UpdateCRC(crc,bytes,size);
        crc^=0xffffffffU;
        BYTE sum[4]={(BYTE)(crc>>24),(BYTE)(crc>>16),(BYTE)(crc>>8),(BYTE)crc};
        return WriteExact(file,sum,4);
    }
    bool SavePNG(HWND owner)
    {
        PaintArray<uint32_t> image;
        if(!CompositeRGBA(image))return false;
        // Cap memory for uncompressed rows and zlib data; no partial output.
        if(image.size()>8000000U)
        {
            MessageBoxW(owner,L"PNG export is limited to 8 million pixels.",L"Paint",MB_OK|MB_ICONWARNING);
            return false;
        }
        WCHAR path[MAX_PATH]=L"";
        OPENFILENAMEW dialog={0};
        dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;
        dialog.lpstrFile=path;dialog.nMaxFile=_countof(path);
        dialog.lpstrFilter=L"PNG image (*.png)\0*.png\0";
        dialog.lpstrDefExt=L"png";
        dialog.Flags=OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;
        if(!GetSaveFileNameW(&dialog))return false;
        PaintArray<BYTE> raw;
        size_t stride=(size_t)g.width*4+1;
        raw.resize(stride*g.height,0);
        for(int y=0;y<g.height;++y)
            for(int x=0;x<g.width;++x)
            {
                uint32_t p=image[(size_t)y*g.width+x];
                unsigned a=(p>>24)&255U;
                size_t off=(size_t)y*stride+1+(size_t)x*4;
                // Convert premultiplied RGB to ordinary PNG RGBA.
                raw[off]=(BYTE)(a?Lower(255U,(((p>>16)&255U)*255U+a/2)/a):0);
                raw[off+1]=(BYTE)(a?Lower(255U,(((p>>8)&255U)*255U+a/2)/a):0);
                raw[off+2]=(BYTE)(a?Lower(255U,((p&255U)*255U+a/2)/a):0);
                raw[off+3]=(BYTE)a;
            }
        PaintArray<BYTE> zlib;
        zlib.push_back(0x78);zlib.push_back(0x01);
        uint32_t adlerA=1,adlerB=0;
        size_t pos=0;
        while(pos<raw.size())
        {
            size_t len=Lower((size_t)65535,raw.size()-pos);
            zlib.push_back(pos+len==raw.size()?1:0);
            uint16_t l=(uint16_t)len;
            zlib.push_back((BYTE)l);zlib.push_back((BYTE)(l>>8));
            zlib.push_back((BYTE)~l);zlib.push_back((BYTE)(~l>>8));
            for(size_t j=0;j<len;++j)
            {
                BYTE b=raw[pos++];zlib.push_back(b);
                adlerA=(adlerA+b)%65521U;
                adlerB=(adlerB+adlerA)%65521U;
            }
        }
        AppendBE(zlib,(adlerB<<16)|adlerA);
        HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(file==INVALID_HANDLE_VALUE)return false;
        BYTE signature[8]={137,80,78,71,13,10,26,10};
        BYTE ihdr[13]={(BYTE)((uint32_t)g.width>>24),(BYTE)(g.width>>16),
                       (BYTE)(g.width>>8),(BYTE)g.width,
                       (BYTE)((uint32_t)g.height>>24),(BYTE)(g.height>>16),
                       (BYTE)(g.height>>8),(BYTE)g.height,8,6,0,0,0};
        bool ok=WriteExact(file,signature,8) && WriteChunk(file,"IHDR",ihdr,13) &&
                WriteChunk(file,"IDAT",&zlib[0],(DWORD)zlib.size()) &&
                WriteChunk(file,"IEND",NULL,0);
        CloseHandle(file);
        if(!ok){DeleteFileW(path);MessageBoxW(owner,L"Could not export PNG.",L"Paint",MB_OK|MB_ICONERROR);}
        return ok;
    }

    // v6: fully owner-painted dock; no STL and no additional resource files.
    enum { CMD_ADD=1, CMD_REMOVE, CMD_DUPLICATE, CMD_UP, CMD_DOWN, CMD_MERGE,
           CMD_SAVE, CMD_OPEN, CMD_PNG };
    struct Action { int id; const WCHAR *hint; };
    const Action kActions[] = {
        {CMD_ADD,L"New layer"}, {CMD_DUPLICATE,L"Duplicate layer"},
        {CMD_REMOVE,L"Delete layer"}, {CMD_UP,L"Move layer up"},
        {CMD_DOWN,L"Move layer down"}, {CMD_MERGE,L"Merge down"}
    };
    const int kRowH = 62;
    int s_scroll = 0;
    int s_hover = -1;
    bool s_dragOpacity = false;
    bool s_dragChanged = false;
    bool s_dragUndo = false;

    RECT ActionRect(int i) { return Rc(10 + i*35, 48, 31, 31); }
    RECT ListRect(const RECT& bounds) { return Rc(8, 100, kPanelWidth-16,
        Upper<LONG>(0, (bounds.bottom-100)-206)); }
    RECT BaseButton() { return Rc(100,8,60,25); }
    RECT WandButton() { return Rc(168,8,55,25); }
    RECT ToleranceRect(const RECT& bounds) { return Rc(14,bounds.bottom-153,172,9); }
    RECT TrackRect(const RECT& bounds) { return Rc(14, bounds.bottom-104, 172, 10); }
    RECT ProjectRect(const RECT& bounds, int command)
    {
        return Rc(command == CMD_OPEN ? 8 : command == CMD_SAVE ? 83 : 158,
                  bounds.bottom-52, 68, 32);
    }
    void StrokeLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF c, int width=2)
    {
        HPEN pen = CreatePen(PS_SOLID,width,c);
        HGDIOBJ old = SelectObject(dc,pen);
        MoveToEx(dc,x1,y1,NULL); LineTo(dc,x2,y2);
        SelectObject(dc,old); DeleteObject(pen);
    }
    void Outline(HDC dc, RECT r, COLORREF c)
    {
        HBRUSH b = CreateSolidBrush(c);
        FrameRect(dc,&r,b); DeleteObject(b);
    }
    void Glyph(HDC dc, int cmd, RECT r, COLORREF color)
    {
        int x=(r.left+r.right)/2, y=(r.top+r.bottom)/2;
        if (cmd == CMD_ADD)
        {
            StrokeLine(dc,x-6,y,x+6,y,color);
            StrokeLine(dc,x,y-6,x,y+6,color);
        }
        else if (cmd == CMD_DUPLICATE)
        {
            Outline(dc,Rc(x-7,y-7,10,11),color);
            Outline(dc,Rc(x-3,y-3,10,11),color);
        }
        else if (cmd == CMD_REMOVE)
        {
            Outline(dc,Rc(x-5,y-4,11,12),color);
            StrokeLine(dc,x-7,y-7,x+7,y-7,color);
            StrokeLine(dc,x-2,y-1,x-2,y+5,color,1);
            StrokeLine(dc,x+2,y-1,x+2,y+5,color,1);
        }
        else if (cmd == CMD_UP || cmd == CMD_DOWN)
        {
            int t=cmd==CMD_UP?-1:1;
            StrokeLine(dc,x,y-7*t,x,y+7*t,color);
            StrokeLine(dc,x-5,y-2*t,x,y-7*t,color);
            StrokeLine(dc,x+5,y-2*t,x,y-7*t,color);
        }
        else if (cmd == CMD_MERGE)
        {
            StrokeLine(dc,x-7,y-7,x+7,y-7,color);
            StrokeLine(dc,x-5,y-3,x+5,y-3,color);
            StrokeLine(dc,x,y,x,y+5,color);
            StrokeLine(dc,x-4,y+2,x,y+6,color);
            StrokeLine(dc,x+4,y+2,x,y+6,color);
            StrokeLine(dc,x-7,y+9,x+7,y+9,color);
        }
    }
    void Eye(HDC dc,int x,int y,bool visible)
    {
        COLORREF c=visible?RGB(68,94,125):RGB(155,164,176);
        StrokeLine(dc,x-7,y,x,y-4,c,1);
        StrokeLine(dc,x,y-4,x+7,y,c,1);
        StrokeLine(dc,x-7,y,x,y+4,c,1);
        StrokeLine(dc,x,y+4,x+7,y,c,1);
        HBRUSH b=CreateSolidBrush(c);
        HGDIOBJ old=SelectObject(dc,b);
        Ellipse(dc,x-2,y-2,x+3,y+3);
        SelectObject(dc,old);DeleteObject(b);
        if(!visible)StrokeLine(dc,x-8,y+7,x+8,y-7,RGB(164,80,80),2);
    }
    void Thumbnail(HDC dc,RECT rc,int index)
    {
        Fill(dc,rc,RGB(255,255,255));
        // Checkerboard conveys real alpha, not a white-filled layer.
        if(index>0)
        {
            Layer *layer=g.layers[index-1];
            for(int yy=0;yy<rc.bottom-rc.top;yy+=8)
                for(int xx=0;xx<rc.right-rc.left;xx+=8)
                    if(((xx/8)+(yy/8))&1)
                        Fill(dc,Rc(rc.left+xx,rc.top+yy,8,8),RGB(222,226,233));
            if(g.width>0 && g.height>0 && layer->pixels.size()==(size_t)g.width*g.height)
            {
                int tw=rc.right-rc.left,th=rc.bottom-rc.top;
                for(int yy=0;yy<th;++yy)
                    for(int xx=0;xx<tw;++xx)
                    {
                        int sx=xx*g.width/tw,sy=yy*g.height/th;
                        uint32_t px=layer->pixels[(size_t)sy*g.width+sx];
                        int a=((px>>24)&255)*layer->opacity/255;
                        if(!a)continue;
                        COLORREF bg=(((xx/8)+(yy/8))&1)?RGB(222,226,233):RGB(255,255,255);
                        // Stored pixels are premultiplied; apply layer opacity to RGB too.
                        int rr=Lower(255,(int)((px>>16)&255)*layer->opacity/255 + GetRValue(bg)*(255-a)/255);
                        int gg=Lower(255,(int)((px>>8)&255)*layer->opacity/255 + GetGValue(bg)*(255-a)/255);
                        int bb=Lower(255,(int)(px&255)*layer->opacity/255 + GetBValue(bg)*(255-a)/255);
                        SetPixelV(dc,rc.left+xx,rc.top+yy,RGB(rr,gg,bb));
                    }
            }
        }
        Outline(dc,rc,RGB(191,199,209));
    }
    int MaxScroll(const RECT& bounds)
    {
        RECT lr=ListRect(bounds);
        return Upper<LONG>(0, ((int)g.layers.size()+1)*kRowH-(lr.bottom-lr.top));
    }
    void ClampScroll(const RECT& bounds)
    { s_scroll=Upper(0,Lower(s_scroll,MaxScroll(bounds))); }
    void SetOpacityFromMouse(HWND hwnd,int x)
    {
        Layer *layer=ActiveLayer();
        if(!layer)return;
        RECT bounds;GetClientRect(hwnd,&bounds);
        RECT track=TrackRect(bounds);
        int value=Upper<LONG>(0,Lower<LONG>(255,(x-track.left)*255/(track.right-track.left)));
        if(value==layer->opacity)return;
        if(!s_dragUndo){PushUndo();s_dragUndo=true;}
        layer->opacity=(BYTE)value;
        g_imageSaved=FALSE;
        s_dragChanged=true;
        Refresh();
    }
    void DoAction(HWND hwnd,int cmd)
    {
        switch(cmd)
        {
            case CMD_ADD:AddLayer();break;
            case CMD_REMOVE:DeleteLayer();break;
            case CMD_DUPLICATE:DuplicateLayer();break;
            case CMD_UP:Reorder(+1);break;
            case CMD_DOWN:Reorder(-1);break;
            case CMD_MERGE:MergeDown();break;
            case CMD_SAVE:ProjectFile(hwnd,true);break;
            case CMD_OPEN:ProjectFile(hwnd,false);s_scroll=0;break;
            case CMD_PNG:SavePNG(hwnd);break;
        }
        Refresh();
    }
    LRESULT CALLBACK PanelProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
    {
        switch(msg)
        {
            case WM_ERASEBKGND:return 1;
            case WM_RBUTTONDOWN:
            {
                POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                RECT wand=WandButton();
                if(PtInRect(&wand,pt))
                {
                    g.wandGlobal=!g.wandGlobal;
                    Refresh();
                    return 0;
                }
                break;
            }
            case WM_MOUSEMOVE:
            {
                POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                if(s_dragOpacity){SetOpacityFromMouse(hwnd,pt.x);return 0;}
                int hover=-1;
                for(int i=0;i<(int)_countof(kActions);++i)
                {RECT r=ActionRect(i);if(PtInRect(&r,pt))hover=i;}
                if(hover!=s_hover){s_hover=hover;InvalidateRect(hwnd,NULL,FALSE);}
                return 0;
            }
            case WM_LBUTTONUP:
                if(s_dragOpacity)
                {
                    s_dragOpacity=false;s_dragUndo=false;s_dragChanged=false;
                    ReleaseCapture();Refresh();
                }
                return 0;
            case WM_CAPTURECHANGED:
                s_dragOpacity=false;s_dragUndo=false;s_dragChanged=false;return 0;
            case WM_MOUSEWHEEL:
            {
                RECT b;GetClientRect(hwnd,&b);
                s_scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*32;
                ClampScroll(b);InvalidateRect(hwnd,NULL,FALSE);return 0;
            }
            case WM_PAINT:
            {
                PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);
                RECT b;GetClientRect(hwnd,&b);ClampScroll(b);
                Fill(dc,b,RGB(248,249,252));
                Fill(dc,Rc(0,0,kPanelWidth,1),RGB(218,224,232));
                Text(dc,L"Layers",Rc(12,9,85,29),RGB(38,48,62));
                RECT bg=BaseButton(),wand=WandButton();
                Fill(dc,bg,g.baseVisible?RGB(238,242,248):RGB(217,230,247));
                Outline(dc,bg,RGB(185,203,223));
                Text(dc,g.baseVisible?L"Base":L"Alpha",bg,RGB(45,65,85),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                Fill(dc,wand,g.wandMode?RGB(195,220,251):RGB(238,242,248));
                Outline(dc,wand,RGB(185,203,223));
                Text(dc,g.wandGlobal?L"Global":L"Wand",wand,RGB(45,65,85),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                Text(dc,L"EDIT",Rc(12,32,150,14),RGB(114,124,138));
                for(int i=0;i<(int)_countof(kActions);++i)
                {
                    RECT r=ActionRect(i);
                    Fill(dc,r,i==s_hover?RGB(217,230,247):RGB(238,242,248));
                    Outline(dc,r,i==s_hover?RGB(127,166,216):RGB(220,227,236));
                    Glyph(dc,kActions[i].id,r,RGB(58,76,98));
                }
                Text(dc,s_hover>=0?kActions[s_hover].hint:L"Choose a layer to edit",
                     Rc(12,82,210,16),RGB(115,125,139));
                RECT list=ListRect(b);
                Fill(dc,list,RGB(243,246,250));
                int saved=SaveDC(dc);IntersectClipRect(dc,list.left,list.top,list.right,list.bottom);
                for(int row=0;row<=(int)g.layers.size();++row)
                {
                    int idx=(int)g.layers.size()-row;
                    RECT r=Rc(10,list.top+row*kRowH-s_scroll,kPanelWidth-20,kRowH-5);
                    if(r.bottom<list.top || r.top>list.bottom)continue;
                    bool chosen=idx==g.active;
                    Fill(dc,r,chosen?RGB(218,232,251):RGB(255,255,255));
                    Outline(dc,r,chosen?RGB(130,170,222):RGB(226,231,238));
                    Eye(dc,r.left+16,r.top+28,idx==0 || g.layers[idx-1]->visible);
                    Thumbnail(dc,Rc(r.left+30,r.top+7,41,42),idx);
                    LPCWSTR name=idx?g.layers[idx-1]->name.GetString():L"Background";
                    Text(dc,name,Rc(r.left+79,r.top+12,119,22),RGB(41,53,67),
                         DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                    WCHAR info[50];
                    if(idx)wsprintfW(info,L"%u%% opacity",(UINT)g.layers[idx-1]->opacity*100/255);
                    else lstrcpyW(info,L"Base image");
                    Text(dc,info,Rc(r.left+79,r.top+34,114,15),RGB(115,125,139));
                }
                RestoreDC(dc,saved);
                int fy=b.bottom-196;
                StrokeLine(dc,8,fy,kPanelWidth-8,fy,RGB(221,227,235),1);
                Text(dc,L"Wand tolerance",Rc(14,b.bottom-189,140,22),RGB(53,64,78));
                WCHAR wandPct[24];wsprintfW(wandPct,L"%d%%",g.tolerance);
                Text(dc,wandPct,Rc(172,b.bottom-189,46,22),RGB(53,64,78),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
                RECT tol=ToleranceRect(b);
                Fill(dc,tol,RGB(218,226,238));
                Fill(dc,Rc(tol.left,tol.top,(tol.right-tol.left)*g.tolerance/100,9),RGB(87,138,201));
                Outline(dc,Rc(tol.left+(tol.right-tol.left)*g.tolerance/100-4,tol.top-4,8,17),RGB(87,138,201));
                Text(dc,L"Opacity",Rc(14,b.bottom-132,120,23),RGB(53,64,78));
                Layer *active=ActiveLayer();
                WCHAR pct[24];wsprintfW(pct,L"%u%%",active?(UINT)active->opacity*100/255:100);
                Text(dc,pct,Rc(170,b.bottom-132,49,23),RGB(63,80,101),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
                RECT track=TrackRect(b);
                Fill(dc,track,RGB(218,226,238));
                if(active)
                {
                    int xp=track.left+(track.right-track.left)*active->opacity/255;
                    Fill(dc,Rc(track.left,track.top,xp-track.left,track.bottom-track.top),RGB(87,138,201));
                    RECT knob=Rc(xp-6,track.top-4,12,18);
                    Fill(dc,knob,RGB(255,255,255));Outline(dc,knob,RGB(91,132,183));
                }
                Text(dc,L"PROJECT",Rc(12,b.bottom-78,140,17),RGB(114,124,138));
                for(int j=0;j<3;++j)
                {
                    int cmd=j==0?CMD_OPEN:j==1?CMD_SAVE:CMD_PNG;
                    RECT r=ProjectRect(b,cmd);
                    Fill(dc,r,RGB(238,242,248));Outline(dc,r,RGB(220,227,236));
                    Text(dc,j==0?L"Open":j==1?L"Save":L"PNG",r,RGB(52,67,84),
                         DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                }
                EndPaint(hwnd,&ps);return 0;
            }
            case WM_LBUTTONDOWN:
            {
                POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                RECT b;GetClientRect(hwnd,&b);
                RECT bg=BaseButton(),wand=WandButton();
                if(PtInRect(&bg,pt)) { PushUndo();g.baseVisible=!g.baseVisible;g_imageSaved=FALSE;Refresh();return 0; }
                if(PtInRect(&wand,pt)) {ModernLayersSetWandMode(!ModernLayersWandMode());return 0;}
                RECT tol=ToleranceRect(b),hitTol=tol;InflateRect(&hitTol,4,10);
                if(PtInRect(&hitTol,pt))
                {
                    g.tolerance=Upper(0,Lower(100,(int)((pt.x-tol.left)*100/(tol.right-tol.left))));
                    Refresh();return 0;
                }
                for(int i=0;i<(int)_countof(kActions);++i)
                {RECT r=ActionRect(i);if(PtInRect(&r,pt)){DoAction(hwnd,kActions[i].id);return 0;}}
                for(int j=0;j<3;++j)
                {int cmd=j==0?CMD_OPEN:j==1?CMD_SAVE:CMD_PNG;RECT r=ProjectRect(b,cmd);
                 if(PtInRect(&r,pt)){DoAction(hwnd,cmd);return 0;}}
                RECT track=TrackRect(b);
                RECT hit=track;InflateRect(&hit,3,12);
                if(PtInRect(&hit,pt) && ActiveLayer())
                {
                    s_dragOpacity=true;s_dragUndo=false;s_dragChanged=false;
                    SetCapture(hwnd);SetOpacityFromMouse(hwnd,pt.x);return 0;
                }
                RECT list=ListRect(b);
                if(PtInRect(&list,pt))
                {
                    int row=(pt.y-list.top+s_scroll)/kRowH;
                    int idx=(int)g.layers.size()-row;
                    if(row>=0 && idx>=0 && idx<=(int)g.layers.size())
                    {
                        if(pt.x<39 && idx>0)
                        {PushUndo();g.layers[idx-1]->visible=!g.layers[idx-1]->visible;
                         g_imageSaved=FALSE;}
                        else g.active=idx;
                        Refresh();
                    }
                }
                return 0;
            }
        }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }

}

HWND ModernLayersCreate(HWND parent)
{
    g.parent = parent;
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = g_hinstExe;
    wc.lpszClassName = kPanelClass;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return NULL;
    g.panel = CreateWindowExW(0, kPanelClass, NULL, WS_CHILD | WS_CLIPCHILDREN,
                              0, 0, kPanelWidth, 300, parent, NULL, g_hinstExe, NULL);
    SyncSize();
    return g.panel;
}

void ModernLayersLayout(HWND parent, RECT *available)
{
    if (!IsWindow(g.panel) || !available || g.parent != parent) return;
    int width = available->right - available->left;
    bool show = g.shown && width > 590;
    ShowWindow(g.panel, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (!show) return;
    MoveWindow(g.panel, available->right-kPanelWidth, available->top, kPanelWidth,
               available->bottom-available->top, TRUE);
    available->right -= kPanelWidth;
}

void ModernLayersToggle(HWND parent)
{
    if (parent != g.parent) return;
    g.shown = !g.shown;
    SendMessageW(parent, WM_SIZE, 0, 0);
    Refresh();
}

void ModernLayersAttachCanvas(void)
{
    if (g.canvas || !canvasWindow.IsWindow()) return;
    g.canvas = canvasWindow.m_hWnd;
    g.oldCanvasProc = (WNDPROC)SetWindowLongPtrW(g.canvas, GWLP_WNDPROC,
                                                  (LONG_PTR)CanvasProc);
}

void ModernLayersPaintComposite(HDC target)
{
    CompositeIntoDC(target);
}

HBITMAP ModernLayersFlatten(HBITMAP original)
{
    (void)original;
    if (g.layers.empty()) return NULL;
    PaintArray<uint32_t> pixels;
    if (!Composite(pixels)) return NULL;
    return CreateBGRA(&pixels[0], g.width, g.height);
}

void ModernLayersImageResized(void)
{
    SyncSize();
    Refresh();
}

void ModernLayersShutdown(void)
{
    ClearLayers();
    g.panel = NULL; g.parent = NULL;
    g.canvas = NULL; g.oldCanvasProc = NULL;
}

bool ModernLayersHasActiveOverlay(void)
{
    return g.active > 0;
}

bool ModernLayersUndo(void)
{
    if (g.undo.empty()) return g.active > 0; // keep base undo separate when no layer changes
    if (g.redo.size() >= 10) g.redo.erase(g.redo.begin());
    g.redo.push_back(TakeSnapshot());
    Snapshot snapshot = g.undo.back();
    g.undo.pop_back();
    RestoreSnapshot(snapshot);
    return true;
}

bool ModernLayersRedo(void)
{
    if (g.redo.empty()) return g.active > 0;
    if (g.undo.size() >= 10) g.undo.erase(g.undo.begin());
    g.undo.push_back(TakeSnapshot());
    Snapshot snapshot = g.redo.back();
    g.redo.pop_back();
    RestoreSnapshot(snapshot);
    return true;
}

// Shared wand selection state for both Layers and Home ribbon.
bool ModernLayersWandMode(void)
{
    return g.wandMode;
}

void ModernLayersSetWandMode(bool enabled)
{
    if (g.wandMode == enabled) return;
    g.wandMode = enabled;
    g.selection.clear();
    Refresh();
    if (IsWindow(g.parent))
    {
        HWND ribbon = FindWindowExW(g.parent, NULL, L"LiberNTPaintRibbon", NULL);
        if (IsWindow(ribbon)) InvalidateRect(ribbon, NULL, FALSE);
    }
}
