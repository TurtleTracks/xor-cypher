all: encrypt print

encrypt:
	g++ -pthread -o encryptUtil encrypt.cc
print:
	g++ -pthread -o printUtil print_threads.cc
