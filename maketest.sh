i686-w64-mingw32-gcc -O3 -Wall -s playsb16.c -o playsb16.exe
i686-w64-mingw32-gcc -O3 -Wall -Wl,--stack,0x200000 -s playds.c -o playds.exe -ldsound