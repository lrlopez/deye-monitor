# Manual de usuario

Fuente del manual de usuario de Deye Monitor, escrito en Markdown y publicado en tres formatos: una
**web** navegable (GitHub Pages), un **PDF** y un **ZIP** con la web para consultarla sin conexión.
Estructura y herramientas reutilizadas del manual de
[ÁTICA Calidad](https://github.com/reasol-edu/atica-calidad). Todos los comandos de esta página se
ejecutan desde la raíz del repositorio con `make`.

## Ficheros

- `index.md` y `01-*.md` a `13-*.md` — los capítulos, en el orden en que aparecen en el PDF y en el
  índice de la web. El orden va de lo que ve la persona usuaria (pantallas, web, avisos) a lo técnico
  (instalación del firmware, al final).
- `mkdocs.yml` — configuración de MkDocs Material (navegación, tema, exclusiones).
- `requirements.txt` — dependencias de Python para generar la web.
- `assets/` — hojas de estilo (`theme.css`, `print.css`) compartidas por el PDF y la web, y el logotipo.
- `img/` — capturas de pantalla referenciadas desde los capítulos (ver
  [Regenerar las capturas](#regenerar-las-capturas)).
- `deye-monitor-manual.pdf` y `_build.html` — salidas generadas por `make docs-pdf`; no se versionan ni se editan.

Los filtros de Pandoc que adaptan el Markdown al PDF (`pandoc-admonitions.lua`,
`pandoc-internal-links.lua`) están en `docs/`.

## Generar el PDF

```bash
make docs-pdf
```

Genera `docs/manual/deye-monitor-manual.pdf` con [pandoc](https://pandoc.org) (Markdown → HTML,
fichero intermedio `_build.html`) y [`pagedjs-cli`](https://pagedjs.org) (vía `npx`, sobre un
Chrome/Chromium local) para maquetarlo e imprimirlo con el mismo tema que la web. Requiere `pandoc` y
Node.js. La versión y la fecha de la portada salen de la última etiqueta de git; se pueden fijar con
`make docs-pdf VERSION=3.7.0 PUB_DATE=01/11/2026`.

## Generar y previsualizar la web

```bash
pip install -r docs/manual/requirements.txt
make docs-web      # genera docs/manual-site/
make docs-serve    # sirve el manual en http://127.0.0.1:8000 con recarga en caliente
```

`make docs` genera el PDF y la web de una vez.

## Publicación automática

La acción de GitHub `.github/workflows/docs.yml` genera los tres formatos:

- en **cada etiqueta de versión** (`v*`): adjunta a la *release* el PDF
  (`deye-monitor-manual-<versión>.pdf`) y el ZIP de la web (`deye-monitor-manual-web-<versión>.zip`), y
  publica la web en GitHub Pages si esa es la **última versión estable** (las prereleases no se
  publican);
- en **pull requests y cambios de `docs/`**: solo comprueba que el manual se genera correctamente;
- **a mano** (*Run workflow*): genera los ficheros como artefactos y, opcionalmente, publica la web.

!!! note "Activar GitHub Pages"
    Una sola vez, en el repositorio: *Settings → Pages → Build and deployment → Source:
    **GitHub Actions***.

## Regenerar las capturas

Las capturas de `img/` se generan **sin hardware**, con datos de ejemplo, con los scripts de
`tools/screenshots/` (pantalla táctil en un simulador del propio firmware, y web fotografiando las
páginas reales con Chrome). Instrucciones y limitaciones en `tools/screenshots/README.md`:

```bash
pio run -e sunton_4827s043            # una vez, para descargar LVGL
tools/screenshots/capture-device.sh   # pantalla táctil
tools/screenshots/capture-web.sh      # web
```

## Cómo mantener el manual

- Cuando cambie una pantalla, un ajuste o un comando, actualiza el capítulo correspondiente **en el
  mismo cambio** y regenera las capturas afectadas.
- Los enlaces entre capítulos se escriben como `fichero.md#ancla`: funcionan igual en la web y en el
  PDF. Un encabezado con ancla propia se escribe `## Título {#mi-ancla}`.
- Los recuadros usan la sintaxis `!!! note "Título"` (también `attention`, `important`…), con el
  contenido sangrado cuatro espacios.
