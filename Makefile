
run: a.out
	valgrind  --leak-check=full ./a.out

a.out: test.o
	g++ -g $< -o $@ -I ./ -lpthread

clean:
	@rm test.o a.out

