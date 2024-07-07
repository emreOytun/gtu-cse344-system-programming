all: clean compile run

compile: hw2.c
	@echo "-------------------------------------------"
	@echo "Compiling all files..."
	@gcc -o hw2_emreoytun hw2.c
run:
	@echo "-------------------------------------------"
	@echo "Running the program..."
	@echo "================================================================================="
	./hw2_emreoytun 5
	@echo "================================================================================="
	@echo "Program exited...."

clean:
	@echo "-------------------------------------------"
	@echo "Removing files..."
	@rm -f *.o
	@rm -f hw2_emreoytun
