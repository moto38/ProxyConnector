CC=gcc
proxyconnector.exe : proxyconnector.o
	$(CC) -o $@ $< init_stdio.o -lws2_32

%.o: %.c
	$(CC) -g -DWIN32 -DDEBUG -I /usr/include -c $<

