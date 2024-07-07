all: clean compile run

compile: main.c 
	@echo "-------------------------------------------"
	@echo "Compiling all files..."
	@gcc -o hw1_emreoytun main.c
run:
	@echo "-------------------------------------------"
	@echo "Running the program..."
	@echo "================================================================================="
	./hw1_emreoytun
	@echo "================================================================================="
	@echo "Program exited...."

clean:
	@echo "-------------------------------------------"
	@echo "Removing files..."
	@rm -f *.o
	@rm -f hw1_emreoytun
	@rm -f *.txt
