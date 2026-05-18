#!/usr/bin/env python3
"""
Lee los entornos de platformio.ini y los expone como output de GitHub Actions.
Ignora secciones sin nombre de entorno concreto (como [env] vacío).
Filtra entornos marcados como ci_skip = true.
"""

import configparser
import json
import sys
import os

def get_envs(ini_path: str = "platformio.ini") -> list[str]:
    if not os.path.exists(ini_path):
        print(f"ERROR: No se encontró {ini_path}", file=sys.stderr)
        sys.exit(1)

    cfg = configparser.ConfigParser()
    cfg.read(ini_path)

    envs = []
    for section in cfg.sections():
        # Solo secciones [env:nombre_concreto], no [env] sin nombre
        if not section.startswith("env:"):
            continue

        env_name = section[4:].strip()
        if not env_name:
            continue

        # Permitir marcar entornos para saltar en CI con:
        #   ci_skip = true   (custom option en platformio.ini)
        if cfg.get(section, "ci_skip", fallback="false").strip().lower() == "true":
            print(f"  Saltando {env_name} (ci_skip = true)", file=sys.stderr)
            continue

        envs.append(env_name)

    return envs


if __name__ == "__main__":
    envs = get_envs()

    if not envs:
        print("ERROR: No se encontraron entornos en platformio.ini", file=sys.stderr)
        sys.exit(1)

    print(f"Entornos detectados: {envs}", file=sys.stderr)

    # Output para GitHub Actions
    print(f"envs={json.dumps(envs)}")
    