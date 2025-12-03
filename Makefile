RL_LIBS=-lraylib -lX11 -lGL -lm

all: build run

build:
	cc -o main main.c -ggdb $(RL_LIBS)

run:
	./main
