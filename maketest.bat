cl /O2 /W3 /MD playsb16.c dsb16_9x.c
del playsb16.obj
del dsb16_9x.obj
cl /O2 /W3 /MD playds.c user32.lib dsound.lib /link /STACK:0x200000
del playds.obj