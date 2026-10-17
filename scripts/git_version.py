Import("env")
import subprocess

# Fija APP_VERSION a partir de `git describe` (DEYE-66), para que lo que muestra el
# firmware (splash, /api/status, comprobación de actualizaciones) coincida con el
# commit compilado y no dependa de acordarse de editar config.h:
#
#   build sobre la etiqueta v3.7.0            -> "3.7.0"
#   24 commits después de v3.6.0              -> "3.6.0+24.g1a2b3c4"
#   con cambios sin commitear                 -> "3.6.0+24.g1a2b3c4.dirty"
#
# El sufijo "+..." es metadato de compilación semver: fw_parse_version() lo ignora, así
# que una build de desarrollo posterior a v3.6.0 NO se ofrece como actualizable a v3.6.0.
# Si git no está disponible o no hay etiquetas, no se define nada y vale el valor por
# defecto de src/config.h.

def describe():
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--match", "v[0-9]*", "--long", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"), capture_output=True, text=True, timeout=10)
    except Exception:
        return None
    if out.returncode != 0:
        return None
    desc = out.stdout.strip()                      # v3.6.0-24-g1a2b3c4[-dirty]
    dirty = desc.endswith("-dirty")
    if dirty:
        desc = desc[:-len("-dirty")]
    parts = desc.rsplit("-", 2)
    if len(parts) != 3:
        return None
    tag, ahead, sha = parts
    ver = tag[1:] if tag.startswith("v") else tag
    if ahead == "0" and not dirty:
        return ver
    return "%s+%s.%s%s" % (ver, ahead, sha, ".dirty" if dirty else "")

version = describe()
if version:
    print("APP_VERSION desde git: " + version)
    env.Append(CPPDEFINES=[("APP_VERSION", env.StringifyMacro(version))])
