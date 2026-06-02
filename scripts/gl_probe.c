/*
 * gl_probe.c - Decide whether the host can give Chiplet Studio the OpenGL 3.3
 * Core context its shaders require.
 *
 * Exit 0  -> a 3.3 Core context was created on $DISPLAY (use host GL).
 * Exit !=0 -> not available (launcher should fall back to bundled software GL).
 *
 * Offscreen (pbuffer), creates no visible window. Link: -lGL -lX11
 */
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <stdio.h>

typedef GLXContext (*CreateCtxAttribs)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

static int g_xerror = 0;
static int xerror_handler(Display* d, XErrorEvent* e) { (void)d; (void)e; g_xerror = 1; return 0; }

int main(void) {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "gl_probe: cannot open display\n"); return 2; }

    int attribs[] = {
        GLX_X_RENDERABLE, True,
        GLX_RENDER_TYPE,  GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT | GLX_WINDOW_BIT,
        GLX_DEPTH_SIZE, 24,
        None
    };
    int n = 0;
    GLXFBConfig* fbc = glXChooseFBConfig(dpy, DefaultScreen(dpy), attribs, &n);
    if (!fbc || n == 0) { fprintf(stderr, "gl_probe: no FBConfig\n"); return 3; }

    CreateCtxAttribs createCtx =
        (CreateCtxAttribs)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
    if (!createCtx) { fprintf(stderr, "gl_probe: no GLX_ARB_create_context\n"); return 4; }

    int (*old)(Display*, XErrorEvent*) = XSetErrorHandler(xerror_handler);
    int ctx_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };
    GLXContext ctx = createCtx(dpy, fbc[0], 0, True, ctx_attribs);
    XSync(dpy, False);
    XSetErrorHandler(old);
    if (g_xerror || !ctx) { fprintf(stderr, "gl_probe: no 3.3 core context\n"); return 1; }

    int pb[] = { GLX_PBUFFER_WIDTH, 1, GLX_PBUFFER_HEIGHT, 1, None };
    GLXPbuffer buf = glXCreatePbuffer(dpy, fbc[0], pb);
    if (!glXMakeContextCurrent(dpy, buf, buf, ctx)) {
        fprintf(stderr, "gl_probe: makeCurrent failed\n"); return 5;
    }
    const char* ver = (const char*)glGetString(GL_VERSION);
    const char* ren = (const char*)glGetString(GL_RENDERER);
    printf("%s | %s\n", ver ? ver : "?", ren ? ren : "?");
    return 0;
}
