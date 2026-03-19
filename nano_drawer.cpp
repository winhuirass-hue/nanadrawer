// nano_drawer.cpp  —  Nana C++ drawing application
// Features: CSD titlebar (centred title, icon-only min/max/close), tabbed canvases,
//           save (BMP via nana::filebox), open image, per-tab undo stack,
//           brush / pencil / eraser / fill / gradient / line / rect / ellipse tools.
//
// Build (Linux):
//   g++ -std=c++17 nano_drawer.cpp -lnana -lX11 -lpthread -lfontconfig -lXft -o nano_drawer
// Build (Windows/MinGW):
//   g++ -std=c++17 nano_drawer.cpp -lnana -lgdi32 -lcomdlg32 -limm32 -mwindows -o nano_drawer.exe
// CMake: see CMakeLists.txt

#include <nana/gui.hpp>
#include <nana/gui/widgets/form.hpp>
#include <nana/gui/widgets/panel.hpp>
#include <nana/gui/widgets/button.hpp>
#include <nana/gui/widgets/label.hpp>
#include <nana/gui/widgets/slider.hpp>
#include <nana/gui/widgets/checkbox.hpp>
#include <nana/gui/widgets/tabbar.hpp>
#include <nana/gui/drawing.hpp>
#include <nana/gui/place.hpp>
#include <nana/gui/filebox.hpp>
#include <nana/gui/msgbox.hpp>
#include <nana/paint/graphics.hpp>
#include <nana/paint/pixel_buffer.hpp>
#include <nana/paint/image.hpp>

#include <cmath>
#include <stack>
#include <deque>
#include <vector>
#include <functional>
#include <algorithm>
#include <string>
#include <memory>
#include <fstream>

using namespace nana;

// ═══════════════════════════════════════════════════════════════
//  Colour helpers
// ═══════════════════════════════════════════════════════════════
static color lerp_color(color a, color b, double t)
{
    return color_rgb(
        static_cast<unsigned>((1-t)*a.r() + t*b.r()),
        static_cast<unsigned>((1-t)*a.g() + t*b.g()),
        static_cast<unsigned>((1-t)*a.b() + t*b.b()));
}

// ═══════════════════════════════════════════════════════════════
//  Tool enum
// ═══════════════════════════════════════════════════════════════
enum class Tool { Brush, Pencil, Eraser, Fill, Gradient, Line, Rectangle, Ellipse };

// ═══════════════════════════════════════════════════════════════
//  CanvasPanel
// ═══════════════════════════════════════════════════════════════
class CanvasPanel : public panel<false>
{
public:
    Tool   active_tool  = Tool::Brush;
    color  fg_color     = color_rgb(0x534AB7);
    color  grad_color1  = color_rgb(0x534AB7);
    color  grad_color2  = color_rgb(0xF0997B);
    int    brush_size   = 8;
    double opacity      = 1.0;
    bool   shape_fill   = true;
    bool   shape_stroke = true;

    std::function<void()> on_dirty;

    CanvasPanel() = default;

    void create(window parent, rectangle rect)
    {
        panel<false>::create(parent, rect, true);
        unsigned w = rect.width  ? (unsigned)rect.width  : 800u;
        unsigned h = rect.height ? (unsigned)rect.height : 560u;
        _buf = paint::pixel_buffer(w, h);
        _fill_white(_buf);
        _ready = true;
        _wire();
    }

    void clear_canvas()
    {
        _push_undo();
        _fill_white(_buf);
        _mark_dirty();
        _blit();
    }

    void undo()
    {
        if (_undo.empty()) return;
        _buf = std::move(_undo.back());
        _undo.pop_back();
        _blit();
    }

    // ── Save as 24-bit BMP (no external lib) ─────────────────
    bool save_bmp(const std::string& path)
    {
        unsigned W = _buf.width(), H = _buf.height();
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;

        int stride = ((int(W) * 3 + 3) & ~3);
        uint32_t img_sz = (uint32_t)(stride * int(H));
        uint32_t file_sz = 54 + img_sz;

        uint8_t fh[14] = {
            'B','M',
            uint8_t(file_sz),uint8_t(file_sz>>8),uint8_t(file_sz>>16),uint8_t(file_sz>>24),
            0,0,0,0,
            54,0,0,0
        };
        f.write((char*)fh, 14);

        uint32_t dib[10] = {
            40, W, H,
            1u | (24u<<16),
            0,
            img_sz,
            2835,2835,0,0
        };
        f.write((char*)dib, 40);

        std::vector<uint8_t> row(size_t(stride), 0);
        for (int y = int(H)-1; y >= 0; --y) {
            for (unsigned x = 0; x < W; ++x) {
                auto& p = _buf.at(unsigned(y), x);
                row[x*3+0] = uint8_t(p.blue);
                row[x*3+1] = uint8_t(p.green);
                row[x*3+2] = uint8_t(p.red);
            }
            f.write((char*)row.data(), stride);
        }
        return true;
    }

    bool load_image(const std::string& path)
    {
        paint::image img(path);
        if (img.empty()) return false;
        auto sz = size();
        _push_undo();
        paint::graphics g({sz.width, sz.height});
        g.rectangle(true, colors::white);
        img.paste(g, {0,0});
        _buf.open(g);
        _mark_dirty();
        _blit();
        return true;
    }

private:
    paint::pixel_buffer  _buf;
    bool  _ready   = false;
    bool  _drawing = false;
    point _last{0,0}, _start{0,0};
    paint::pixel_buffer  _snapshot;
    static constexpr std::size_t MAX_UNDO = 20;
    std::deque<paint::pixel_buffer> _undo;

    static void _fill_white(paint::pixel_buffer& b)
    {
        for (unsigned y=0; y<b.height(); ++y)
            for (unsigned x=0; x<b.width(); ++x)
                b.at(y,x) = {255,255,255,255};
    }

    void _push_undo()
    {
        if (_undo.size() >= MAX_UNDO) _undo.pop_front();
        _undo.push_back(_buf);
    }

    void _mark_dirty() { if (on_dirty) on_dirty(); }

    void _blit()
    {
        if (!_ready) return;
        drawing dw(*this);
        dw.draw([this](paint::graphics& g){ _buf.paste(g,{0,0}); });
        dw.update();
    }

    void _wire()
    {
        events().expose([this](const arg_expose&){ _blit(); });

        events().resized([this](const arg_resized& a){
            paint::pixel_buffer nb(a.width, a.height);
            _fill_white(nb);
            for (unsigned y=0; y<std::min(_buf.height(),a.height); ++y)
                for (unsigned x=0; x<std::min(_buf.width(),a.width); ++x)
                    nb.at(y,x) = _buf.at(y,x);
            _buf = std::move(nb);
            _blit();
        });

        events().mouse_down([this](const arg_mouse& m){
            _drawing = true;
            _start = _last = m.pos;
            if (active_tool == Tool::Fill) {
                _push_undo();
                _flood_fill(m.pos.x, m.pos.y, fg_color);
                _mark_dirty(); _drawing=false; _blit(); return;
            }
            bool is_shape = (active_tool==Tool::Line||
                             active_tool==Tool::Rectangle||
                             active_tool==Tool::Ellipse||
                             active_tool==Tool::Gradient);
            if (is_shape) { _push_undo(); _snapshot=_buf; }
            else          { _push_undo(); _paint_dot(m.pos.x,m.pos.y); _mark_dirty(); _blit(); }
        });

        events().mouse_move([this](const arg_mouse& m){
            if (!_drawing) return;
            if (active_tool==Tool::Brush||active_tool==Tool::Pencil||active_tool==Tool::Eraser) {
                _paint_segment(_last,m.pos); _last=m.pos; _mark_dirty(); _blit();
            } else if (active_tool==Tool::Gradient) {
                _buf=_snapshot; _paint_gradient(_start,m.pos); _blit();
            } else {
                _buf=_snapshot; _paint_shape(_start,m.pos); _blit();
            }
        });

        events().mouse_up([this](const arg_mouse&){ _drawing=false; });
        events().mouse_leave([this](const arg_mouse&){ _drawing=false; });
    }

    void _put_pixel(int x, int y, color c, double am=1.0)
    {
        int W=int(_buf.width()), H=int(_buf.height());
        if (x<0||y<0||x>=W||y>=H) return;
        auto& p = _buf.at(unsigned(y),unsigned(x));
        double a = opacity*am;
        if (active_tool==Tool::Eraser) {
            p.red  =uint8_t(p.red  +(255-p.red)  *a);
            p.green=uint8_t(p.green+(255-p.green)*a);
            p.blue =uint8_t(p.blue +(255-p.blue) *a);
        } else {
            p.red  =uint8_t((1-a)*p.red  +a*c.r());
            p.green=uint8_t((1-a)*p.green+a*c.g());
            p.blue =uint8_t((1-a)*p.blue +a*c.b());
        }
    }

    void _circle_fill(int cx,int cy,int r,color c)
    {
        for (int dy=-r;dy<=r;++dy)
            for (int dx=-r;dx<=r;++dx)
                if (dx*dx+dy*dy<=r*r) _put_pixel(cx+dx,cy+dy,c);
    }

    void _paint_dot(int x,int y)
    {
        int r = (active_tool==Tool::Pencil) ? 1 : std::max(1,brush_size/2);
        _circle_fill(x,y,r,fg_color);
    }

    void _paint_segment(point a,point b)
    {
        int x0=a.x,y0=a.y,x1=b.x,y1=b.y;
        int dx=std::abs(x1-x0),dy=std::abs(y1-y0);
        int sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;
        int r=(active_tool==Tool::Pencil)?1:std::max(1,brush_size/2);
        while (true) {
            _circle_fill(x0,y0,r,fg_color);
            if (x0==x1&&y0==y1) break;
            int e2=2*err;
            if(e2>-dy){err-=dy;x0+=sx;}
            if(e2< dx){err+=dx;y0+=sy;}
        }
    }

    void _paint_gradient(point a,point b)
    {
        int x0=std::min(a.x,b.x),y0=std::min(a.y,b.y);
        int x1=std::max(a.x,b.x),y1=std::max(a.y,b.y);
        int W=x1-x0,H=y1-y0; if(W<=0||H<=0) return;
        double d=std::sqrt((double)W*W+(double)H*H);
        for(int y=y0;y<=y1;++y)
            for(int x=x0;x<=x1;++x) {
                double t=std::sqrt((double)(x-x0)*(x-x0)+(double)(y-y0)*(y-y0))/d;
                _put_pixel(x,y,lerp_color(grad_color1,grad_color2,t));
            }
    }

    void _paint_shape(point a,point b)
    {
        if      (active_tool==Tool::Line)      _draw_line(a,b);
        else if (active_tool==Tool::Rectangle) _draw_rect(a,b);
        else if (active_tool==Tool::Ellipse)   _draw_ellipse(a,b);
    }

    void _draw_line(point a,point b)
    {
        int r=std::max(1,brush_size/2);
        int x0=a.x,y0=a.y,x1=b.x,y1=b.y;
        int dx=std::abs(x1-x0),dy=std::abs(y1-y0);
        int sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;
        while (true) {
            _circle_fill(x0,y0,r,fg_color);
            if(x0==x1&&y0==y1) break;
            int e2=2*err;
            if(e2>-dy){err-=dy;x0+=sx;}
            if(e2< dx){err+=dx;y0+=sy;}
        }
    }

    void _draw_rect(point a,point b)
    {
        int x0=std::min(a.x,b.x),y0=std::min(a.y,b.y);
        int x1=std::max(a.x,b.x),y1=std::max(a.y,b.y);
        if (shape_fill)
            for(int y=y0;y<=y1;++y)
                for(int x=x0;x<=x1;++x)
                    _put_pixel(x,y,fg_color);
        if (shape_stroke){
            int t=std::max(1,brush_size/2);
            for(int i=-t;i<=t;++i){
                for(int x=x0;x<=x1;++x){ _put_pixel(x,y0+i,fg_color); _put_pixel(x,y1+i,fg_color); }
                for(int y=y0;y<=y1;++y){ _put_pixel(x0+i,y,fg_color); _put_pixel(x1+i,y,fg_color); }
            }
        }
    }

    void _draw_ellipse(point a,point b)
    {
        double cx=(a.x+b.x)/2.0, cy=(a.y+b.y)/2.0;
        double rx=std::abs(b.x-a.x)/2.0, ry=std::abs(b.y-a.y)/2.0;
        if (rx<1||ry<1) return;
        int x0=std::min(a.x,b.x),y0=std::min(a.y,b.y);
        int x1=std::max(a.x,b.x),y1=std::max(a.y,b.y);
        for(int y=y0;y<=y1;++y)
            for(int x=x0;x<=x1;++x){
                double ddx=(x-cx)/rx,ddy=(y-cy)/ry,d2=ddx*ddx+ddy*ddy;
                if (shape_fill && d2<=1.0) _put_pixel(x,y,fg_color);
                if (shape_stroke){
                    double thick=double(brush_size)/2.0/std::min(rx,ry);
                    if (d2<=1.0 && d2>=(1.0-thick)*(1.0-thick)) _put_pixel(x,y,fg_color);
                }
            }
    }

    void _flood_fill(int sx,int sy,color fc)
    {
        int W=int(_buf.width()),H=int(_buf.height());
        if(sx<0||sy<0||sx>=W||sy>=H) return;
        auto& tp=_buf.at(unsigned(sy),unsigned(sx));
        unsigned tr=tp.red,tg=tp.green,tb=tp.blue;
        unsigned fr=fc.r(),fg2=fc.g(),fb2=fc.b();
        if(tr==fr&&tg==fg2&&tb==fb2) return;
        std::stack<std::pair<int,int>> stk;
        stk.push({sx,sy});
        while(!stk.empty()){
            auto[x,y]=stk.top(); stk.pop();
            if(x<0||y<0||x>=W||y>=H) continue;
            auto& p=_buf.at(unsigned(y),unsigned(x));
            if(p.red!=tr||p.green!=tg||p.blue!=tb) continue;
            p.red=uint8_t(fr); p.green=uint8_t(fg2); p.blue=uint8_t(fb2);
            stk.push({x+1,y});stk.push({x-1,y});
            stk.push({x,y+1});stk.push({x,y-1});
        }
    }
};

// ═══════════════════════════════════════════════════════════════
//  CSD Titlebar
//  — custom-drawn top bar with centred title + icon-only buttons
//  — drag-to-move the window
// ═══════════════════════════════════════════════════════════════
class CSDTitleBar : public panel<false>
{
public:
    std::function<void()> on_close, on_minimize, on_maximize;

    void create(window parent, const std::string& title, int w)
    {
        _title = title;
        panel<false>::create(parent, rectangle{0,0,w,36}, true);
        bgcolor(color_rgb(0x2C2C2A));
        _mk_buttons(w);
        _wire_drag();
        _repaint();
    }

    void set_title(const std::string& t) { _title=t; _repaint(); }

    void resize_to(int w)
    {
        API::window_size(*this, {unsigned(w), 36u});
        int x = w;
        x-=36; API::window_position(_btn_close, {x,0});
        x-=36; API::window_position(_btn_max,   {x,0});
        x-=36; API::window_position(_btn_min,   {x,0});
        _repaint();
    }

private:
    std::string _title;
    button _btn_close, _btn_max, _btn_min;
    point  _drag_start;
    bool   _dragging = false;

    // ── Icon-only button factory ──────────────────────────────
    struct BtnStyle {
        color bg_normal{color_rgb(0x2C2C2A)};
        color bg_hover;
        // painter: receives graphics, size, hovered
        std::function<void(paint::graphics&,nana::size,bool)> paint;
    };

    void _mk_icon_btn(button& btn, rectangle r, BtnStyle s)
    {
        btn.create(*this, r, true);
        btn.caption("");
        btn.bgcolor(s.bg_normal);

        struct Shared { bool hov=false; BtnStyle sty; };
        auto sh = std::make_shared<Shared>();
        sh->sty = std::move(s);

        drawing dw(btn);
        dw.draw([sh](paint::graphics& g){
            g.rectangle(true, sh->hov ? sh->sty.bg_hover : sh->sty.bg_normal);
            sh->sty.paint(g, g.size(), sh->hov);
        });

        btn.events().mouse_enter([sh,&btn](const arg_mouse&){
            sh->hov=true; drawing(btn).update();
        });
        btn.events().mouse_leave([sh,&btn](const arg_mouse&){
            sh->hov=false; drawing(btn).update();
        });
    }

    void _mk_buttons(int w)
    {
        int x = w;

        // ── Close  ×  ─────────────────────────────────────────
        x -= 36;
        _mk_icon_btn(_btn_close, {x,0,36,36}, {
            color_rgb(0x2C2C2A), color_rgb(0xC0392B),
            [](paint::graphics& g, nana::size s, bool hov){
                int cx=s.width/2,cy=s.height/2,d=7;
                color lc = hov ? colors::white : color_rgb(0xAAAAAA);
                g.line({cx-d,cy-d},{cx+d,cy+d},lc);
                g.line({cx+d,cy-d},{cx-d,cy+d},lc);
            }
        });
        _btn_close.events().click([this]{ if(on_close) on_close(); });

        // ── Maximise  ▢  ─────────────────────────────────────
        x -= 36;
        _mk_icon_btn(_btn_max, {x,0,36,36}, {
            color_rgb(0x2C2C2A), color_rgb(0x444441),
            [](paint::graphics& g, nana::size s, bool hov){
                int cx=s.width/2,cy=s.height/2,d=7;
                color lc = hov ? colors::white : color_rgb(0xAAAAAA);
                g.rectangle({cx-d,cy-d,(unsigned)(d*2),(unsigned)(d*2)}, false, lc);
            }
        });
        _btn_max.events().click([this]{ if(on_maximize) on_maximize(); });

        // ── Minimise  –  ─────────────────────────────────────
        x -= 36;
        _mk_icon_btn(_btn_min, {x,0,36,36}, {
            color_rgb(0x2C2C2A), color_rgb(0x444441),
            [](paint::graphics& g, nana::size s, bool hov){
                int cy=s.height/2;
                color lc = hov ? colors::white : color_rgb(0xAAAAAA);
                g.line({9,cy},{int(s.width)-9,cy},lc);
            }
        });
        _btn_min.events().click([this]{ if(on_minimize) on_minimize(); });
    }

    void _wire_drag()
    {
        events().expose([this](const arg_expose&){ _repaint(); });

        events().mouse_down([this](const arg_mouse& m){
            _dragging = true;
            // Record where on screen the window is, adjusted for click offset
            auto wp = API::window_position(parent_window(*this));
            _drag_start = { wp.x - m.pos.x, wp.y - m.pos.y };
        });
        events().mouse_move([this](const arg_mouse& m){
            if (!_dragging) return;
            API::window_position(parent_window(*this),
                { _drag_start.x + m.pos.x, _drag_start.y + m.pos.y });
        });
        events().mouse_up  ([this](const arg_mouse&){ _dragging=false; });
        events().mouse_leave([this](const arg_mouse&){ _dragging=false; });
    }

    void _repaint()
    {
        drawing dw(*this);
        dw.draw([this](paint::graphics& g){
            g.rectangle(true, color_rgb(0x2C2C2A));
            g.typeface(paint::font("", 12, {500,false,false,false}));
            auto ext = g.text_extent_size(_title);
            int tx = (int(g.size().width) - int(ext.width)) / 2;
            int ty = (36 - int(ext.height)) / 2;
            if (tx < 4) tx = 4;
            g.string({tx,ty}, _title, color_rgb(0xDDDDDD));
        });
        dw.update();
    }
};

// ═══════════════════════════════════════════════════════════════
//  NanoDrawer — main application
// ═══════════════════════════════════════════════════════════════
class NanoDrawer
{
    // Palette (20 colours)
    static constexpr color PALETTE[] = {
        color_rgb(0x2C2C2A), color_rgb(0x888780), color_rgb(0xD3D1C7), color_rgb(0xFFFFFF),
        color_rgb(0xE24B4A), color_rgb(0xD85A30), color_rgb(0xEF9F27), color_rgb(0x639922),
        color_rgb(0x1D9E75), color_rgb(0x378ADD), color_rgb(0x534AB7), color_rgb(0xD4537E),
        color_rgb(0xF09595), color_rgb(0xFAC775), color_rgb(0x9FE1CB), color_rgb(0xB5D4F4),
        color_rgb(0xCECBF6), color_rgb(0xF4C0D1), color_rgb(0xC0DD97), color_rgb(0xF5C4B3),
    };

public:
    NanoDrawer()
        : _form(API::make_center(1140,680),
                appear::decorate<appear::taskbar,appear::minimize,
                                 appear::maximize,appear::resizeable>())
    {
        _form.caption("");
        _form.bgcolor(color_rgb(0x2C2C2A));
        _build_ui();
        _form.show();
        exec();
    }

private:
    form          _form;
    CSDTitleBar   _titlebar;

    // ── Tool panel ───────────────────────────────────────────
    panel<false>  _toolbar;
    struct ToolBtn { button btn; Tool tool; };
    std::vector<std::unique_ptr<ToolBtn>> _tool_btns;
    button        _btn_clr, _btn_undo;

    // ── Sidebar ───────────────────────────────────────────────
    panel<false>  _sidebar;
    slider        _size_sl, _op_sl;
    label         _size_lbl, _op_lbl;
    checkbox      _chk_fill, _chk_stroke;
    button        _grad1_btn, _grad2_btn;
    button        _btn_new, _btn_open, _btn_save, _btn_saveas;
    color         _grad1{color_rgb(0x534AB7)};
    color         _grad2{color_rgb(0xF0997B)};
    int           _grad1_idx{10}, _grad2_idx{11};
    color         _cur_color{color_rgb(0x534AB7)};
    Tool          _cur_tool{Tool::Brush};

    // ── Tabs ──────────────────────────────────────────────────
    panel<false>        _tab_area;
    tabbar<std::string> _tabbar;
    button              _btn_tab_add, _btn_tab_close;

    struct TabEntry {
        std::unique_ptr<CanvasPanel> canvas;
        std::string filename;
    };
    std::vector<TabEntry> _tabs;
    std::size_t           _active{0};
    int                   _uid{1};

    // ─────────────────────────────────────────────────────────
    void _build_ui()
    {
        constexpr int FW=1140, FH=680;
        constexpr int TH=36;   // titlebar height
        constexpr int TBW=52;  // toolbar width
        constexpr int SBW=170; // sidebar width
        constexpr int TABH=28; // tabbar height
        int cw = FW - TBW - SBW;

        // ── CSD titlebar ─────────────────────────────────────
        _titlebar.create(_form, "Nano Drawer", FW);
        _titlebar.on_close    = [this]{ _form.close(); };
        _titlebar.on_minimize = [this]{ API::show_window(_form, false); };
        _titlebar.on_maximize = [this]{
            static bool mx=false; mx=!mx;
            if (mx) API::window_size(_form, API::screen_size());
            else    API::window_size(_form, {(unsigned)FW,(unsigned)FH});
        };
        _form.events().resized([this,TBW,SBW,TABH,TH](const arg_resized& a){
            _titlebar.resize_to(int(a.width));
            int ncw = int(a.width) - TBW - SBW;
            int nch = int(a.height) - TH;
            API::window_size(_tab_area, {unsigned(ncw), unsigned(nch)});
            _btn_tab_add  .move(rectangle{ncw-60, 0, 28u, unsigned(TABH)});
            _btn_tab_close.move(rectangle{ncw-30, 0, 28u, unsigned(TABH)});
            _tabbar.move(rectangle{0, 0, unsigned(ncw-64), unsigned(TABH)});
            if (_active < _tabs.size())
                _tabs[_active].canvas->move(
                    rectangle{0, TABH, unsigned(ncw), unsigned(nch-TABH)});
        });

        // ── Toolbar ──────────────────────────────────────────
        _toolbar.create(_form, rectangle{0,TH,unsigned(TBW),unsigned(FH-TH)});
        _toolbar.bgcolor(color_rgb(0xECEBE7));
        _build_toolbar(FH-TH);

        // ── Sidebar ──────────────────────────────────────────
        _sidebar.create(_form, rectangle{TBW,TH,unsigned(SBW),unsigned(FH-TH)});
        _sidebar.bgcolor(color_rgb(0xF0EFEb));
        _build_sidebar(SBW, FH-TH);

        // ── Tab area ─────────────────────────────────────────
        _tab_area.create(_form, rectangle{TBW+SBW,TH,unsigned(cw),unsigned(FH-TH)});
        _tab_area.bgcolor(color_rgb(0xE8E7E3));

        _tabbar.create(_tab_area, rectangle{0, 0, unsigned(cw-64), unsigned(TABH)}, true);
        _tabbar.bgcolor(color_rgb(0xDDDCDB));
        _tabbar.events().activated([this](const arg_tabbar<std::string>& a){
            _switch_tab(static_cast<std::size_t>(a.widget.activated()));
        });

        _btn_tab_add.create(_tab_area, rectangle{cw-60, 0, 28u, unsigned(TABH)}, true);
        _btn_tab_add.caption("+");
        _btn_tab_add.bgcolor(color_rgb(0xDDDCDB));
        _btn_tab_add.events().click([this]{ _add_tab(); });

        _btn_tab_close.create(_tab_area, rectangle{cw-30, 0, 28u, unsigned(TABH)}, true);
        _btn_tab_close.caption("×");
        _btn_tab_close.bgcolor(color_rgb(0xDDDCDB));
        _btn_tab_close.events().click([this]{ _close_tab(_active); });

        _add_tab();  // first empty canvas
        _select_tool(Tool::Brush);
    }

    // ── Toolbar ───────────────────────────────────────────────
    void _build_toolbar(int h)
    {
        struct TDef{ const char* cap; Tool t; };
        const TDef TD[] = {
            {"Brs",Tool::Brush},  {"Pen",Tool::Pencil},
            {"Ers",Tool::Eraser}, {"Fil",Tool::Fill},
            {"Grd",Tool::Gradient},{"Lin",Tool::Line},
            {"Rct",Tool::Rectangle},{"Ell",Tool::Ellipse},
        };
        int ty=10;
        for (auto& td : TD) {
            auto tb=std::make_unique<ToolBtn>();
            tb->tool=td.t;
            tb->btn.create(_toolbar,rectangle{8,ty,36,28},true);
            tb->btn.caption(td.cap);
            tb->btn.bgcolor(color_rgb(0xECEBE7));
            Tool t=td.t;
            tb->btn.events().click([this,t]{ _select_tool(t); });
            _tool_btns.push_back(std::move(tb));
            ty+=34;
        }
        _btn_clr.create(_toolbar,rectangle{8,ty+6,36,26},true);
        _btn_clr.caption("Clr");
        _btn_clr.bgcolor(color_rgb(0xECEBE7));
        _btn_clr.events().click([this]{ if(auto*c=_cv()) c->clear_canvas(); });
        ty+=36;
        _btn_undo.create(_toolbar,rectangle{8,ty+6,36,26},true);
        _btn_undo.caption("↩");
        _btn_undo.bgcolor(color_rgb(0xECEBE7));
        _btn_undo.events().click([this]{ if(auto*c=_cv()) c->undo(); });
    }

    // ── Sidebar ───────────────────────────────────────────────
    void _build_sidebar(int w, int /*h*/)
    {
        int sy=8;
        auto sec=[&](const char* t){
            label* l=new label;
            l->create(_sidebar,rectangle{8,sy,unsigned(w-16),14u},true);
            l->caption(t);
            l->bgcolor(color_rgb(0xF0EFEb));
            sy+=17;
        };

        // File operations
        sec("FILE");
        auto fb=[&](button& b, const char* cap, int x, int bw){
            b.create(_sidebar,rectangle{x,sy,unsigned(bw),24u},true);
            b.caption(cap);
            b.bgcolor(color_rgb(0xE4E3DF));
        };
        fb(_btn_new,   "New",   8, 36);
        fb(_btn_open,  "Open",  48, 44);
        fb(_btn_save,  "Save",  96, 44);
        sy+=28;
        fb(_btn_saveas,"Save As",8,w-16);
        sy+=32;

        _btn_new   .events().click([this]{ _add_tab(); });
        _btn_open  .events().click([this]{ _do_open(); });
        _btn_save  .events().click([this]{ _do_save(false); });
        _btn_saveas.events().click([this]{ _do_save(true); });

        // Palette
        sec("COLOR");
        int px=8, rc=0;
        for (auto& c : PALETTE) {
            button* sw=new button;
            sw->create(_sidebar,rectangle{px,sy,22,22},true);
            sw->bgcolor(c); sw->caption("");
            color cc=c;
            sw->events().click([this,cc]{ _set_color(cc); });
            px+=26;
            if(++rc%5==0){px=8;sy+=26;}
        }
        sy+=10;

        // Size
        sec("SIZE");
        _size_sl.create(_sidebar,rectangle{8,sy,108,20},true);
        _size_sl.maximum(60); _size_sl.value(8);
        _size_sl.events().value_changed([this]{
            int v=int(_size_sl.value());
            for(auto&te:_tabs) te.canvas->brush_size=v;
            _size_lbl.caption(std::to_string(v));
        });
        _size_lbl.create(_sidebar,rectangle{120,sy,36,20},true);
        _size_lbl.caption("8");
        _size_lbl.bgcolor(color_rgb(0xF0EFEb));
        sy+=28;

        // Opacity
        sec("OPACITY");
        _op_sl.create(_sidebar,rectangle{8,sy,108,20},true);
        _op_sl.maximum(100); _op_sl.value(100);
        _op_sl.events().value_changed([this]{
            int v=int(_op_sl.value());
            for(auto&te:_tabs) te.canvas->opacity=v/100.0;
            _op_lbl.caption(std::to_string(v)+"%");
        });
        _op_lbl.create(_sidebar,rectangle{120,sy,44,20},true);
        _op_lbl.caption("100%");
        _op_lbl.bgcolor(color_rgb(0xF0EFEb));
        sy+=28;

        // Gradient stops
        sec("GRADIENT STOPS");
        _grad1_btn.create(_sidebar,rectangle{8,sy,68,24},true);
        _grad1_btn.bgcolor(_grad1); _grad1_btn.caption("Stop 1");
        _grad1_btn.events().click([this]{
            _grad1_idx=(_grad1_idx+1)%20;
            _grad1=PALETTE[_grad1_idx];
            _grad1_btn.bgcolor(_grad1);
            for(auto&te:_tabs) te.canvas->grad_color1=_grad1;
        });
        _grad2_btn.create(_sidebar,rectangle{84,sy,68,24},true);
        _grad2_btn.bgcolor(_grad2); _grad2_btn.caption("Stop 2");
        _grad2_btn.events().click([this]{
            _grad2_idx=(_grad2_idx+1)%20;
            _grad2=PALETTE[_grad2_idx];
            _grad2_btn.bgcolor(_grad2);
            for(auto&te:_tabs) te.canvas->grad_color2=_grad2;
        });
        sy+=32;

        // Shape options
        sec("SHAPE OPTIONS");
        _chk_fill.create(_sidebar,rectangle{8,sy,66,22},true);
        _chk_fill.caption("Fill"); _chk_fill.bgcolor(color_rgb(0xF0EFEb)); _chk_fill.check(true);
        _chk_fill.events().checked([this]{
            for(auto&te:_tabs) te.canvas->shape_fill=_chk_fill.checked();
        });
        _chk_stroke.create(_sidebar,rectangle{78,sy,76,22},true);
        _chk_stroke.caption("Stroke"); _chk_stroke.bgcolor(color_rgb(0xF0EFEb)); _chk_stroke.check(true);
        _chk_stroke.events().checked([this]{
            for(auto&te:_tabs) te.canvas->shape_stroke=_chk_stroke.checked();
        });
    }

    // ─────────────────────────────────────────────────────────
    //  Tab management
    // ─────────────────────────────────────────────────────────
    void _add_tab(const std::string& file="")
    {
        std::string label = file.empty()
            ? ("Untitled "+std::to_string(_uid++))
            : _basename(file);

        _tabbar.append(label);
        std::size_t idx = _tabs.size();

        TabEntry te;
        te.filename = file;
        te.canvas   = std::make_unique<CanvasPanel>();

        auto tab_sz = _tab_area.size();
        int cw = int(tab_sz.width);
        int ch = int(tab_sz.height) - 28;

        te.canvas->create(_tab_area, rectangle{0,28,unsigned(cw),unsigned(ch)});
        te.canvas->bgcolor(colors::white);
        te.canvas->fg_color    = _cur_color;
        te.canvas->grad_color1 = _grad1;
        te.canvas->grad_color2 = _grad2;
        te.canvas->active_tool = _cur_tool;
        te.canvas->brush_size  = int(_size_sl.value());
        te.canvas->opacity     = int(_op_sl.value())/100.0;
        te.canvas->shape_fill  = _chk_fill.checked();
        te.canvas->shape_stroke= _chk_stroke.checked();

        // Dirty-indicator: prefix tab label with bullet
        te.canvas->on_dirty = [this, idx]{
            std::string cur = _tabbar.text(idx);
            if (cur.size()<3 || cur[0]!=(char)0xE2)
                _tabbar.text(idx, "\xE2\x80\xA2 "+cur);
        };

        if (!file.empty()) te.canvas->load_image(file);

        _tabs.push_back(std::move(te));
        _tabbar.activated(idx);
        _switch_tab(idx);
    }

    void _switch_tab(std::size_t idx)
    {
        if (idx>=_tabs.size()) return;
        for (std::size_t i=0;i<_tabs.size();++i)
            API::show_window(*_tabs[i].canvas, i==idx);
        _active = idx;
        _update_title();
    }

    void _close_tab(std::size_t idx)
    {
        if (_tabs.size()<=1) return;
        _tabbar.erase(idx);
        _tabs[idx].canvas->close();
        _tabs.erase(_tabs.begin()+std::ptrdiff_t(idx));
        std::size_t next = idx<_tabs.size() ? idx : _tabs.size()-1;
        _active = next;
        _tabbar.activated(next);
        _switch_tab(next);
    }

    CanvasPanel* _cv()
    {
        return _tabs.empty() ? nullptr : _tabs[_active].canvas.get();
    }

    // ─────────────────────────────────────────────────────────
    //  File I/O
    // ─────────────────────────────────────────────────────────
    void _do_open()
    {
        filebox fb(_form, true);
        fb.add_filter("BMP Images","*.bmp");
        fb.add_filter("All Files","*.*");
        auto files = fb.show();
        if (files.empty()) return;
        _add_tab(files.front());
    }

    void _do_save(bool dialog)
    {
        auto* cv = _cv();
        if (!cv) return;
        std::string path = _tabs[_active].filename;
        if (dialog || path.empty()) {
            filebox fb(_form, false);
            fb.add_filter("BMP image","*.bmp");
            fb.init_file("untitled.bmp");
            auto files = fb.show();
            if (files.empty()) return;
            path = files.front();
            if (path.size()<4 || path.substr(path.size()-4)!=".bmp")
                path += ".bmp";
            _tabs[_active].filename = path;
        }
        if (cv->save_bmp(path)) {
            _tabbar.text(_active, _basename(path));
            _update_title();
        } else {
            msgbox mb(_form,"Save failed");
            mb.icon(msgbox::icon_error);
            mb << "Could not write:\n" << path;
            mb.show();
        }
    }

    // ─────────────────────────────────────────────────────────
    //  Utilities
    // ─────────────────────────────────────────────────────────
    void _select_tool(Tool t)
    {
        _cur_tool = t;
        for (auto& tb : _tool_btns)
            tb->btn.bgcolor(tb->tool==t
                ? color_rgb(0xDDDBD5)
                : color_rgb(0xECEBE7));
        for (auto& te : _tabs) te.canvas->active_tool = t;
    }

    void _set_color(color c)
    {
        _cur_color = c;
        for (auto& te : _tabs) te.canvas->fg_color = c;
    }

    void _update_title()
    {
        std::string t = "Nano Drawer";
        if (!_tabs.empty()) {
            auto& te=_tabs[_active];
            std::string fn = te.filename.empty()
                ? ("Untitled "+std::to_string(_active+1))
                : _basename(te.filename);
            t += " — " + fn;
        }
        _titlebar.set_title(t);
    }

    static std::string _basename(const std::string& p)
    {
        auto pos=p.find_last_of("/\\");
        return (pos==std::string::npos) ? p : p.substr(pos+1);
    }
};

// ═══════════════════════════════════════════════════════════════
int main()
{
    NanoDrawer app;
    return 0;
}
