rc dsound.rc
cl /DNDEBUG /O2 /W3 /MD /LD dsound.c dxguid.lib dsound.def dsound.res
del dsound.res
del dsound.obj
del dsound.exp
del dsound.lib