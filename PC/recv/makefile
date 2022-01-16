program=test
OBJ = main.o
LIBS = -lSDL2 -lGLEW -lGL -lGLU
OPT=-O2 -g
CC=gcc
DEFS=
CFLAGS=${OPT} ${DEFS} -Werror

build: ${program}

clean:
	rm -f *.o ${program}

${program}: ${OBJ}
	${CC} ${OBJ} ${LIBS} -o ${program} ${OPT}

