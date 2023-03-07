USERID=123456789

default: build

#added -g dbg symbols
build: server.c client.c
	gcc -Wall -Wextra -o server -g server.c 
	gcc -Wall -Wextra -o client -g client.c

clean:
	rm -rf *.o server client *.tar.gz

dist: zip
zip: clean
	zip ${USERID}.zip server.c client.c Makefile