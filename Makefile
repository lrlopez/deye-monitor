.PHONY: docs docs-pdf docs-web docs-serve docs-clean test

# Versión y fecha que aparecen en la portada del manual y en el pie de la web. Por defecto salen de
# git (última etiqueta alcanzable y fecha de su commit); se pueden forzar con
#   make docs VERSION=3.7.0 PUB_DATE=01/11/2026
# (es lo que hace la acción de GitHub de la documentación al compilar una etiqueta).
VERSION  ?= $(shell git describe --tags --always 2>/dev/null | sed -E 's/^v//')
PUB_DATE ?= $(shell git log -1 --format=%cd --date=format:%d/%m/%Y 2>/dev/null)

## Tests de lógica pura en el PC (sin hardware).
test:
	pio test -e native

## Genera el manual completo: PDF (docs/manual/deye-monitor-manual.pdf) y web (docs/manual-site/).
docs: docs-pdf docs-web

## Genera el manual en PDF.
##
## Usa pandoc (Markdown -> HTML) y "npx pagedjs-cli" (Chromium headless) para imprimir el HTML con
## el tema CSS. El HTML intermedio (_build.html) se genera en docs/manual/ para que las rutas
## relativas a assets/ e img/ resuelvan igual que en la web.
##
## pagedjs-cli usa Puppeteer; se reutiliza el Chrome del sistema vía PUPPETEER_EXECUTABLE_PATH para
## no descargar un Chromium aparte. Si no se encuentra, instala uno con
## "npx puppeteer browsers install chrome".
docs-pdf:
	@command -v pandoc >/dev/null 2>&1 || { echo "Necesitas pandoc. Instálalo (p. ej. brew install pandoc) y reintenta."; exit 1; }
	@command -v npx >/dev/null 2>&1 || { echo "Necesitas Node.js/npx para generar el PDF. Instala Node y reintenta."; exit 1; }
	pandoc -s -f markdown-auto_identifiers --toc --toc-depth=2 \
		--lua-filter=docs/pandoc-admonitions.lua \
		--lua-filter=docs/pandoc-internal-links.lua \
		--metadata title="Manual de usuario de Deye Monitor" \
		--metadata subtitle="Monitor de instalación solar con inversor Deye SUN-6K-SG05" \
		--metadata date="Versión $(VERSION) · $(PUB_DATE)" \
		--metadata lang=es \
		-c assets/theme.css -c assets/print.css \
		-o docs/manual/_build.html \
		docs/manual/index.md docs/manual/[0-9][0-9]-*.md
	cd docs/manual && CHROME="$${PUPPETEER_EXECUTABLE_PATH:-$$(for c in \
		"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
		"/Applications/Chromium.app/Contents/MacOS/Chromium" \
		"$$(command -v google-chrome)" "$$(command -v chromium)" "$$(command -v chromium-browser)"; do \
		[ -x "$$c" ] && echo "$$c" && break; done)}"; \
		PUPPETEER_EXECUTABLE_PATH="$$CHROME" npx --yes pagedjs-cli _build.html -o deye-monitor-manual.pdf

## Construye la web del manual (docs/manual-site/) con MkDocs Material.
## Requiere: pip install -r docs/manual/requirements.txt
docs-web:
	@command -v mkdocs >/dev/null 2>&1 || { echo "Necesitas MkDocs Material: pip install -r docs/manual/requirements.txt"; exit 1; }
	MANUAL_COPYRIGHT="Deye Monitor · versión $(VERSION) · $(PUB_DATE)" mkdocs build -f docs/manual/mkdocs.yml

## Previsualiza la web del manual en local (http://127.0.0.1:8000) con recarga en caliente.
docs-serve:
	@command -v mkdocs >/dev/null 2>&1 || { echo "Necesitas MkDocs Material: pip install -r docs/manual/requirements.txt"; exit 1; }
	MANUAL_COPYRIGHT="Deye Monitor · versión $(VERSION) · $(PUB_DATE)" mkdocs serve -f docs/manual/mkdocs.yml

## Borra lo que generan los targets de documentación.
docs-clean:
	rm -rf docs/manual-site docs/manual/_build.html docs/manual/deye-monitor-manual.pdf
