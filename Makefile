default:tls_lib

sample_grader:autograder_main.c tls_lib
	g++ autograder_main.c tls.o -o sample_grader -pthread

tls_lib:tls.cpp
	g++ -c tls.cpp -o tls.o -pthread

clean:
	rm -f sample_grader *.o
