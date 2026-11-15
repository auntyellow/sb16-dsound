rc dsound.rc
cl /DNDEBUG /O2 /W3 /MD /LD dsound.c dsb16_9x.c dxguid.lib dsound.def dsound.res
del dsound.res
del dsound.obj
del dsb16_9x.obj
del dsound.exp
del dsound.lib