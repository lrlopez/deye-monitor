#!/bin/sh
# Copia las fuentes de pantallas que necesitan stubs de cabecera (se recopian en cada render)
S=$(cd "$(dirname "$0")" && pwd); P=$(cd "$S/../../.." && pwd)
cp $P/src/stats_screen.cpp $S/stats_copy.cpp
cp $P/src/chart_screen.cpp $S/chart_copy.cpp
cp $P/src/config_screen.cpp $S/config_copy.cpp
cp $P/src/energy_profile.cpp $S/profile_copy.cpp
