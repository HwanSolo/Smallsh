CC = gcc
SRC = smallsh.c

 
all:  smallsh

smallsh:
	$(CC)  ${SRC} -o smallsh

clean:
	rm -rf *.o smallsh
