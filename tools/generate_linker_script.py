#!/usr/bin/env python3
"""Genera un linker script autocontenido a partir del árbol memmap del Pico SDK.

Motivación (ver docs/DECISIONES.md, decisión "Linker script autocontenido"):
el driver g++ de GCC (toolchain ARM) en Windows DESCARTa las flags ``-L``
(``-Wl,-L``, ``-Xlinker -L`` o ``@rsp``) cuando los objetos se pasan mediante un
fichero de respuesta ``@rsp`` (el que genera Ninja para enlazar cientos de
objetos). ld usa esas ``-L`` únicamente para resolver las directivas
``INCLUDE "..."`` del linker script, por lo que el enlace falla con
``cannot open linker script file memmap_default.incl``.

En Linux el problema no aparece (g++ conserva ``-L``), pero para que el build
sea reproducible en Windows generamos un script donde todas las directivas
``INCLUDE "..."`` quedan resueltas e insertadas en línea. El script resultante
se pasa a ld vía ``-T`` (ruta absoluta), que no depende de ``-L``.

Uso (invocado desde CMake en tiempo de configuración):

    python tools/generate_linker_script.py \
        --entry  <ruta a memmap_default.ld> \
        --search <dir1> <dir2> ... \
        --out    <ruta del .ld generado>

``--search`` son los directorios donde se buscan los ``INCLUDE``, EN ORDEN
(el primero con el fichero gana), replicando el orden de ``-L`` de ld.
"""

import argparse
import os
import re
import sys

# INCLUDE "fichero" (el SDK siempre usa comillas dobles).
_INCLUDE_RE = re.compile(r'INCLUDE\s+"([^"]+)"')


def resolve(filename, search_dirs):
    """Devuelve la ruta absoluta del fichero o None si no está en search_dirs."""
    for directory in search_dirs:
        candidate = os.path.join(directory, filename)
        if os.path.isfile(candidate):
            return candidate
    return None


def expand(path, search_dirs, stack):
    """Lee `path` e inserta en línea, recursivamente, todos sus INCLUDE.

    Solo se resuelven los INCLUDE que están FUERA de comentarios ``/* ... */``.
    Los ``INCLUDE`` que aparecen dentro de un comentario (p. ej. el ejemplo de
    memory_extra.incl) se copian tal cual, igual que hace ld.
    """
    if path in stack:
        chain = " -> ".join(stack + [path])
        raise SystemExit(f"INCLUDE circular detectado: {chain}")

    try:
        with open(path, "r", encoding="utf-8") as handle:
            content = handle.read()
    except OSError as exc:
        raise SystemExit(f"No se pudo leer {path}: {exc}")

    out = []
    i = 0
    n = len(content)
    while i < n:
        # Bloque de comentario: se copia íntegro (los INCLUDE de dentro no se resuelven).
        if content.startswith("/*", i):
            end = content.find("*/", i + 2)
            if end == -1:
                out.append(content[i:])
                break
            out.append(content[i:end + 2])
            i = end + 2
            continue

        match = _INCLUDE_RE.match(content, i)
        if match:
            # Evita falsos positivos tipo "XINCLUDE \"...\"".
            if i == 0 or not (content[i - 1].isalnum() or content[i - 1] == "_"):
                name = match.group(1)
                target = resolve(name, search_dirs)
                if target is None:
                    raise SystemExit(
                        f"No se pudo resolver INCLUDE \"{name}\" (requerido desde {path}); "
                        f"buscado en: {', '.join(search_dirs)}"
                    )
                out.append(expand(target, search_dirs, stack + [path]))
                i = match.end()
                continue

        out.append(content[i])
        i += 1

    return "".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--entry", required=True, help="Linker script raíz (memmap_default.ld).")
    parser.add_argument("--out", required=True, help="Ruta del .ld autocontenido a escribir.")
    parser.add_argument(
        "--search",
        required=True,
        help="Directorios de búsqueda de INCLUDE separados por '|' (en orden de prioridad).",
    )
    args = parser.parse_args()

    search_dirs = [os.path.abspath(d) for d in args.search.split("|") if d]
    entry = os.path.abspath(args.entry)
    if not os.path.isfile(entry):
        raise SystemExit(f"Script raíz no encontrado: {entry}")

    result = expand(entry, search_dirs, [])

    out = os.path.abspath(args.out)
    out_dir = os.path.dirname(out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    header = (
        "/* Linker script autocontenido generado por tools/generate_linker_script.py.\n"
        " * No editar a mano: se regenera en cada configuración de CMake.\n"
        " * Todas las directivas INCLUDE del árbol memmap del SDK están insertadas en línea\n"
        " * para que ld no dependa de las flags -L (descartadas por g++ en Windows).\n"
        " */\n"
    )
    with open(out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(header)
        handle.write(result)

    print(f"generate_linker_script: {out} ({len(result)} bytes, entry={entry})")


if __name__ == "__main__":
    main()
