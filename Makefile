# if test ! -f config.h; then rm -f stamp-h1; make  stamp-h1; else :; fi
# [ http://gcc.gnu.org/onlinedocs/gcc-4.4.0/gcc/Preprocessor-Options.html ]
## gcc -DHAVE_CONFIG_H -I.     -g -O2 -MT main.o -MD -MP -MF .deps/main.Tpo -c -o main.o main.c
# CFLAGS=-DHAVE_CONFIG_H -I. -g -O2
CFLAGS=-g -O2
sshpass: main
	mv main sshpass
main: main.c

clean:
	rm -f sshpass *.o

#gcc $(CFLAGS) -c -o main.o main.c
#gcc  -g -O2   -o sshpass main.o
