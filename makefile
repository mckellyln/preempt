
all: libmyclose.so

#LIBS=-lunwind -ldl
LIBS=-ldl

libmyclose.so: myclose.cpp makefile
	$(CXX) -Wall -O0 -shared -std=gnu++11 -fPIC -o $@ $< $(LIBS)

clean:
	$(RM) -f libmyclose.so

