cl /Ox /W3 /MD playsb16.c
del playsb16.obj
cl /Ox /W3 /MD playds.c user32.lib dsound.lib /link /STACK:0x200000
del playds.obj