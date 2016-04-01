all: clean
	g++ -fPIC -c -O0 -g explbr.cpp -I../intel-toolset/
	g++ -fPIC -O3 explbr.o ../intel-toolset/intel-lbr/liblbr.a -o explbr
clean:
	rm  -f explbr.o explbr
