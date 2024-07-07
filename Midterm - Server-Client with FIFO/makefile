all: clean compile

compile: utility.h utility.c server.c client.c
	@echo "-------------------------------------------"
	@echo "Compiling all files..."
	@gcc utility.c server.c -o server
	@gcc utility.c client.c -o client
	
clean:
	@echo "-------------------------------------------"
	@echo "Removing files..."
	@rm -f *.o
	@rm -f server
	@rm -f client
