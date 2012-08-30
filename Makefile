ocarina:
	gcc -o ocarina ocarina.c

tar: ocarina.c Makefile
	tar -cvf Ocarina.tar ocarina.c Makefile ../README.txt ../GUIDE.txt
	
clean:
	rm -r ocarina Ocarina.tar

