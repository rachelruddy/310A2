CC=gcc
framesize ?= 18
varmemsize ?= 10

CFLAGS=-pthread -D FRAME_STORE_SIZE=$(framesize) -D VAR_STORE_SIZE=$(varmemsize)
FMT=indent

mysh: shell.c interpreter.c shellmemory.c scheduler.c
	$(CC) $(CFLAGS) -c shell.c interpreter.c shellmemory.c scheduler.c
	$(CC) $(CFLAGS) -o mysh shell.o interpreter.o shellmemory.o scheduler.o
	

style: shell.c shell.h interpreter.c interpreter.h shellmemory.c shellmemory.h scheduler.c scheduler.h
	$(FMT) $?

clean: 
	$(RM) mysh; $(RM) *.o; $(RM) *~

