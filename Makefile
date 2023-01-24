.ONESHELL : # 
.PHONY : run inject

run: main injector hack.so
	LD_DEBUG=1 ./main | ./injector

inject: main hack.so
	LD_PRELOAD=./hack.so ./main
	./main "$$PWD/hack.so"

main: main.cpp
	clang++ main.cpp -o main

injector: injector.cpp
	clang++ -std=gnu++20 injector.cpp -o injector

hack.so: hack.cpp
	clang++ -fPIC -shared hack.cpp -o hack.so

clean:
	rm -f main injector hack.so

