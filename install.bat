sc stop DSB16
sc delete DSB16
copy dsb16.sys %SystemRoot%\System32\drivers
sc create DSB16 type= kernel binPath= System32\drivers\dsb16.sys start= system
sc start DSB16