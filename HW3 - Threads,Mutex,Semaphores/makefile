all: clean compile run

compile: hw3.c
	@echo "-------------------------------------------"
	@echo "Compiling all files..."
	@gcc -o hw3_emreoytun hw3.c -pthread
run:
	@echo "-------------------------------------------"
	@echo "Running the program..."
	@echo "================================================================================="
	./hw3_emreoytun
	@echo "================================================================================="
	@echo "Program exited...."

clean:
	@echo "-------------------------------------------"
	@echo "Removing files..."
	@rm -f *.o
	@rm -f hw3_emreoytun
