demo: demo.cc promise.h
	g++ -std=c++17 -Wall -g $< -o $@

.phony: clean

clean:
	rm -rf *.o demo
