.PHONY: clean

main.pdf: main.tex
	pdflatex main
	biber main
	pdflatex main
	pdflatex main

clean:
	rm -f *.aux *.bbl *.bcf *.blg *.log *.out *.pdf *.run.xml
