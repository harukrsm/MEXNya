// Immediate-mode widgets for the editor, drawn with the engine's 2D pass and pixel font.
// Look: dark panels, square double borders, one warm accent, no rounded corners.
#ifndef ED_GUI_H
#define ED_GUI_H

#include "engine/platform/input.h"

// A neutral dark theme, the kind desktop editors use; one blue for focus and selection.
#define C_BG       0x1E1F22
#define C_PANEL    0x26272B
#define C_PANEL2   0x2E3035
#define C_EDGE     0x3C3F45
#define C_EDGE_IN  0x323439
#define C_TEXT     0xC8CBD0
#define C_BRIGHT   0xF2F3F5
#define C_DIM      0x8B8F96
#define C_ACCENT   0x4C8DDB
#define C_COLD     0x6FB3C8
#define C_SEL      0x2F4A6E
#define C_WARN     0xE06C60
#define C_OK       0x7DB86A
#define C_AGENT    0xB18FE0
#define C_NOTE     0xE8B04A   // human notes on the scene

typedef unsigned int GuiId;

typedef struct {
    Input in;
    GuiId hot, active, focus;      // hovered, pressed/dragging, keyboard focus
    GuiId hot_next;
    int mx, my, pmx, pmy;          // mouse now / when the active widget was pressed
    int clicked, released, rclicked, dclicked;
    int mouse_used;                // a widget took the mouse this frame
    int wheel_used;
    double last_click_t;
    int last_click_x, last_click_y;
    double time;
    // Text field editing state
    char edit_buf[1024];
    int edit_caret;
    int edit_commit;               // the focused field committed this frame
    GuiId edit_owner;
    // Popup menu
    int menu_open, menu_x, menu_y, menu_w, menu_n, menu_pick;
    GuiId menu_owner;
    char menu_items[24][64];
    char tooltip[256];
    int tip_x, tip_y;
} Gui;

extern Gui gui;

GuiId gui_id(const char *s, int n);
void gui_begin(const Input *in, double time);
void gui_end(void);

int gui_hit(int x, int y, int w, int h);
int gui_hover(GuiId id, int x, int y, int w, int h);  // registers hot
void gui_take_mouse(void);
int gui_dbl(void);  // this frame's left click is a double click

// Drawing
void gui_rect(int x, int y, int w, int h, unsigned int rgb);
void gui_rect_a(int x, int y, int w, int h, unsigned int rgb, int alpha);
void gui_frame(int x, int y, int w, int h, unsigned int rgb);
void gui_line(float x0, float y0, float x1, float y1, unsigned int rgb);
void gui_circle(float cx, float cy, float r, unsigned int rgb);
void gui_disc(float cx, float cy, float r, unsigned int rgb);
void gui_panel(int x, int y, int w, int h);
int gui_header(int x, int y, int w, const char *title);  // returns height used
int gui_text(int x, int y, unsigned int rgb, const char *s);
int gui_text_clip(int x, int y, int w, unsigned int rgb, const char *s);
int gui_textf(int x, int y, unsigned int rgb, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
// Wraps at w pixels; returns lines drawn (draw = 0 only measures).
int gui_text_wrap(int x, int y, int w, unsigned int rgb, const char *s, int draw);
void gui_clip(int x, int y, int w, int h);
void gui_unclip(void);
void gui_tip(const char *s);

// Widgets. Return 1 when clicked/changed.
// Widgets named by a translation key: the key is the widget's identity, tr(key) its
// text, so switching language never changes which widget is which.
int gui_button(const char *key, int x, int y, int w, int h);
int gui_button_id(GuiId id, const char *label, int x, int y, int w, int h, int on);
int gui_tab(const char *key, int x, int y, int w, int on);
int gui_check(const char *key, int x, int y, int *v);
int gui_row(GuiId id, int x, int y, int w, int h, int selected);
// Single-line text field. Returns 1 when committed (Enter or focus lost after a change).
int gui_field(GuiId id, int x, int y, int w, char *buf, int size);
// Number: drag horizontally to change, click to type.
int gui_number(GuiId id, int x, int y, int w, float *v, float step);
// Drag handle helpers: returns 1 while this id is being dragged.
int gui_drag(GuiId id, int x, int y, int w, int h, int button);
void gui_focus(GuiId id, const char *initial);
int gui_focused(GuiId id);

// Popup menu at the mouse. Open with gui_menu_open, then each frame call gui_menu()
// which returns the picked index once (or -1).
void gui_menu_open(GuiId owner, const char *const *items, int n);
void gui_menu_open_keys(GuiId owner, const char *const *keys, int n);   // "-" = separator
int gui_menu(GuiId owner);
int gui_menu_active(void);

#endif
