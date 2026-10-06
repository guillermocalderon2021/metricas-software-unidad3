# Métricas de Software 2026

Material del curso publicado con MkDocs Material.

## Ejecución local

```bash
python -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
mkdocs serve
```

El sitio queda disponible, por defecto, en `http://127.0.0.1:8000/`.

## Validación

```bash
mkdocs build --strict
```

## Publicación

El flujo `.github/workflows/pages.yml` compila el sitio y lo publica mediante GitHub Pages cuando se realiza un `push` a `main`.

En el repositorio de GitHub debe seleccionarse **Settings → Pages → Source → GitHub Actions**.
