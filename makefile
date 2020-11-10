.RECIPEPREFIX = -
.PHONE: all clean test setup

CINCLUDE = -I..

all: setup build/subp

setup:
- mkdir -p build

build/subp: subp.cpp broker.cpp
- g++ --std=c++17 -DDEBUG_ON -o $@ $^

clean:
- rm -rf build/ || true

test: setup build/subp
- ./build/subp 'echo hello "world!"; echo done; echo this is my error 1>&2'

test-slow: setup build/subp
- ./build/subp 'echo start...; sleep 33; echo done'

valgrind: setup build/subp
- valgrind --leak-check=full ./build/subp 'echo hello "world!"; echo done'
