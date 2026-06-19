set INCLUDE=C:\WINDDK\2600.1106\inc\crt;C:\WINDDK\2600.1106\inc\wxp;C:\WINDDK\2600.1106\inc\ddk\wxp;
set LIB=C:\WINDDK\2600.1106\lib\wxp\i386

C:\WINDDK\2600.1106\bin\x86\cl.exe /c /D_X86_ /DSTD_CALL /DNDEBUG /Gz /Ox /W3 dsb16.c
C:\WINDDK\2600.1106\bin\x86\link.exe /SUBSYSTEM:NATIVE /DRIVER /ENTRY:DriverEntry /OUT:dsb16.sys dsb16.obj ntoskrnl.lib hal.lib
del dsb16.obj