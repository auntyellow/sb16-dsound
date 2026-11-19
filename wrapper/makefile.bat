cl /O2 /W3 /MD /LD /Fedsound.dll dsound_wrapper.c dxguid.lib ..\dsound.def
del dsound_wrapper.obj
del dsound.exp
del dsound.lib