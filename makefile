
all: libmyclose.so

#LIBS=-lunwind
LIBS=

libmyclose.so: myclose.cpp
	$(CXX) -Wall -O0 -shared -std=gnu++11 -fPIC -o $@ $< $(LIBS)

clean:
	$(RM) -f libmyclose.so

