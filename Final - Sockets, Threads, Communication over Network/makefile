all: clean compile

compile: utils.c client.c server.c
	@echo "-------------------------------------------"
	@echo "Compiling all files..."
	@gcc utils.c server.c -o PideShop -pthread -lm
	@gcc utils.c client.c -o HungryVeryMuch
	
clean:
	@echo "-------------------------------------------"
	@echo "Removing files..."
	@rm -f *.o
	@rm -f PideShop
	@rm -f HungryVeryMuch
