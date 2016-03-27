rm -rf ./libegl-unpack
mkdir -p ./libegl-unpack
gcc -O2 -g -L/opt/vc/lib-ORIG -lEGL -L/opt/vc/lib -lGLESv2 -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux -c k_eglGetDisplayCopy.c
cd ./libegl-unpack
ar x /opt/vc/lib/libkhrn_static.a
rm -f khrn_client_platform_openwfc.c.o egl_client.c.o
ar x /opt/vc/lib/libEGL_static.a
rm -f k_eglGetDisplayCopy.c.o
mv ../k_eglGetDisplayCopy.o k_eglGetDisplayCopy.c.o
objcopy \
 --redefine-sym eglGetDisplay=real_eglGetDisplay \
 --redefine-sym eglCreateWindowSurface=real_eglCreateWindowSurface \
 --redefine-sym eglSwapBuffers=real_eglSwapBuffers \
 --redefine-sym eglMakeCurrent=real_eglMakeCurrent \
 --redefine-sym eglGetConfigAttrib=real_eglGetConfigAttrib \
 --redefine-sym eglDestroySurface=real_eglDestroySurface \
 --redefine-sym eglTerminate=real_eglTerminate \
     egl_client.c.o egl_client-modified.c.o
rm -rf egl_client.c.o
cd ..
gcc -shared -o libEGL.so -L/opt/vc/lib libegl-unpack/*.o -g -L/opt/vc/lib -lbcm_host -lX11
# /opt/vc/lib/libkhrn_static.a
echo "### Compilation ENDED - do something like:"
echo "sudo cp libEGL.so /opt/vc/lib/."