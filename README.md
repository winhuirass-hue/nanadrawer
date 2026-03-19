# Nano Drawer — Nana C++ GUI

A pixel-buffer painting app built with [Nana C++](https://github.com/cnjinhao/nana).

## What's new in v2

| Feature | Detail |
|---|---|
| **CSD titlebar** | Custom-drawn bar with centred window title; drag to move window |
| **Minimise** | Icon-only `–` button hides the window |
| **Maximise** | Icon-only `▢` button toggles full-screen |
| **Close** | Icon-only `×` button with red hover highlight |
| **Tabs** | `+` creates a new canvas tab; `×` closes it; tab label gets a `•` when unsaved |
| **Open file** | Native file dialog — loads any BMP (or format Nana supports) |
| **Save / Save As** | Native file dialog — writes a standard 24-bit BMP without extra libs |
| **Undo** | Per-tab undo stack (up to 20 steps), toolbar `↩` button |

## Tools

Brush · Pencil · Eraser · Fill (BFS bucket) · Gradient · Line · Rectangle · Ellipse

## Build

### Prerequisites (Linux)
```bash
sudo apt install build-essential cmake libx11-dev libxft-dev libfontconfig1-dev libxcursor-dev
```

### Get Nana
```bash
git clone https://github.com/cnjinhao/nana.git ../nana
```

### CMake
```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
./nano_drawer
```

### Single g++ command (Linux)
```bash
g++ -std=c++17 nano_drawer.cpp \
    -I../nana/include -L../nana/build -lnana \
    -lX11 -lXft -lfontconfig -lpthread \
    -o nano_drawer
```

### Windows (MinGW)
```bash
g++ -std=c++17 nano_drawer.cpp \
    -I../nana/include -L../nana/build -lnana \
    -lgdi32 -lcomdlg32 -limm32 -mwindows \
    -o nano_drawer.exe
```

## Architecture notes

- **`CSDTitleBar`** — `panel<false>` that paints its own background, centred title string, and three icon-only buttons. Mouse-down/move on the bar calls `API::window_position()` to drag the parent window.
- **`CanvasPanel`** — `panel<false>` owning a `nana::paint::pixel_buffer` for direct RGBA access. All rasterisation (Bresenham strokes, BFS fill, gradient interpolation, shape preview via snapshot/restore) is CPU-side and blitted with `pixel_buffer::paste`.
- **`NanoDrawer`** — owns `tabbar<string>` + a `vector<TabEntry>`, each entry holding a `unique_ptr<CanvasPanel>`. Tabs are shown/hidden with `API::window_visible`.
- **BMP save** — hand-written 54-byte header + BGR rows, no external imaging library required.
