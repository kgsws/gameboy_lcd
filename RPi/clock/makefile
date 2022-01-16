program = test
OBJ = main.o gblcd.o
LIBS =
OPT=-O2 -g
CC=gcc
CFLAGS=${OPT}

build: images.h ${program}

images.h:
	./imgconv.py

clean:
	rm -f *.o ${program} images.h

${program}: ${OBJ}
	${CC} ${OBJ} ${LIBS} -o ${program} ${OPT}

