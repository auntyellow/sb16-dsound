i686-w64-mingw32-windres dsound.rc -o version.o
i686-w64-mingw32-gcc -DNDEBUG -O3 -Wall -Wl,--enable-stdcall-fixup -s dsound.c -shared -o dsound.dll -ldxguid dsound.def version.o
rm version.o