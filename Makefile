RL_LIBS=-lraylib -lX11 -lGL -lm

all: build-raylib run

build-raylib:
	cc -o main ./examples/raylib_example.c -ggdb $(RL_LIBS) -fsanitize=address

run:
	./main
