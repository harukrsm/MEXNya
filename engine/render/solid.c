#include "solid.h"
#include "model.h"
#include "render_api.h"
#include <stddef.h>
#include "stats.h"
#include "engine/render/render_config.h"

typedef struct { float x, y, z; float u, v; unsigned int color; } V;

void solid_box(float x, float y, float z, float w, float h, float d, unsigned int rgb)
{
    static const int faces[6][4] = {{0, 1, 3, 2}, {5, 4, 6, 7}, {4, 0, 2, 6}, {1, 5, 7, 3}, {2, 3, 7, 6}, {4, 5, 1, 0}};
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    RVertex *v = r_temp_alloc(36 * sizeof(RVertex));
    if (!v)
        return;
    int n = 0;
    for (int f = 0; f < 6; f++)
        for (int j = 0; j < 6; j++) {
            int k = faces[f][order[j]];
            unsigned int c = rgb;
            if (f < 4)
                c = ((c & 0xfefefe) >> 1) + ((c & 0xfcfcfc) >> 2);
            v[n++] = (RVertex){x + ((k & 1) ? w : 0), y + ((k & 2) ? h : 0), z + ((k & 4) ? d : 0),
                               0, 0, MC_RGB(c)};
        }
    r_bind_texture(NULL);
    r_matrix_mode(R_MATRIX_MODEL);
    r_load_identity();
    r_set_cull(0);
    r_draw(R_PRIM_TRIANGLES, v, 36);
    r_set_cull(1);
    tex_invalidate();
    g_rstats.vertices += 36;
    g_rstats.triangles += 12;
    g_rstats.draw_calls++;
}