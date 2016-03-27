#include <EGL/egl.h>
#include <EGL/eglext_brcm.h>
#include <GLES2/gl2.h>
#include <bcm_host.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

/*  
Copyright (C)2016 Rudolf Opalla

This is based on X11EGLRPI
Copyright (C)2014 Mohamed MEDIOUNI

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

/* #include <vc_vchi_dispmanx.h> */
#define CHANGE_LAYER    (1<<0)
#define CHANGE_OPACITY  (1<<1)
#define CHANGE_DEST     (1<<2)
#define CHANGE_SRC      (1<<3)
#define CHANGE_MASK     (1<<4)
#define CHANGE_XFORM    (1<<5)

#ifdef DEBUG
#define DPRINTF(...) fprintf(stderr,__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

typedef struct {
  int isActive;
  int x11Enabled;
  int dispmanxEnabled;
  EGLDisplay eglDisplay;
  /* FB */
  NativeDisplayType nativeDisplay;
  /* X11 */
  Display* x11Display;
  Window x11Root;
} BcmhostDisplayInfo;

typedef struct {
  int isActive;

  int displayIndex;
  EGLSurface eglSurface;
  EGLContext eglContext;

  EGL_DISPMANX_WINDOW_T dispmanxWindow;
  DISPMANX_ELEMENT_HANDLE_T dispmanxElement;
  DISPMANX_DISPLAY_HANDLE_T dispmanxDisplay;
  VC_RECT_T destinationRect;

  /* FB */
  NativeWindowType nativeWindow;
  /* X11 */
  Window	     x11Window;
} BcmhostSurfaceInfo;

static BcmhostDisplayInfo* bcmhostDisplayInfos = NULL;
static int allocatedBcmhostDisplayInfos = 0;
static int usedBcmhostDisplayInfos = 0;

static BcmhostSurfaceInfo* bcmhostSurfaceInfos = NULL;
static int allocatedBcmhostSurfaceInfos = 0;
static int usedBcmhostSurfaceInfos = 0;

static int bcm_host_initialized=0;
static int egl_dispmanx = 0;
static int egl_fbdev = 0;
static int egl_fb_x = -1;
static int egl_fb_y = -1;
static int egl_fb_width = -1;
static int egl_fb_height = -1;
static int egl_fb_alpha = -1;

static int reserveBcmhostDisplayInfo() {
  int ret;

  for (ret=0; ret<usedBcmhostDisplayInfos; ret++) {
    if (!bcmhostDisplayInfos[ret].isActive) {
      return ret;
    }
  }
  if (allocatedBcmhostDisplayInfos <= 0) {
    allocatedBcmhostDisplayInfos = 2;
    bcmhostDisplayInfos = (BcmhostDisplayInfo *) malloc(allocatedBcmhostDisplayInfos * sizeof(BcmhostDisplayInfo));
  } else if (usedBcmhostDisplayInfos >= allocatedBcmhostDisplayInfos) {
    allocatedBcmhostDisplayInfos *= 2;
    bcmhostDisplayInfos = (BcmhostDisplayInfo *) realloc(bcmhostDisplayInfos, allocatedBcmhostDisplayInfos * sizeof(BcmhostDisplayInfo));
  }
  ret = usedBcmhostDisplayInfos;
  usedBcmhostDisplayInfos++;
  return ret;
}

static int reserveBcmhostSurfaceInfo() {
  int ret;
  
  for (ret=0; ret<usedBcmhostSurfaceInfos; ret++) {
    if (!bcmhostSurfaceInfos[ret].isActive) {
      return ret;
    }
  }
  if (allocatedBcmhostSurfaceInfos <= 0) {
    allocatedBcmhostSurfaceInfos = 2;
    bcmhostSurfaceInfos = (BcmhostSurfaceInfo *) malloc(allocatedBcmhostSurfaceInfos * sizeof(BcmhostSurfaceInfo));
  } else if (usedBcmhostSurfaceInfos >= allocatedBcmhostSurfaceInfos) {
    allocatedBcmhostSurfaceInfos *= 2;
    bcmhostSurfaceInfos = (BcmhostSurfaceInfo *) realloc(bcmhostSurfaceInfos, allocatedBcmhostSurfaceInfos * sizeof(BcmhostSurfaceInfo));
  }
  ret = usedBcmhostSurfaceInfos;
  usedBcmhostSurfaceInfos++;
  return ret;
}

static int findDisplayIndex(EGLSurface edisplay) {
  int i;

  for (i=0; i<usedBcmhostDisplayInfos; i++) {
    if ((bcmhostDisplayInfos[i].isActive) &&
        (edisplay == bcmhostDisplayInfos[i].eglDisplay)) {
      return i;
    }
  }
  return -1;
}

static int findSurfaceIndex(EGLSurface surface) {
  int i;

  for (i=0; i<usedBcmhostSurfaceInfos; i++) {
    if ((bcmhostSurfaceInfos[i].isActive) &&
        (surface == bcmhostSurfaceInfos[i].eglSurface)) {
      return i;
    }
  }
  return -1;
}

static void getEnvironmentVariables() {
  char *env_str;

  env_str = getenv("EGL_DISPMANX");
  egl_dispmanx = 0;
  if (env_str != NULL) {
    if (strcmp(env_str, "auto") == 0) {
      egl_dispmanx = 0;
    } else if (strcmp(env_str, "yes") == 0) {
      egl_dispmanx = 1;
    } else if (strcmp(env_str, "no") == 0) {
      egl_dispmanx = -1;
    }
  }
  env_str = getenv("EGL_PLATFORM");
  if ((env_str != NULL) && (strcmp(env_str, "fbdev") == 0)) {
    egl_fbdev = 1;
  } else {
    egl_fbdev = 0;
  }
  env_str = getenv("EGL_FB_X");
  if (env_str != NULL) {
    egl_fb_x = atoi(env_str);
  }
  env_str = getenv("EGL_FB_Y");
  if (env_str != NULL) {
    egl_fb_y = atoi(env_str);
  }
  env_str = getenv("EGL_FB_WIDTH");
  if (env_str != NULL) {
    egl_fb_width = atoi(env_str);
  }
  env_str = getenv("EGL_FB_HEIGHT");
  if (env_str != NULL) {
    egl_fb_height = atoi(env_str);
  }
  env_str = getenv("EGL_FB_ALPHA");
  if (env_str != NULL) {
    egl_fb_alpha = atoi(env_str);
  }
}

EGLDisplay eglGetDisplay(NativeDisplayType native_display) {
  int i;

  getEnvironmentVariables();

  for (i=0; i<usedBcmhostDisplayInfos; i++) {
    if ((bcmhostDisplayInfos[i].isActive) && (native_display == bcmhostDisplayInfos[i].nativeDisplay)) {
      return bcmhostDisplayInfos[i].eglDisplay;
    }
  }
  i = reserveBcmhostDisplayInfo();
  bcmhostDisplayInfos[i].isActive = 1;
  bcmhostDisplayInfos[i].nativeDisplay = native_display;
  if ((egl_fbdev) || ( native_display == EGL_DEFAULT_DISPLAY)) {
    bcmhostDisplayInfos[i].x11Enabled = 0;
    if ((egl_dispmanx > 0) ||
	((egl_dispmanx == 0) && (egl_fbdev))) {
      bcmhostDisplayInfos[i].dispmanxEnabled = 1;
    } else {
      bcmhostDisplayInfos[i].dispmanxEnabled = 0;
    }
  } else {
    bcmhostDisplayInfos[i].x11Enabled = 1;
    bcmhostDisplayInfos[i].dispmanxEnabled = 1;
    bcmhostDisplayInfos[i].x11Display = (Display*)native_display;
    bcmhostDisplayInfos[i].x11Root = DefaultRootWindow(bcmhostDisplayInfos[i].x11Display);
  }
  if ((!bcm_host_initialized) && (bcmhostDisplayInfos[i].dispmanxEnabled)) {
    bcm_host_init();
    bcm_host_initialized = 1;
  }
  bcmhostDisplayInfos[i].eglDisplay = (EGLDisplay) real_eglGetDisplay(EGL_DEFAULT_DISPLAY);

  DPRINTF("eglGetDisplay: dispmanx: %d  x11_enabled: %d  native_display: %d\n",
	bcmhostDisplayInfos[i].dispmanxEnabled,
	bcmhostDisplayInfos[i].x11Enabled,
	(int) bcmhostDisplayInfos[i].nativeDisplay);

  return bcmhostDisplayInfos[i].eglDisplay;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
			      EGLint attribute, EGLint *value) {
  XVisualInfo *egl_visualinfo = NULL;
  XVisualInfo vi_in;
  int out_count;
  int display_index;

  /* We have to return the Visual-ID of the X-Display or of the Framebuffer here, */
  /* or eglut or standard sdl initialization will fail */
  if (attribute == EGL_NATIVE_VISUAL_ID) {
    display_index = findDisplayIndex(dpy);
    if (display_index >= 0) {
      DPRINTF("eglGetConfigAttrib: EGL_NATIVE_VISUAL_ID-found: %d\n", display_index);
      if (bcmhostDisplayInfos[display_index].x11Enabled != 0) {
/*
        // if you want to make sure it is a 32-Bit Truecolor-Display
        XVisualInfo *vinfo_return;

        if (XMatchVisualInfo(bcmhostDisplayInfos[display_index].x11Display, DefaultScreen(bcmhostDisplayInfos[display_index].x11Display), 32, TrueColor, &vinfo_return)) {
          *value = vinfo_return.visualid;
          return EGL_TRUE;
        }
*/
        /* get DefaultVisualId */
        vi_in.screen = DefaultScreen(bcmhostDisplayInfos[display_index].x11Display);
        egl_visualinfo = XGetVisualInfo(bcmhostDisplayInfos[display_index].x11Display,
                                            VisualScreenMask,
                                            &vi_in, &out_count);
        if (egl_visualinfo != NULL) {
          *value = egl_visualinfo->visualid;
          DPRINTF("  eglGetConfigAttrib: X11-EGL_NATIVE_VISUAL_ID-val: %d=%d\n", attribute, *value);
          return EGL_TRUE;
        }
      } else if (egl_fbdev) {
        *value = (int) bcmhostDisplayInfos[display_index].nativeDisplay;
        return EGL_TRUE;
      }
    }
  }
  DPRINTF("eglGetConfigAttrib: DEFAULT: %d\n", attribute);
  return real_eglGetConfigAttrib(dpy, config, attribute, value);
}

static int getX11WindowRect(int drawable_index, VC_RECT_T *rect) {
  XWindowAttributes window_attributes;
  Window             root_return, child_return;
  int                x_return, y_return;
  unsigned int       width_return, height_return, border_width_return, depth_return;

  XGetWindowAttributes(bcmhostDisplayInfos[bcmhostSurfaceInfos[drawable_index].displayIndex].x11Display,
                       bcmhostSurfaceInfos[drawable_index].x11Window,
                       &window_attributes);
  rect->width = window_attributes.width;
  rect->height = window_attributes.height;

  XGetGeometry(bcmhostDisplayInfos[bcmhostSurfaceInfos[drawable_index].displayIndex].x11Display,
               bcmhostSurfaceInfos[drawable_index].x11Window,
               &root_return, &x_return, &y_return,
               &width_return, &height_return, &border_width_return, &depth_return);
  XTranslateCoordinates(bcmhostDisplayInfos[bcmhostSurfaceInfos[drawable_index].displayIndex].x11Display,
                        bcmhostSurfaceInfos[drawable_index].x11Window,
                        bcmhostDisplayInfos[bcmhostSurfaceInfos[drawable_index].displayIndex].x11Root,
                        0, 0, &x_return, &y_return, &child_return);
  rect->x = x_return;
  rect->y = y_return;

  return 1;
}

EGLSurface eglCreateWindowSurface(EGLDisplay egldisplay, EGLConfig config,
				  NativeWindowType native_window,
				  EGLint const * attrib_list) {
  DISPMANX_UPDATE_HANDLE_T dispman_update;
  VC_RECT_T src_rect;
  int display_index, drawable_index, success;
  VC_DISPMANX_ALPHA_T alpha = { DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS, 
                             255, /*alpha 0->255*/
                             0 };
  display_index = findDisplayIndex(egldisplay);
  /* something went wrong */
  assert(display_index >= 0);

  DPRINTF("eglCreateWindowSurface: disp: %d   drawable: %d\n", drawable_index, display_index);

  drawable_index = reserveBcmhostSurfaceInfo();
  bcmhostSurfaceInfos[drawable_index].displayIndex = display_index;
  bcmhostSurfaceInfos[drawable_index].nativeWindow = native_window;
  bcmhostSurfaceInfos[drawable_index].isActive = 1;

  if (bcmhostDisplayInfos[display_index].x11Enabled == 0) {
    if (bcmhostDisplayInfos[display_index].dispmanxEnabled) {
      if (egl_fb_x > 0) {
        bcmhostSurfaceInfos[drawable_index].destinationRect.x = egl_fb_x;
      } else {
        bcmhostSurfaceInfos[drawable_index].destinationRect.x = 0;
      }
      if (egl_fb_y > 0) {
        bcmhostSurfaceInfos[drawable_index].destinationRect.y = egl_fb_y;
      } else {
        bcmhostSurfaceInfos[drawable_index].destinationRect.y = 0;
      }
      success = graphics_get_display_size(0 /* LCD */,
					&(bcmhostSurfaceInfos[drawable_index].destinationRect.width),
					&(bcmhostSurfaceInfos[drawable_index].destinationRect.height));
      assert( success >= 0 );
      if (egl_fb_width > 0) {
        bcmhostSurfaceInfos[drawable_index].destinationRect.width = egl_fb_width;
      } else {
        bcmhostSurfaceInfos[drawable_index].destinationRect.width -= bcmhostSurfaceInfos[drawable_index].destinationRect.x;
      }
      if (egl_fb_height > 0) {
        bcmhostSurfaceInfos[drawable_index].destinationRect.height = egl_fb_height;
      } else {
        bcmhostSurfaceInfos[drawable_index].destinationRect.height -= bcmhostSurfaceInfos[drawable_index].destinationRect.y;
      }
      if (egl_fb_alpha >= 0) {
        alpha.opacity = egl_fb_alpha;
      }
    }
  } else {
    bcmhostSurfaceInfos[drawable_index].x11Window = (Window) native_window;
    assert(getX11WindowRect(drawable_index, &(bcmhostSurfaceInfos[drawable_index].destinationRect)) > 0);
  }

  if (bcmhostDisplayInfos[display_index].dispmanxEnabled) {
    bcmhostSurfaceInfos[drawable_index].dispmanxWindow.width = bcmhostSurfaceInfos[drawable_index].destinationRect.width;
    bcmhostSurfaceInfos[drawable_index].dispmanxWindow.height = bcmhostSurfaceInfos[drawable_index].destinationRect.height;
    DPRINTF("  eglCreateWindowSurface-dispmanx: x11: %d x: %d y: %d w: %d h: %d\n",
	bcmhostDisplayInfos[display_index].x11Enabled,
	bcmhostSurfaceInfos[drawable_index].destinationRect.x,
	bcmhostSurfaceInfos[drawable_index].destinationRect.y,
	bcmhostSurfaceInfos[drawable_index].destinationRect.width,
	bcmhostSurfaceInfos[drawable_index].destinationRect.height);
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = bcmhostSurfaceInfos[drawable_index].destinationRect.width << 16;
    src_rect.height = bcmhostSurfaceInfos[drawable_index].destinationRect.height << 16;

    bcmhostSurfaceInfos[drawable_index].dispmanxDisplay = vc_dispmanx_display_open( 0 /* LCD */);
    dispman_update = vc_dispmanx_update_start( 0 );
    bcmhostSurfaceInfos[drawable_index].dispmanxElement =
        vc_dispmanx_element_add(dispman_update, bcmhostSurfaceInfos[drawable_index].dispmanxDisplay,
                                drawable_index/*layer*/,
				&(bcmhostSurfaceInfos[drawable_index].destinationRect), 0/*src*/,
                                &src_rect, DISPMANX_PROTECTION_NONE,
                                &alpha /*alpha*/, 0/*clamp*/, 0/*transform*/);
    vc_dispmanx_update_submit_sync( dispman_update );
    bcmhostSurfaceInfos[drawable_index].dispmanxWindow.element = bcmhostSurfaceInfos[drawable_index].dispmanxElement;
    bcmhostSurfaceInfos[drawable_index].eglSurface = (EGLSurface) real_eglCreateWindowSurface(
        bcmhostDisplayInfos[display_index].eglDisplay,
        config, &(bcmhostSurfaceInfos[drawable_index].dispmanxWindow), attrib_list );
  } else {
    bcmhostSurfaceInfos[drawable_index].eglSurface = (EGLSurface) real_eglCreateWindowSurface(egldisplay, config,
				  native_window,
				  attrib_list);
  }

  return bcmhostSurfaceInfos[drawable_index].eglSurface;
}

EGLBoolean eglSwapBuffers(EGLDisplay edisplay, EGLSurface eglsurface) {
  int drawable_index, rc;
  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;
  DISPMANX_UPDATE_HANDLE_T dispman_update;

  drawable_index = findSurfaceIndex(eglsurface);
  if ((drawable_index >= 0) &&
      (bcmhostDisplayInfos[bcmhostSurfaceInfos[drawable_index].displayIndex].x11Enabled)) {
    /* TODO: there must be a better way to determine if the x-window-geometry changed */
    if (getX11WindowRect(drawable_index, &dst_rect)) {
      if ((bcmhostSurfaceInfos[drawable_index].destinationRect.x != dst_rect.x) ||
	  (bcmhostSurfaceInfos[drawable_index].destinationRect.y != dst_rect.y) ||
	  (bcmhostSurfaceInfos[drawable_index].destinationRect.width != dst_rect.width) ||
	  (bcmhostSurfaceInfos[drawable_index].destinationRect.height != dst_rect.height)) {
        DPRINTF("  eglSwapBuffers change x-geometry: x:%d->%d y:%d->%d w:%d->%d h:%d->%d\n",
           bcmhostSurfaceInfos[drawable_index].destinationRect.x, dst_rect.x,
           bcmhostSurfaceInfos[drawable_index].destinationRect.y, dst_rect.y,
           bcmhostSurfaceInfos[drawable_index].destinationRect.width, dst_rect.width,
           bcmhostSurfaceInfos[drawable_index].destinationRect.height, dst_rect.height);
	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = dst_rect.width << 16;
	src_rect.height = dst_rect.height << 16;
        bcmhostSurfaceInfos[drawable_index].dispmanxWindow.width = dst_rect.width;
        bcmhostSurfaceInfos[drawable_index].dispmanxWindow.height = dst_rect.height;
        dispman_update = vc_dispmanx_update_start( 0 );	
        rc = vc_dispmanx_element_change_attributes(dispman_update,
                                bcmhostSurfaceInfos[drawable_index].dispmanxElement,
                                CHANGE_LAYER|CHANGE_SRC|CHANGE_DEST|CHANGE_XFORM,
                                drawable_index /* layer */,
                                0 /* opacity */,
                                &dst_rect,
                                &src_rect,
                                0/* mask */, 0/* transform */);
        vc_dispmanx_update_submit_sync( dispman_update );

        if (rc != 0) {
          DPRINTF("ERROR: Change atts: %d\n", rc);
	} else {
          memcpy(&(bcmhostSurfaceInfos[drawable_index].destinationRect),
	       &dst_rect,
	       sizeof(VC_RECT_T));
	}
      }
    }
  }
  DPRINTF("  eglSwapBuffers: drawable: %d display: %d\n", drawable_index,
	bcmhostSurfaceInfos[drawable_index].displayIndex);

  return real_eglSwapBuffers(edisplay,eglsurface);
}

EGLBoolean eglMakeCurrent(EGLDisplay egldisplay, EGLSurface eglsurfdraw, EGLSurface eglsurfread, EGLContext context) {
  int drawable_index;

  drawable_index = findSurfaceIndex(eglsurfdraw);
  if (drawable_index >= 0) {
    bcmhostSurfaceInfos[drawable_index].eglContext = context;
    DPRINTF("eglMakeCurrent: drawable: %d display: %d\n", drawable_index,
       bcmhostSurfaceInfos[drawable_index].displayIndex);
  }
  return real_eglMakeCurrent(egldisplay,eglsurfdraw,eglsurfread,context);
}


EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
  DISPMANX_UPDATE_HANDLE_T dispman_update;
  int drawable_index;
  EGLBoolean ret;

  drawable_index = findSurfaceIndex(surface);
  ret = real_eglDestroySurface(dpy, surface);
  if (drawable_index >= 0) {
    if (bcmhostDisplayInfos[bcmhostSurfaceInfos[drawable_index].displayIndex].dispmanxEnabled) {
      dispman_update = vc_dispmanx_update_start( 10 );
      vc_dispmanx_element_remove(dispman_update, bcmhostSurfaceInfos[drawable_index].dispmanxElement );
      vc_dispmanx_update_submit_sync( dispman_update );
      vc_dispmanx_display_close( bcmhostSurfaceInfos[drawable_index].dispmanxDisplay );
    }
    bcmhostSurfaceInfos[drawable_index].isActive = 0;
  }
  return ret;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
  EGLBoolean ret;

  ret = real_eglTerminate(dpy);

  /* TODO: real cleanup */
  if (bcm_host_initialized) {
    bcm_host_deinit();
    bcm_host_initialized = 0;
  }
  usedBcmhostDisplayInfos = 0;
  usedBcmhostSurfaceInfos = 0;

  return ret;
}