// GRAPH: scenes and tunnels as nodes, Lines as wires between their ports.
//
// A Line says "when this port fires, that port fires". Scenes have in-ports (show,
// hide, ...) and out-ports (enter, exit, ...). A Tunnel is an abstraction layer: a
// named bus whose ports pass through, so scenes depend on the tunnel, not on each other.
#include "ed.h"
#include "gui.h"
#include "engine/render/font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NODE_W 156
#define HEAD_H 20
#define PORT_H 16

static float s_panx = 20, s_pany = 10;
static char s_drag_node[64];
static float s_drag_dx, s_drag_dy, s_drag_orig[2];
static int s_wire;                   // dragging a new line
static char s_wire_from[140];
static float s_wire_x, s_wire_y;
static int s_pan;
static char s_port_name[64];
static int s_port_dir;               // 0 in, 1 out, 2 both
static char s_line_note[256];
static char s_lit[16][64];           // lines lit by a simulated emit
static int s_nlit;
static double s_lit_t;
static char s_emit_port[64];

typedef struct {
    const char *id, *name;
    Json *ports;
    int is_tunnel, is2d;
    float x, y;
    int h;
} Node;

static int node_of(int i, Node *n)
{
    memset(n, 0, sizeof(*n));
    Json *g = json_get(ed.project, "graph");
    Json *tunnels = json_get(ed.project, "tunnels");
    Json *src;
    if (i < ed.nscenes) {
        src = ed.scenes[i];
        n->is2d = scene_is_2d(src);
    } else {
        src = json_at(tunnels, i - ed.nscenes);
        if (!src) return 0;
        n->is_tunnel = 1;
    }
    n->id = json_gets(src, "id", "?");
    n->name = json_gets(src, "name", n->id);
    n->ports = json_get(src, "ports");
    float p[2] = {40.0f + (i % 4) * 190.0f, 40.0f + (i / 4) * 150.0f};
    json_getv(g, n->id, p, 2);
    n->x = p[0] + s_panx;
    n->y = p[1] + s_pany;
    int nin = 0, nout = 0;
    for (Json *q = n->ports ? n->ports->child : NULL; q; q = q->next) {
        const char *d = json_gets(q, "dir", "both");
        if (n->is_tunnel || !strcmp(d, "both")) { nin++; nout++; }
        else if (!strcmp(d, "in")) nin++;
        else nout++;
    }
    int rows = n->is_tunnel ? json_len(n->ports) : (nin > nout ? nin : nout);
    n->h = HEAD_H + rows * PORT_H + 6;
    return 1;
}

static int node_count(void) { return ed.nscenes + json_len(json_get(ed.project, "tunnels")); }

// Port anchor: side 0 = left (input), 1 = right (output). Returns 1 if the port has that side.
static int port_pos(const Node *n, const char *port, int side, float *x, float *y)
{
    int in_row = 0, out_row = 0, row = 0;
    for (Json *q = n->ports ? n->ports->child : NULL; q; q = q->next, row++) {
        const char *d = json_gets(q, "dir", "both");
        int has_in = n->is_tunnel || strcmp(d, "out") != 0;
        int has_out = n->is_tunnel || strcmp(d, "in") != 0;
        int r_in = n->is_tunnel ? row : in_row, r_out = n->is_tunnel ? row : out_row;
        if (!strcmp(json_gets(q, "name", ""), port)) {
            if (side == 0 && has_in) { *x = n->x; *y = n->y + HEAD_H + r_in * PORT_H + 8; return 1; }
            if (side == 1 && has_out) { *x = n->x + NODE_W; *y = n->y + HEAD_H + r_out * PORT_H + 8; return 1; }
            return 0;
        }
        if (!n->is_tunnel) {
            if (has_in && !has_out) in_row++;
            else if (has_out && !has_in) out_row++;
            else { in_row++; out_row++; }
        }
    }
    return 0;
}

static int find_node(const char *id, Node *n)
{
    for (int i = 0; i < node_count(); i++)
        if (node_of(i, n) && !strcmp(n->id, id)) return 1;
    return 0;
}

static int ref_pos(const char *ref, int side, float *x, float *y)
{
    char node[64];
    const char *dot = strchr(ref, '.');
    if (!dot || dot - ref >= 64) return 0;
    memcpy(node, ref, (size_t)(dot - ref));
    node[dot - ref] = 0;
    Node n;
    if (!find_node(node, &n)) return 0;
    return port_pos(&n, dot + 1, side, x, y);
}

static void wire(float x0, float y0, float x1, float y1, unsigned int c, int thick)
{
    // A cubic curve with horizontal tangents, in short segments.
    float dx = fmaxf(40.0f, fabsf(x1 - x0) * 0.5f);
    float px = x0, py = y0;
    for (int i = 1; i <= 20; i++) {
        float t = i / 20.0f, u = 1 - t;
        float x = u * u * u * x0 + 3 * u * u * t * (x0 + dx) + 3 * u * t * t * (x1 - dx) + t * t * t * x1;
        float y = u * u * u * y0 + 3 * u * u * t * y0 + 3 * u * t * t * y1 + t * t * t * y1;
        gui_line(px, py, x, y, c);
        if (thick) gui_line(px, py + 1, x, y + 1, c);
        px = x;
        py = y;
    }
}

static float wire_dist(float x0, float y0, float x1, float y1, float mx, float my)
{
    float dx = fmaxf(40.0f, fabsf(x1 - x0) * 0.5f), best = 1e9f;
    float px = x0, py = y0;
    for (int i = 1; i <= 20; i++) {
        float t = i / 20.0f, u = 1 - t;
        float x = u * u * u * x0 + 3 * u * u * t * (x0 + dx) + 3 * u * t * t * (x1 - dx) + t * t * t * x1;
        float y = u * u * u * y0 + 3 * u * u * t * y0 + 3 * u * t * t * y1 + t * t * t * y1;
        float sx = x - px, sy = y - py, l2 = sx * sx + sy * sy;
        float k = l2 > 0 ? ((mx - px) * sx + (my - py) * sy) / l2 : 0;
        if (k < 0) k = 0;
        if (k > 1) k = 1;
        float ex = px + sx * k - mx, ey = py + sy * k - my, d = ex * ex + ey * ey;
        if (d < best) best = d;
        px = x;
        py = y;
    }
    return sqrtf(best);
}

static int lit(const char *line_id)
{
    if (ed.time - s_lit_t > 2.5) return 0;
    for (int i = 0; i < s_nlit; i++) if (!strcmp(s_lit[i], line_id)) return 1;
    return 0;
}

static void draw_node(const Node *n, int selected)
{
    unsigned int head = C_PANEL2;
    unsigned int edge = selected ? C_ACCENT : C_EDGE;
    unsigned int kind = n->is_tunnel ? C_AGENT : (n->is2d ? C_COLD : 0xC8A060);
    int x = (int)n->x, y = (int)n->y;
    gui_rect_a(x + 2, y + 2, NODE_W, n->h, 0x000000, 90);
    gui_rect(x, y, NODE_W, n->h, C_PANEL);
    gui_rect(x, y, NODE_W, HEAD_H, head);
    gui_frame(x, y, NODE_W, n->h, edge);
    const char *badge = n->is_tunnel ? "Tunnel" : (n->is2d ? "2D" : "3D");
    gui_rect(x, y, 3, HEAD_H, kind);
    font_draw(x + 7, y + 2, kind, badge);
    gui_text_clip(x + 10 + font_width(badge), y + 2, NODE_W - 16 - font_width(badge), C_BRIGHT, n->name);
    int in_row = 0, out_row = 0, row = 0;
    for (Json *q = n->ports ? n->ports->child : NULL; q; q = q->next, row++) {
        const char *d = json_gets(q, "dir", "both");
        const char *pn = json_gets(q, "name", "");
        int has_in = n->is_tunnel || strcmp(d, "out") != 0;
        int has_out = n->is_tunnel || strcmp(d, "in") != 0;
        if (n->is_tunnel) {
            int ry = y + HEAD_H + row * PORT_H;
            gui_disc((float)x, (float)(ry + 8), 3.5f, C_AGENT);
            gui_disc((float)(x + NODE_W), (float)(ry + 8), 3.5f, C_AGENT);
            int tw = font_width(pn);
            font_draw(x + (NODE_W - tw) / 2, ry, C_TEXT, pn);
            continue;
        }
        if (has_in && !has_out) {
            int ry = y + HEAD_H + in_row * PORT_H;
            gui_disc((float)x, (float)(ry + 8), 3.5f, C_COLD);
            font_draw(x + 8, ry, C_TEXT, pn);
            in_row++;
        } else if (has_out && !has_in) {
            int ry = y + HEAD_H + out_row * PORT_H;
            gui_disc((float)(x + NODE_W), (float)(ry + 8), 3.5f, C_ACCENT);
            font_draw(x + NODE_W - 8 - font_width(pn), ry, C_TEXT, pn);
            out_row++;
        } else {
            int ry = y + HEAD_H + (in_row > out_row ? in_row : out_row) * PORT_H;
            gui_disc((float)x, (float)(ry + 8), 3.5f, C_COLD);
            gui_disc((float)(x + NODE_W), (float)(ry + 8), 3.5f, C_ACCENT);
            font_draw(x + 8, ry, C_TEXT, pn);
            in_row = out_row = (in_row > out_row ? in_row : out_row) + 1;
        }
    }
}

// The port under the mouse: side 0 = input dot, 1 = output dot.
static int port_at(float mx, float my, int side, char *ref, int size)
{
    for (int i = node_count() - 1; i >= 0; i--) {
        Node n;
        if (!node_of(i, &n)) continue;
        for (Json *q = n.ports ? n.ports->child : NULL; q; q = q->next) {
            float px, py;
            const char *pn = json_gets(q, "name", "");
            if (!port_pos(&n, pn, side, &px, &py)) continue;
            if (fabsf(mx - px) < 7 && fabsf(my - py) < 7) {
                snprintf(ref, (size_t)size, "%s.%s", n.id, pn);
                return 1;
            }
        }
    }
    return 0;
}

static void layout_node(const char *id, float x, float y)
{
    Json *a = json_obj();
    json_sets(a, "id", id);
    float p[2] = {roundf(x / 4) * 4, roundf(y / 4) * 4};
    json_setv(a, "pos", p, 2);
    ops_do("graph.layout", a);
}

static void side_panel(int x, int y, int w, int h)
{
    gui_rect(x, y, w, h, C_PANEL);
    gui_rect(x, y, 1, h, C_EDGE);
    int cy = y + gui_header(x, y, w, T("graph.properties")) + 4;
    x += 8;
    w -= 16;
    Json *line = json_find_id(json_get(ed.project, "lines"), ed.sel_line);
    Node n;
    if (line) {
        font_drawf(x, cy, C_DIM, "Line  %s", ed.sel_line);
        cy += 20;
        font_drawf(x, cy, C_ACCENT, T("graph.from"), json_gets(line, "from", ""));
        cy += 18;
        font_drawf(x, cy, C_COLD, T("graph.to"), json_gets(line, "to", ""));
        cy += 22;
        font_draw(x, cy, C_DIM, T("graph.note"));
        cy += 18;
        static char last_id[64];
        if (strcmp(last_id, ed.sel_line)) { snprintf(last_id, sizeof(last_id), "%s", ed.sel_line); snprintf(s_line_note, sizeof(s_line_note), "%s", json_gets(line, "note", "")); }
        if (gui_field(gui_id("line-note", 0), x, cy, w, s_line_note, sizeof(s_line_note))) {
            Json *a = json_obj();
            json_sets(a, "id", ed.sel_line);
            Json *f = json_set(a, "fields", json_obj());
            json_sets(f, "note", s_line_note);
            ops_do("line.set", a);
        }
        cy += 26;
        if (gui_button("graph.delete_line", x, cy, w, 20)) {
            Json *a = json_obj();
            json_sets(a, "id", ed.sel_line);
            ops_do("line.delete", a);
            ed.sel_line[0] = 0;
        }
    } else if (ed.sel_node[0] && find_node(ed.sel_node, &n)) {
        font_drawf(x, cy, C_DIM, "%s  %s", n.is_tunnel ? "Tunnel" : "SCENE", n.id);
        cy += 20;
        gui_text_clip(x, cy, w, C_BRIGHT, n.name);
        cy += 22;
        font_draw(x, cy, C_DIM, T("field.ports"));
        cy += 18;
        for (Json *q = n.ports ? n.ports->child : NULL; q; q = q->next) {
            const char *pn = json_gets(q, "name", ""), *d = json_gets(q, "dir", "both");
            font_drawf(x + 4, cy, !strcmp(d, "in") ? C_COLD : C_ACCENT, "%s", pn);
            font_draw(x + w - 92, cy, C_DIM, d);
            GuiId eid = gui_id(pn, 0xE417);
            if (gui_button_id(eid, "→", x + w - 44, cy, 20, 16, 0)) {
                char ref[140];
                snprintf(ref, sizeof(ref), "%s.%s", n.id, pn);
                Json *a = json_obj();
                json_sets(a, "from", ref);
                Json *r = ops_call("graph.emit", a, "human");
                s_nlit = 0;
                Json *ls = json_get(r, "lines");
                for (Json *l = ls ? ls->child : NULL; l && s_nlit < 16; l = l->next) snprintf(s_lit[s_nlit++], 64, "%s", l->str);
                s_lit_t = ed.time;
                Json *reached = json_get(r, "reached");
                char *t = reached ? json_write(reached, 0) : NULL;
                ed_status(C_COLD, "%s → %s", ref, t ? t : "[]");
                ed_log(LOG_INFO, "emit %s reaches %s", ref, t ? t : "[]");
                free(t);
                json_free(r);
                snprintf(s_emit_port, sizeof(s_emit_port), "%s", ref);
            }
            if (gui_button_id(gui_id(pn, 0xDE1), "×", x + w - 20, cy, 20, 16, 0)) {
                Json *a = json_obj();
                json_sets(a, "node", n.id);
                json_sets(a, "name", pn);
                ops_do("port.delete", a);
                break;
            }
            cy += 18;
        }
        cy += 6;
        font_draw(x, cy, C_DIM, T("graph.new_port"));
        cy += 18;
        gui_field(gui_id("port-name", 0), x, cy, w, s_port_name, sizeof(s_port_name));
        cy += 20;
        static const char *const D[] = {"in", "out", "both"};
        for (int i = 0; i < 3; i++)
            if (gui_button_id(gui_id("pdir", i), D[i], x + i * (w / 3), cy, w / 3 - 2, 18, s_port_dir == i)) s_port_dir = i;
        cy += 22;
        if (gui_button("graph.add_port", x, cy, w, 20) && s_port_name[0]) {
            Json *a = json_obj();
            json_sets(a, "node", n.id);
            json_sets(a, "name", s_port_name);
            json_sets(a, "dir", D[s_port_dir]);
            if (ops_do("port.add", a)) s_port_name[0] = 0;
        }
        cy += 26;
        if (!n.is_tunnel && gui_button("graph.open_scene", x, cy, w, 20)) {
            ed.cur = scene_index(n.id);
            ed.mode = MODE_SCENE;
        }
        if (n.is_tunnel && gui_button("graph.delete_tunnel", x, cy, w, 20)) {
            Json *a = json_obj();
            json_sets(a, "id", n.id);
            ops_do("tunnel.delete", a);
            ed.sel_node[0] = 0;
        }
    }
}

void graph_frame(int x, int y, int w, int h)
{
    int pw = 220;
    int gw = w - pw;
    gui_rect(x, y, gw, h, 0x1B1C1F);
    // Dot grid, like graph paper in the dark.
    for (int gy = (int)fmodf(s_pany, 24) + y; gy < y + h; gy += 24)
        for (int gx = (int)fmodf(s_panx, 24) + x; gx < x + gw; gx += 24) gui_rect(gx, gy, 1, 1, 0x2C2E33);
    gui_clip(x, y, gw, h);
    int over = gui_hit(x, y, gw, h) && !gui.mouse_used;
    // Lines under the nodes.
    Json *lines = json_get(ed.project, "lines");
    char hover_line[64] = "";
    for (Json *l = lines ? lines->child : NULL; l; l = l->next) {
        float x0, y0, x1, y1;
        const char *from = json_gets(l, "from", ""), *to = json_gets(l, "to", "");
        if (!ref_pos(from, 1, &x0, &y0) && !ref_pos(from, 0, &x0, &y0)) continue;
        if (!ref_pos(to, 0, &x1, &y1) && !ref_pos(to, 1, &x1, &y1)) continue;
        const char *id = json_gets(l, "id", "");
        int sel = !strcmp(id, ed.sel_line);
        int hot = over && wire_dist(x0, y0, x1, y1, (float)gui.mx, (float)gui.my) < 5;
        if (hot) snprintf(hover_line, sizeof(hover_line), "%s", id);
        unsigned int c = lit(id) ? 0xF2F3F5 : (sel ? C_ACCENT : (hot ? C_BRIGHT : 0x7A8899));
        wire(x0, y0, x1, y1, c, sel || lit(id));
        const char *note = json_gets(l, "note", "");
        if (note[0] && (sel || hot)) font_draw((int)((x0 + x1) / 2) - font_width(note) / 2, (int)((y0 + y1) / 2) - 16, C_DIM, note);
    }
    int n = node_count();
    char hover_node[64] = "";
    for (int i = 0; i < n; i++) {
        Node nd;
        if (!node_of(i, &nd)) continue;
        draw_node(&nd, !strcmp(nd.id, ed.sel_node));
        if (gui.mx >= nd.x && gui.mx < nd.x + NODE_W && gui.my >= nd.y && gui.my < nd.y + nd.h) snprintf(hover_node, sizeof(hover_node), "%s", nd.id);
    }
    // Wiring.
    char ref[140];
    if (over && gui.clicked && port_at((float)gui.mx, (float)gui.my, 1, ref, sizeof(ref))) {
        s_wire = 1;
        snprintf(s_wire_from, sizeof(s_wire_from), "%s", ref);
        ref_pos(ref, 1, &s_wire_x, &s_wire_y);
        gui_take_mouse();
    } else if (over && gui.clicked && hover_node[0]) {
        Node nd;
        find_node(hover_node, &nd);
        snprintf(ed.sel_node, sizeof(ed.sel_node), "%s", hover_node);
        ed.sel_line[0] = 0;
        if (gui.my < nd.y + HEAD_H) {
            snprintf(s_drag_node, sizeof(s_drag_node), "%s", hover_node);
            s_drag_dx = gui.mx - nd.x;
            s_drag_dy = gui.my - nd.y;
            s_drag_orig[0] = nd.x - s_panx;
            s_drag_orig[1] = nd.y - s_pany;
        }
        if (gui_dbl() && !nd.is_tunnel) { ed.cur = scene_index(nd.id); ed.mode = MODE_SCENE; }
        gui_take_mouse();
    } else if (over && gui.clicked && hover_line[0]) {
        snprintf(ed.sel_line, sizeof(ed.sel_line), "%s", hover_line);
        ed.sel_node[0] = 0;
        gui_take_mouse();
    } else if (over && gui.clicked) {
        ed.sel_node[0] = ed.sel_line[0] = 0;
    }
    if (s_wire) {
        int to_ok = port_at((float)gui.mx, (float)gui.my, 0, ref, sizeof(ref));
        wire(s_wire_x, s_wire_y, (float)gui.mx, (float)gui.my, to_ok ? C_OK : C_ACCENT, 1);
        if (!(gui.in.mouse_buttons & 1)) {
            s_wire = 0;
            if (to_ok) {
                Json *a = json_obj();
                json_sets(a, "from", s_wire_from);
                json_sets(a, "to", ref);
                if (ops_do("line.add", a)) ed_status(C_OK, "Line %s → %s", s_wire_from, ref);
            }
        }
        gui_take_mouse();
    }
    if (s_drag_node[0]) {
        if (gui.in.mouse_buttons & 1) {
            Json *g = json_get(ed.project, "graph");
            float p[2] = {gui.mx - s_drag_dx - s_panx, gui.my - s_drag_dy - s_pany};
            if (g) json_setv(g, s_drag_node, p, 2);  // live; committed as one op on release
            gui_take_mouse();
        } else {
            Json *g = json_get(ed.project, "graph");
            float p[2] = {0, 0};
            json_getv(g, s_drag_node, p, 2);
            if (g) json_setv(g, s_drag_node, s_drag_orig, 2);  // so undo returns here
            if (p[0] != s_drag_orig[0] || p[1] != s_drag_orig[1]) layout_node(s_drag_node, p[0], p[1]);
            s_drag_node[0] = 0;
        }
    }
    // Pan.
    if (over && (gui.in.mouse_pressed & 4)) s_pan = 1;
    if (over && (gui.in.mouse_pressed & 2) && !hover_node[0]) s_pan = 2;
    if (s_pan && !(gui.in.mouse_buttons & 6)) {
        if (s_pan == 2 && abs(gui.mx - gui.pmx) + abs(gui.my - gui.pmy) < 3) {
            static const char *const M[] = {"menu.new_tunnel", "menu.new_scene_3d", "menu.new_scene_2d"};
            gui_menu_open_keys(gui_id("graph-menu", 0), M, 3);
        }
        s_pan = 0;
    }
    if (over && (gui.in.mouse_pressed & 6)) { gui.pmx = gui.mx; gui.pmy = gui.my; }
    if (s_pan) { s_panx += gui.in.mouse_dx; s_pany += gui.in.mouse_dy; }
    static float menu_x, menu_y;
    if (gui_menu_active()) { /* keep the spot where the menu opened */ }
    else { menu_x = gui.mx - s_panx; menu_y = gui.my - s_pany; }
    int m = gui_menu(gui_id("graph-menu", 0));
    if (m == 0) {
        Json *a = json_obj();
        json_sets(a, "name", "Tunnel");
        Json *ports = json_set(a, "ports", json_arr());
        json_push(ports, json_str("signal"));
        json_setn(a, "x", menu_x);
        json_setn(a, "y", menu_y);
        ops_do("tunnel.add", a);
    } else if (m == 1 || m == 2) {
        Json *a = json_obj();
        json_sets(a, "kind", m == 2 ? "2d" : "3d");
        json_sets(a, "name", m == 2 ? "UI" : "Scene");
        if (ops_do("scene.create", a)) layout_node(scene_id(cur_scene()), menu_x, menu_y);
    }
    if (!gui.focus && (input_key_pressed(KEY_DELETE) || input_key_pressed(KEY_BACKSPACE)) && ed.sel_line[0]) {
        Json *a = json_obj();
        json_sets(a, "id", ed.sel_line);
        ops_do("line.delete", a);
        ed.sel_line[0] = 0;
    }
    gui_unclip();
    side_panel(x + gw, y, pw, h);
}
