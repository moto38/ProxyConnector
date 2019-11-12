gcc -g -DWIN32 -DDEBUG -I /usr/include -c SimpleProxy.c
gcc -o SimpleProxy.exe SimpleProxy.o init_stdio.o -lws2_32
