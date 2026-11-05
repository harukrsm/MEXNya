// OpenGL 3.3 core backend. Renders at 480x272 into an RGB565 framebuffer with
// reversed 16-bit-style depth and dithering, then presents it with integer
// nearest-neighbour scaling. State rules match the software rasteriser.
#ifndef MC_NO_OPENGL
#include "../render_api.h"
#include "../gfx.h"
#include "engine/render/render_config.h"
#include "../../platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
// Windows' opengl32 exports only OpenGL 1.1; everything newer is loaded at run time.
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#define GL_FUNCS \
    X(glActiveTexture, PFNGLACTIVETEXTUREPROC) \
    X(glAttachShader, PFNGLATTACHSHADERPROC) \
    X(glBindBuffer, PFNGLBINDBUFFERPROC) \
    X(glBindFramebuffer, PFNGLBINDFRAMEBUFFERPROC) \
    X(glBindRenderbuffer, PFNGLBINDRENDERBUFFERPROC) \
    X(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC) \
    X(glBufferData, PFNGLBUFFERDATAPROC) \
    X(glCheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC) \
    X(glCompileShader, PFNGLCOMPILESHADERPROC) \
    X(glCreateProgram, PFNGLCREATEPROGRAMPROC) \
    X(glCreateShader, PFNGLCREATESHADERPROC) \
    X(glDeleteBuffers, PFNGLDELETEBUFFERSPROC) \
    X(glDeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC) \
    X(glDeleteProgram, PFNGLDELETEPROGRAMPROC) \
    X(glDeleteRenderbuffers, PFNGLDELETERENDERBUFFERSPROC) \
    X(glDeleteShader, PFNGLDELETESHADERPROC) \
    X(glDeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC) \
    X(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC) \
    X(glFramebufferRenderbuffer, PFNGLFRAMEBUFFERRENDERBUFFERPROC) \
    X(glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC) \
    X(glGenBuffers, PFNGLGENBUFFERSPROC) \
    X(glGenFramebuffers, PFNGLGENFRAMEBUFFERSPROC) \
    X(glGenRenderbuffers, PFNGLGENRENDERBUFFERSPROC) \
    X(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC) \
    X(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC) \
    X(glGetProgramiv, PFNGLGETPROGRAMIVPROC) \
    X(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC) \
    X(glGetShaderiv, PFNGLGETSHADERIVPROC) \
    X(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC) \
    X(glLinkProgram, PFNGLLINKPROGRAMPROC) \
    X(glRenderbufferStorage, PFNGLRENDERBUFFERSTORAGEPROC) \
    X(glShaderSource, PFNGLSHADERSOURCEPROC) \
    X(glUniform1f, PFNGLUNIFORM1FPROC) \
    X(glUniform1i, PFNGLUNIFORM1IPROC) \
    X(glUniform2f, PFNGLUNIFORM2FPROC) \
    X(glUniform3f, PFNGLUNIFORM3FPROC) \
    X(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC) \
    X(glUseProgram, PFNGLUSEPROGRAMPROC) \
    X(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)

#define X(name, type) static type p_##name;
GL_FUNCS
#undef X
static int load_gl(void)
{
    int ok = 1;
#define X(name, type) p_##name = (type)plat_gl_proc(#name); if (!p_##name) ok = 0;
    GL_FUNCS
#undef X
    return ok ? 0 : -1;
}
#define glActiveTexture p_glActiveTexture
#define glAttachShader p_glAttachShader
#define glBindBuffer p_glBindBuffer
#define glBindFramebuffer p_glBindFramebuffer
#define glBindRenderbuffer p_glBindRenderbuffer
#define glBindVertexArray p_glBindVertexArray
#define glBufferData p_glBufferData
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glCompileShader p_glCompileShader
#define glCreateProgram p_glCreateProgram
#define glCreateShader p_glCreateShader
#define glDeleteBuffers p_glDeleteBuffers
#define glDeleteFramebuffers p_glDeleteFramebuffers
#define glDeleteProgram p_glDeleteProgram
#define glDeleteRenderbuffers p_glDeleteRenderbuffers
#define glDeleteShader p_glDeleteShader
#define glDeleteVertexArrays p_glDeleteVertexArrays
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glFramebufferRenderbuffer p_glFramebufferRenderbuffer
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glGenBuffers p_glGenBuffers
#define glGenFramebuffers p_glGenFramebuffers
#define glGenRenderbuffers p_glGenRenderbuffers
#define glGenVertexArrays p_glGenVertexArrays
#define glGetProgramInfoLog p_glGetProgramInfoLog
#define glGetProgramiv p_glGetProgramiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glGetShaderiv p_glGetShaderiv
#define glGetUniformLocation p_glGetUniformLocation
#define glLinkProgram p_glLinkProgram
#define glRenderbufferStorage p_glRenderbufferStorage
#define glShaderSource p_glShaderSource
#define glUniform1f p_glUniform1f
#define glUniform1i p_glUniform1i
#define glUniform2f p_glUniform2f
#define glUniform3f p_glUniform3f
#define glUniformMatrix4fv p_glUniformMatrix4fv
#define glUseProgram p_glUseProgram
#define glVertexAttribPointer p_glVertexAttribPointer
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
static int load_gl(void) { return 0; }
#endif
#if defined(__APPLE__)
static int load_gl(void) { return 0; }
#endif

// Frame size and look are set at run time: the retro look renders into RGB565 with
// dither; the native look renders RGBA8 at the window's own resolution.
static int s_iw = MC_LOWRES_W, s_ih = MC_LOWRES_H;
static int s_native;
#define IW s_iw
#define IH s_ih

// ---------------------------------------------------------------- matrices
static RMat4 s_proj, s_view, s_model;
static RMat4 s_proj_stack[8], s_view_stack[8], s_model_stack[8];
static int s_proj_top, s_view_top, s_model_top;
static RMatrixMode s_mmode = R_MATRIX_MODEL;
static int s_two_d;
static int s_inited;

static void mat_identity(RMat4 *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

static void mat_mul(RMat4 *r, const RMat4 *a, const RMat4 *b)
{
    RMat4 t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float v = 0.0f;
            for (int k = 0; k < 4; k++)
                v += a->m[k * 4 + j] * b->m[i * 4 + k];
            t.m[i * 4 + j] = v;
        }
    *r = t;
}

static RMat4 *cur(void)
{
    if (s_mmode == R_MATRIX_PROJECTION) return &s_proj;
    if (s_mmode == R_MATRIX_VIEW) return &s_view;
    return &s_model;
}

void gl_r_matrix_mode(RMatrixMode m) { s_mmode = m; }
void gl_r_load_identity(void) { mat_identity(cur()); }
void gl_r_load_matrix(const RMat4 *m) { if (m) *cur() = *m; }
void gl_r_store_matrix(RMat4 *m) { if (m) *m = *cur(); }
void gl_r_update_matrix(void) {}
void gl_r_translate(const RVec3 *v)
{
    RMat4 t; mat_identity(&t); t.m[12] = v->x; t.m[13] = v->y; t.m[14] = v->z;
    mat_mul(cur(), cur(), &t);
}
void gl_r_rotate_x(float a)
{
    RMat4 t; mat_identity(&t);
    float c = cosf(a), s = sinf(a);
    t.m[5] = c; t.m[6] = s; t.m[9] = -s; t.m[10] = c;
    mat_mul(cur(), cur(), &t);
}
void gl_r_rotate_y(float a)
{
    RMat4 t; mat_identity(&t);
    float c = cosf(a), s = sinf(a);
    t.m[0] = c; t.m[2] = -s; t.m[8] = s; t.m[10] = c;
    mat_mul(cur(), cur(), &t);
}
void gl_r_rotate_z(float a)
{
    RMat4 t; mat_identity(&t);
    float c = cosf(a), s = sinf(a);
    t.m[0] = c; t.m[1] = s; t.m[4] = -s; t.m[5] = c;
    mat_mul(cur(), cur(), &t);
}
void gl_r_scale(const RVec3 *v)
{
    RMat4 *m = cur();
    m->m[0] *= v->x; m->m[1] *= v->x; m->m[2] *= v->x;
    m->m[4] *= v->y; m->m[5] *= v->y; m->m[6] *= v->y;
    m->m[8] *= v->z; m->m[9] *= v->z; m->m[10] *= v->z;
}
void gl_r_perspective(float fov, float aspect, float near_m, float far_m)
{
    RMat4 t; mat_identity(&t);
    float a = (fov * 0.5f) * (3.14159265f / 180.0f), cot = cosf(a) / sinf(a);
    float dz = near_m - far_m;
    t.m[0] = cot / aspect;
    t.m[5] = cot;
    t.m[10] = (far_m + near_m) / dz;
    t.m[11] = -1.0f;
    t.m[14] = 2.0f * far_m * near_m / dz;
    t.m[15] = 0.0f;
    mat_mul(cur(), cur(), &t);
}
void gl_r_look_at(const RVec3 *eye, const RVec3 *at, const RVec3 *up)
{
    RVec3 f = {at->x - eye->x, at->y - eye->y, at->z - eye->z};
    float l = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
    if (l < 1e-8f) return;
    f.x /= l; f.y /= l; f.z /= l;
    RVec3 s = {f.y * up->z - f.z * up->y, f.z * up->x - f.x * up->z, f.x * up->y - f.y * up->x};
    l = sqrtf(s.x * s.x + s.y * s.y + s.z * s.z);
    if (l < 1e-8f) return;
    s.x /= l; s.y /= l; s.z /= l;
    RVec3 u = {s.y * f.z - s.z * f.y, s.z * f.x - s.x * f.z, s.x * f.y - s.y * f.x};
    RMat4 t; mat_identity(&t);
    t.m[0] = s.x; t.m[4] = s.y; t.m[8] = s.z;
    t.m[1] = u.x; t.m[5] = u.y; t.m[9] = u.z;
    t.m[2] = -f.x; t.m[6] = -f.y; t.m[10] = -f.z;
    mat_mul(cur(), cur(), &t);
    RVec3 ie = {-eye->x, -eye->y, -eye->z};
    gl_r_translate(&ie);
}

// ---------------------------------------------------------------- GL state
static GLuint s_prog, s_present_prog;
static GLuint s_fbo, s_color, s_depth;
static GLuint s_vao, s_vbo, s_quad_vao, s_quad_vbo, s_white;
static GLint u_mvp, u_mv, u_tex, u_has_tex, u_replace, u_alpha_test;
static GLint u_fog_on, u_fog_color, u_fog_near, u_fog_far, u_uv_scale, u_uv_off, u_dither;
static GLint u_present_tex;

typedef struct {
    const void *key;
    int w, h;
    int dynamic;
    GLuint tex;
} TexCache;
static TexCache s_texcache[64];
static int s_ntex;

static RTexDesc s_tex;
static int s_tex_on;
static RTexFunc s_texfunc = R_TEXFUNC_MODULATE;
static RWrap s_wrap = R_WRAP_REPEAT;
static int s_fog_on = 1;
static uint32_t s_fog_color = 0xFF3C3834u;
static float s_fog_near = 20.0f, s_fog_far = 80.0f;
static int s_depth_test = 1, s_depth_write = 1, s_cull = 1, s_alpha_test;
static RBlend s_blend = R_BLEND_NONE;
static float s_uv_su = 1, s_uv_sv = 1, s_uv_ou, s_uv_ov;
static int s_vx, s_vy, s_vw = MC_LOWRES_W, s_vh = MC_LOWRES_H;
static int s_sx, s_sy, s_sw = MC_LOWRES_W, s_sh = MC_LOWRES_H;
static uint8_t *s_arena;
static int s_arena_cap, s_arena_used;

static GLuint compile(const char *vs, const char *fs)
{
    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v, 1, &vs, NULL);
    glShaderSource(f, 1, &fs, NULL);
    glCompileShader(v);
    glCompileShader(f);
    GLint ok = 0;
    glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(v, sizeof(log), NULL, log);
        fprintf(stderr, "gl vertex shader: %s\n", log);
        return 0;
    }
    glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(f, sizeof(log), NULL, log);
        fprintf(stderr, "gl fragment shader: %s\n", log);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "gl link: %s\n", log);
        return 0;
    }
    return p;
}

static const char *VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_mv;\n"
    "uniform vec2 u_uv_scale;\n"
    "uniform vec2 u_uv_off;\n"
    "uniform int u_fog_on;\n"
    "uniform float u_fog_near;\n"
    "uniform float u_fog_far;\n"
    "uniform int u_two_d;\n"
    "out vec2 v_uv;\n"
    "noperspective out vec4 v_color;\n"
    "noperspective out float v_fog;\n"
    "void main(){\n"
    "  vec4 p = u_mvp * vec4(a_pos,1.0);\n"
    "  if (u_two_d == 0) p.z = -p.z;\n"
    "  gl_Position = p;\n"
    "  v_uv = a_uv * u_uv_scale + u_uv_off;\n"
    "  v_color = a_color;\n"
    "  float d = -(u_mv * vec4(a_pos,1.0)).z;\n"
    "  v_fog = (u_fog_on == 0) ? 1.0 : clamp((u_fog_far - d) / (u_fog_far - u_fog_near), 0.0, 1.0);\n"
    "}\n";

static const char *FS =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "noperspective in vec4 v_color;\n"
    "noperspective in float v_fog;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_has_tex;\n"
    "uniform int u_replace;\n"
    "uniform int u_alpha_test;\n"
    "uniform int u_fog_on;\n"
    "uniform vec3 u_fog_color;\n"
    "uniform int u_dither;\n"
    "out vec4 frag;\n"
    "float bayer(vec2 p){\n"
    "  int x = int(mod(p.x,4.0)); int y = int(mod(p.y,4.0));\n"
    "  int m[16] = int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);\n"
    "  return float(m[y*4+x]) / 16.0 - 0.5;\n"
    "}\n"
    "void main(){\n"
    "  vec4 t = (u_has_tex == 0) ? vec4(1.0) : texture(u_tex, v_uv);\n"
    "  vec4 c = (u_replace == 0) ? t * v_color : vec4(t.rgb, t.a * v_color.a);\n"
    "  if (u_alpha_test == 1 && c.a <= 0.0) discard;\n"
    "  vec3 col = (u_fog_on == 0) ? c.rgb : mix(u_fog_color, c.rgb, v_fog);\n"
    "  if (u_dither == 1) col += vec3(bayer(gl_FragCoord.xy)) / 255.0;\n"
    "  frag = vec4(clamp(col,0.0,1.0), c.a);\n"
    "}\n";

static const char *PVS =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main(){ v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos,0.0,1.0); }\n";

static const char *PFS =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "void main(){ frag = vec4(texture(u_tex, v_uv).rgb, 1.0); }\n";

const char *gl_r_backend_name(void) { return "opengl"; }
void gl_r_size(int *w, int *h) { if (w) *w = IW; if (h) *h = IH; }
void gl_r_set_headless(int on) { (void)on; }
void gl_r_set_near(float n) { (void)n; }

static void upload_tex(const RTexDesc *t, GLuint tex)
{
    int w = t->w, h = t->h;
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) return;
    if (t->format == R_TEX_T4) {
        const uint8_t *px = t->pixels;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint8_t b = px[y * ((w + 1) / 2) + x / 2];
                int idx = (x & 1) ? (b >> 4) : (b & 15);
                uint32_t c = t->clut ? t->clut[idx] : 0xFFFFFFFFu;
                uint8_t *d = rgba + (y * w + x) * 4;
                d[0] = c & 0xFF; d[1] = (c >> 8) & 0xFF; d[2] = (c >> 16) & 0xFF; d[3] = (c >> 24) & 0xFF;
            }
    } else {
        const uint16_t *px = t->pixels;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint16_t c = px[y * w + x];
                uint8_t *d = rgba + (y * w + x) * 4;
                d[0] = ((c >> 11) & 31) * 255 / 31;
                d[1] = ((c >> 5) & 63) * 255 / 63;
                d[2] = (c & 31) * 255 / 31;
                d[3] = 255;
            }
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, t->wrap == R_WRAP_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, t->wrap == R_WRAP_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    free(rgba);
}

static int make_targets(void)
{
    if (s_fbo) {
        glDeleteFramebuffers(1, &s_fbo);
        glDeleteTextures(1, &s_color);
        glDeleteRenderbuffers(1, &s_depth);
    }
    glGenFramebuffers(1, &s_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glGenTextures(1, &s_color);
    glBindTexture(GL_TEXTURE_2D, s_color);
    if (s_native)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, IW, IH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, IW, IH, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_color, 0);
    glGenRenderbuffers(1, &s_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, s_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, IW, IH);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_depth);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? 0 : -1;
}

int gl_r_set_frame_size(int w, int h)
{
    if (w < 16 || h < 16 || w > 8192 || h > 8192) return -1;
    if (w == s_iw && h == s_ih) return 0;
    s_iw = w;
    s_ih = h;
    return s_inited ? make_targets() : 0;
}

void gl_r_set_native(int on)
{
    if (s_native == !!on) return;
    s_native = !!on;
    if (s_inited) make_targets();
}

int gl_r_init(const char *backend)
{
    (void)backend;
    if (plat_video_open_gl(IW, IH, 2, 0) != 0)
        return -1;
    if (load_gl() != 0) {
        plat_video_close();
        return -1;
    }
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    s_vao = vao;
    glGenBuffers(1, &s_vbo);
    s_prog = compile(VS, FS);
    s_present_prog = compile(PVS, PFS);
    if (!s_prog || !s_present_prog) {
        plat_video_close();
        return -2;
    }
    u_mvp = glGetUniformLocation(s_prog, "u_mvp");
    u_mv = glGetUniformLocation(s_prog, "u_mv");
    u_tex = glGetUniformLocation(s_prog, "u_tex");
    u_has_tex = glGetUniformLocation(s_prog, "u_has_tex");
    u_replace = glGetUniformLocation(s_prog, "u_replace");
    u_alpha_test = glGetUniformLocation(s_prog, "u_alpha_test");
    u_fog_on = glGetUniformLocation(s_prog, "u_fog_on");
    u_fog_color = glGetUniformLocation(s_prog, "u_fog_color");
    u_fog_near = glGetUniformLocation(s_prog, "u_fog_near");
    u_fog_far = glGetUniformLocation(s_prog, "u_fog_far");
    u_uv_scale = glGetUniformLocation(s_prog, "u_uv_scale");
    u_uv_off = glGetUniformLocation(s_prog, "u_uv_off");
    u_dither = glGetUniformLocation(s_prog, "u_dither");
    u_present_tex = glGetUniformLocation(s_present_prog, "u_tex");

    if (make_targets() != 0) {
        fprintf(stderr, "gl: incomplete framebuffer\n");
        plat_video_close();
        return -4;
    }
    glGenTextures(1, &s_white);
    glBindTexture(GL_TEXTURE_2D, s_white);
    const unsigned char white[4] = {255,255,255,255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenVertexArrays(1, &s_quad_vao);
    glGenBuffers(1, &s_quad_vbo);
    glBindVertexArray(s_quad_vao);
    const float quad[] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
    glBindBuffer(GL_ARRAY_BUFFER, s_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    mat_identity(&s_proj);
    mat_identity(&s_view);
    mat_identity(&s_model);
    mat_identity(&s_proj_stack[0]);
    mat_identity(&s_view_stack[0]);
    mat_identity(&s_model_stack[0]);
    s_proj_top = s_view_top = s_model_top = 0;
    s_mmode = R_MATRIX_MODEL;
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL);
    glClearDepth(0.0);
    s_inited = 1;
    return 0;
}

void gl_r_shutdown(void)
{
    if (!s_inited)
        return;
    for (int i = 0; i < s_ntex; i++)
        if (s_texcache[i].tex)
            glDeleteTextures(1, &s_texcache[i].tex);
    s_ntex = 0;
    glDeleteFramebuffers(1, &s_fbo);
    glDeleteTextures(1, &s_color);
    glDeleteTextures(1, &s_white);
    glDeleteRenderbuffers(1, &s_depth);
    s_fbo = 0;
    glDeleteProgram(s_prog);
    glDeleteProgram(s_present_prog);
    glDeleteBuffers(1, &s_vbo);
    glDeleteBuffers(1, &s_quad_vbo);
    glDeleteVertexArrays(1, &s_vao);
    glDeleteVertexArrays(1, &s_quad_vao);
    free(s_arena);
    s_arena = NULL;
    s_arena_cap = s_arena_used = 0;
    s_inited = 0;
}

void gl_r_begin_3d(void)
{
    s_two_d = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glViewport(0, 0, IW, IH);
    glScissor(0, 0, IW, IH);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glBindVertexArray(s_vao);
    s_tex_on = 0;
    s_uv_su = s_uv_sv = 1;
    s_uv_ou = s_uv_ov = 0;
    s_texfunc = R_TEXFUNC_MODULATE;
    s_wrap = R_WRAP_REPEAT;
    s_arena_used = 0;
    if (!s_arena) {
        s_arena_cap = 4 * 1024 * 1024;
        s_arena = malloc(s_arena_cap);
    }
}

void gl_r_begin_2d(void)
{
    s_two_d = 1;
    glBindVertexArray(s_vao);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glViewport(0, 0, IW, IH);
    glScissor(0, 0, IW, IH);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    s_fog_on = 0;
    s_tex_on = 0;
    s_uv_su = s_uv_sv = 1;
    s_uv_ou = s_uv_ov = 0;
    s_texfunc = R_TEXFUNC_MODULATE;
}

void gl_r_end_frame(void) {}

void gl_r_present(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int dw = IW, dh = IH;
    plat_video_drawable(&dw, &dh);
    glViewport(0, 0, dw, dh);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    int x, y, w, h;
    plat_video_present_rect(dw, dh, &x, &y, &w, &h);
    glViewport(x, y, w, h);
    glUseProgram(s_present_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_color);
    glUniform1i(u_present_tex, 0);
    glBindVertexArray(s_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    plat_video_swap_gl();
}

void gl_r_clear(uint32_t abgr, int clear_depth)
{
    glClearColor((abgr & 0xFF) / 255.0f, ((abgr >> 8) & 0xFF) / 255.0f,
                 ((abgr >> 16) & 0xFF) / 255.0f, ((abgr >> 24) & 0xFF) / 255.0f);
    glDepthMask(GL_TRUE);
    if (clear_depth >= 0) {
        glClearDepth(clear_depth / 65535.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else {
        glClear(GL_COLOR_BUFFER_BIT);
    }
}
void gl_r_clear_depth(int v)
{
    glDepthMask(GL_TRUE);
    glClearDepth(v / 65535.0);
    glClear(GL_DEPTH_BUFFER_BIT);
}
void gl_r_viewport(int x, int y, int w, int h)
{
    s_vx = x; s_vy = y; s_vw = w; s_vh = h;
    glViewport(x, IH - (y + h), w, h);
}
void gl_r_scissor(int x, int y, int w, int h)
{
    s_sx = x; s_sy = y; s_sw = w; s_sh = h;
    glScissor(x, IH - (y + h), w, h);
}
void gl_r_set_fog(int on) { s_fog_on = on; }
void gl_r_set_fog_params(float n, float f, uint32_t c) { s_fog_near = n; s_fog_far = f; s_fog_color = c; }
void gl_r_set_depth_test(int on) { s_depth_test = on; if (on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
void gl_r_set_depth_write(int on) { s_depth_write = on; glDepthMask(on ? GL_TRUE : GL_FALSE); }
void gl_r_set_cull(int on) { s_cull = on; if (on) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
void gl_r_set_alpha_test(int on) { s_alpha_test = on; }
void gl_r_set_tex_func(RTexFunc f) { s_texfunc = f; }
void gl_r_set_tex_wrap(RWrap w) { s_wrap = w; }
void gl_r_set_uv_scale(float u, float v) { s_uv_su = u; s_uv_sv = v; }
void gl_r_set_uv_offset(float u, float v) { s_uv_ou = u; s_uv_ov = v; }
void gl_r_set_blend(RBlend b)
{
    s_blend = b;
    if (b == R_BLEND_NONE) { glDisable(GL_BLEND); return; }
    glEnable(GL_BLEND);
    if (b == R_BLEND_ADD) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static GLuint bind_texture(const RTexDesc *t)
{
    if (!t) return 0;
    for (int i = 0; i < s_ntex; i++)
        if (s_texcache[i].key == t->pixels && s_texcache[i].w == t->w && s_texcache[i].h == t->h) {
            if (t->dynamic) upload_tex(t, s_texcache[i].tex);
            return s_texcache[i].tex;
        }
    if (s_ntex >= 64) s_ntex = 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    upload_tex(t, tex);
    s_texcache[s_ntex].key = t->pixels;
    s_texcache[s_ntex].w = t->w;
    s_texcache[s_ntex].h = t->h;
    s_texcache[s_ntex].dynamic = t->dynamic;
    s_texcache[s_ntex].tex = tex;
    s_ntex++;
    return tex;
}

void gl_r_bind_texture(const RTexDesc *t)
{
    s_tex_on = t != NULL;
    if (t) { s_tex = *t; s_wrap = t->wrap; }
    if (t) bind_texture(&s_tex);
}

void *gl_r_temp_alloc(int bytes)
{
    if (bytes <= 0 || !s_arena) return NULL;
    if (s_arena_used + bytes > s_arena_cap) return NULL;
    void *p = s_arena + s_arena_used;
    s_arena_used += (bytes + 15) & ~15;
    return p;
}
void gl_r_temp_reset(void) { s_arena_used = 0; }

typedef struct { float x, y, z, u, v; unsigned char c[4]; } GLV;

static void upload_and_draw(const void *data, int count, int native, GLenum prim)
{
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * 24, data, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    if (native) {
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)(uintptr_t)12);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 24, (void *)(uintptr_t)0);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 24, (void *)(uintptr_t)8);
    } else {
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)(uintptr_t)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 24, (void *)(uintptr_t)12);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 24, (void *)(uintptr_t)20);
    }
    glDrawArrays(prim, 0, count);
}

static void set_common_uniforms(const RMat4 *mvp, const RMat4 *mv)
{
    glUseProgram(s_prog);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp->m);
    glUniformMatrix4fv(u_mv, 1, GL_FALSE, mv->m);
    glUniform1i(u_has_tex, s_tex_on ? 1 : 0);
    glUniform1i(u_replace, s_texfunc == R_TEXFUNC_REPLACE ? 1 : 0);
    glUniform1i(u_alpha_test, s_alpha_test ? 1 : 0);
    glUniform1i(u_fog_on, s_fog_on ? 1 : 0);
    glUniform3f(u_fog_color, (s_fog_color & 0xFF) / 255.0f, ((s_fog_color >> 8) & 0xFF) / 255.0f,
                ((s_fog_color >> 16) & 0xFF) / 255.0f);
    glUniform1f(u_fog_near, s_fog_near);
    glUniform1f(u_fog_far, s_fog_far);
    glUniform2f(u_uv_scale, s_uv_su, s_uv_sv);
    glUniform2f(u_uv_off, s_uv_ou, s_uv_ov);
    glUniform1i(u_dither, s_two_d || s_native ? 0 : 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_tex_on ? bind_texture(&s_tex) : s_white);
    glUniform1i(u_tex, 0);
}

void gl_r_inset_3d(int on)
{
    if (on) {
        s_two_d = 0;
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    } else {
        gl_r_begin_2d();
    }
}

void gl_r_draw(RPrim prim, const RVertex *v, int count)
{
    if (!v || count <= 0) return;
    RMat4 mvp, mv;
    if (s_two_d) {
        RMat4 ortho; mat_identity(&ortho);
        ortho.m[0] = 2.0f / IW; ortho.m[5] = -2.0f / IH;
        ortho.m[12] = -1.0f; ortho.m[13] = 1.0f;
        mvp = ortho;
        mat_identity(&mv);
        set_common_uniforms(&mvp, &mv);
        glUniform1i(u_dither, 0);
        if (prim == R_PRIM_SPRITES) {
            int n = (count / 2) * 6;
            RVertex *tmp = gl_r_temp_alloc(n * sizeof(RVertex));
            if (!tmp) return;
            int k = 0;
            for (int i = 0; i + 1 < count; i += 2) {
                RVertex a = v[i], b = v[i + 1];
                tmp[k++] = (RVertex){a.x, a.y, 0, a.u, a.v, a.color};
                tmp[k++] = (RVertex){b.x, a.y, 0, b.u, a.v, a.color};
                tmp[k++] = (RVertex){b.x, b.y, 0, b.u, b.v, b.color};
                tmp[k++] = (RVertex){a.x, a.y, 0, a.u, a.v, a.color};
                tmp[k++] = (RVertex){b.x, b.y, 0, b.u, b.v, b.color};
                tmp[k++] = (RVertex){a.x, b.y, 0, a.u, b.v, b.color};
            }
            upload_and_draw(tmp, n, 0, GL_TRIANGLES);
            return;
        }
        upload_and_draw(v, count, 0, prim == R_PRIM_LINES ? GL_LINES :
                        (prim == R_PRIM_TRIANGLE_STRIP ? GL_TRIANGLE_STRIP : GL_TRIANGLES));
        return;
    }
    mat_mul(&mv, &s_view, &s_model);
    mat_mul(&mvp, &s_proj, &mv);
    set_common_uniforms(&mvp, &mv);
    upload_and_draw(v, count, 0, prim == R_PRIM_LINES ? GL_LINES :
                    (prim == R_PRIM_TRIANGLE_STRIP ? GL_TRIANGLE_STRIP : GL_TRIANGLES));
}

void gl_r_draw_native(RPrim prim, const RVertexNative *v, int count)
{
    if (!v || count <= 0 || s_two_d) return;
    RMat4 mvp, mv;
    mat_mul(&mv, &s_view, &s_model);
    mat_mul(&mvp, &s_proj, &mv);
    set_common_uniforms(&mvp, &mv);
    upload_and_draw(v, count, 1, prim == R_PRIM_TRIANGLE_STRIP ? GL_TRIANGLE_STRIP : GL_TRIANGLES);
}

void gl_r_copy_frame(uint16_t *dst, int stride)
{
    if (!dst) return;
    unsigned char *tmp = malloc((size_t)IW * IH * 4);
    if (!tmp) return;
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, IW, IH, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    // The engine's 16-bit format is RGB565 with blue in the high bits, red low.
    for (int y = 0; y < IH; y++) {
        uint16_t *row = dst + (size_t)(IH - 1 - y) * stride;
        const unsigned char *src = tmp + (size_t)y * IW * 4;
        for (int x = 0; x < IW; x++) {
            unsigned int r = src[x * 4 + 0], g = src[x * 4 + 1], b = src[x * 4 + 2];
            row[x] = (uint16_t)(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3));
        }
    }
    free(tmp);
}

void gl_r_copy_frame_rgb(unsigned char *dst)
{
    if (!dst) return;
    unsigned char *tmp = malloc((size_t)IW * IH * 4);
    if (!tmp) return;
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, IW, IH, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    for (int y = 0; y < IH; y++) {
        unsigned char *row = dst + (size_t)(IH - 1 - y) * IW * 3;
        const unsigned char *src = tmp + (size_t)y * IW * 4;
        for (int x = 0; x < IW; x++) { row[x * 3] = src[x * 4]; row[x * 3 + 1] = src[x * 4 + 1]; row[x * 3 + 2] = src[x * 4 + 2]; }
    }
    free(tmp);
}

#endif  // MC_NO_OPENGL