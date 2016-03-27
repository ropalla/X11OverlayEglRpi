# X11OverlayEglRpi
X11 Overlay EGL on the Raspberry Pi - This is a small library which permits hardware accelerated OpenGLES on X11.

This Software is based on x11eglrpi by Mohamed Mediouni.
But instead of using Pixmaps, it renders directly into
the Hardware-Overlay.

# To install it, call:

sh ./compile.sh

sudo cp libEGL.so /opt/vc/lib/.

After installing it, it can be used as a
drop-in replacement for the
standared MESA-software-libraries.

sudo su -

cd /opt/vc/lib

ln -s libGLESv2.so libGLESv1_CM.so.1

ln -s libGLESv2.so libGLESv2.so.2

ln -s libEGL.so libEGL.so.1

exit

 To test it you may use es2gears_x11 from mesa-demos-8.2.0
 (The demos must NOT have been compiled with rpath, or you
   have to replace the original Libs - not recomended!)
   
LD_LIBRARY_PATH=/opt/vc/lib ./es2gears_x11

 or in the SDL2 demos (SDL2 should have been complied with --enable-video-opengles2)
 
LD_LIBRARY_PATH=/opt/vc/lib SDL_RENDER_DRIVER=opengles2 ./testdraw2

------------------------------------------

# Advantages:
- It is fast 
- It works with different color-depths
- You may use it with glshim, so you can
  have fast OpenGL (1.1).

# Problems (Any tips to solve this, are welcome):
- Its an overlay - so it hides the mousepointer
  and overlapping windows.
- There is a problem to detect window-movements.
  I check the X11-Window-Geometry with each eglSwapBuffers.
- if you use a windowmanager with virtual-screens (i.e. if you
  are using cool stuff like fvwm ;-), you may see some garbage
  on the other screens.

# Extensions for using EGL without X11.
The following environment-variables are supported.
- EGL_DISPMANX - possible values are (auto, yes, no) default is auto
  Tells EGL to manage Dispmanx itself.
- EGL_PLATFORM - possible value fbdev (default is unset)
- EGL_FB_X - horizontal start of the fbdev overlay (default is 0)
- EGL_FB_Y - vertical start of the fbdev overlay (default is 0)
- EGL_FB_WIDTH - horizontal size of the fbdev overlay (default is the screen-width)
- EGL_FB_HEIGHT - vertical size of the fbdev overlay (default is the screen-width)
- EGL_ALPHA - possible values or 0-255 (default is 255)

This can be tested with eglfbdev from mesa-demos-8.2.0
Example call:
LD_LIBRARY_PATH=/opt/vc/lib EGL_ALPHA=128 EGL_FB_HEIGHT=600 ./eglfbdev
