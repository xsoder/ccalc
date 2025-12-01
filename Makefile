all: ccalc

.PHONY: ccalc

ccalc: ccalc.c
	cc -g -Wall -Wextra -Wno-unused-function -Wpedantic -o ccalc ccalc.c -lreadline

clean:
	rm ./ccalc
