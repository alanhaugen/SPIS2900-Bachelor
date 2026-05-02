.PHONY: clean

main.pdf: main.tex references.bib
	pdflatex main
	biber main
	makeglossaries main
	pdflatex main
	pdflatex main

clean:
	rm -f *.aux *.bbl *.bcf *.blg *.log *.out *.pdf *.run.xml
