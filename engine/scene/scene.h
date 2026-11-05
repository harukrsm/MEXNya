// Scene runtime: load a project made with the MEXNya Editor (the mexnya/ folder), draw
// its scenes and follow its Lines and Tunnels. The documents are the engine's JSON DOM,
// so a game can read and change any object at run time.
#ifndef MX_SCENE_SCENE_H
#define MX_SCENE_SCENE_H

#include "engine/core/json.h"

typedef struct MxProject MxProject;

typedef struct {
    float target[3];
    float yaw, pitch, dist;   // degrees, degrees, metres
    float fov;                // vertical, degrees
    int ortho;
} MxCamera;

// dir is the folder holding project.json and scenes/ (usually "<data>/mexnya").
MxProject *mx_project_load(const char *dir);
void mx_project_free(MxProject *p);

int mx_scene_count(const MxProject *p);
int mx_scene_find(const MxProject *p, const char *id);          // -1 if missing
const char *mx_scene_id(const MxProject *p, int scene);
int mx_scene_is_2d(const MxProject *p, int scene);
Json *mx_scene_doc(MxProject *p, int scene);                    // the whole scene document
Json *mx_object(MxProject *p, int scene, const char *id);       // an object by id, or NULL
int mx_scene_visible(const MxProject *p, int scene);
void mx_scene_set_visible(MxProject *p, int scene, int on);

// The camera saved with a 3D scene in the editor.
void mx_scene_camera(const MxProject *p, int scene, MxCamera *out);
// Sets projection and view for a viewport (call in the 3D pass, or inside r_inset_3d).
void mx_camera_apply(const MxCamera *c, int w, int h);
// The eye position for a camera (for listeners, culling, ...).
void mx_camera_eye(const MxCamera *c, float eye[3]);

// Draws a 3D scene's visible objects with the current camera (primitives are flat
// shaded; "model" objects load .mcm files relative to the data folder).
void mx_scene_draw_3d(MxProject *p, int scene);
// Draws a 2D scene's canvas at (x, y) in the 2D pass, `scale` canvas pixels per point.
void mx_scene_draw_2d(MxProject *p, int scene, int x, int y, float scale);
// Draws every visible scene in layer order: 3D layers with cam (call in the 3D pass),
// then switches to the 2D pass and draws 2D layers fitted to w x h points.
void mx_project_draw(MxProject *p, const MxCamera *cam, int w, int h);

// Lines and Tunnels. mx_emit("scene.port") fires a port: every Line leaving it fires the
// port at its other end; a Tunnel passes what arrives on a port on to the Lines leaving
// that port. Each port reached is reported to the handler. Built in: a scene's "show"
// and "hide" in-ports change its visibility. Returns the number of ports reached.
typedef void (*MxPortFn)(MxProject *p, const char *node, const char *port, void *user);
void mx_on_port(MxProject *p, MxPortFn fn, void *user);
int mx_emit(MxProject *p, const char *node_port);

#endif
