# 066 — Fast bootstrap: the seed is the build's units, adopted by gamma0 (s70)

User: "try and speed up bootstrap times". Measured on 4 cpus (gcc 13), build.sh:
- before: 141 s = seed 33 (8 units of 5.5 MB at -O1) + stage 1 55 + stage 2 48.
  The seed predated 065, so gamma0 neither cached units nor pooled cc: stage 2 recompiled all.
- seed refreshed only (old format): 88 s = 35 + 48 + 5 (stage 2 = all cache hits).
- final: 52 s = seed 42 (387 by-file units at -O2, xargs -P, largest first) + 5 + 5.

## Design
- Seed files = exactly the texts a split build of the compiler compiles (unit_sources +
  runtime_source in driver/native.gm), named `<first 16 hex of sha256(text)>.c`.
  `build --emit-c` with GAMMA_C_UNITS>1 writes them (exe.rt.c, exe.c, exe.N.c; the old
  shared-header form is gone); refresh-seed.sh / verify.sh rename by hash (sha256sum|shasum).
- build.sh compiles each with BASE_CFLAGS -pthread -O2 -Werror=implicit-function-declaration
  + $CFLAGS (must equal the driver's flags for a Checked split build; a mismatch is not
  detected, only means gamma is built with build.sh's flags) into $out/seed/<h>.o, links gamma0.
- `GAMMA_UNIT_OBJECTS=<dir>`: on a cache miss, runtime_object / unit_objects copy
  `<dir>/<sha16(text)>.o` into the cache instead of compiling (rtcache.adopt_object; not
  with -g, whose text names the cache path). Adopted units leave no `.c` in the cache
  (the selfhost test uses that to see nothing was compiled).
- build.sh uses relative GAMMA_STD=lib/std GAMMA_RUNTIME=runtime (paths are in the C; the
  seed is generated with relative ones); tools/test.sh rebuild does too (same cache).

## Consequences
- Bootstrap time now depends on seed freshness: per changed source file one unit compiled
  again; a cgen change that touches every unit → ~42 + 48 + 5 (or + 48 if also not a
  fixed point yet). Break-even vs the old -O1 seed at ~20 % hits. Docs say: refresh when slow.
- Seed is 74 MB / 390 files (was 53 MB / 10).

## Rejected / measured
- Seed at -O0: compiles in 17 s instead of 35 but gamma0's front end takes 25 s instead of 5.
- Stage 1 in debug mode: 30 s instead of 48 but stage 2 then never hits the cache.
- BSD xargs -I truncates replacements at 255 bytes: build.sh uses `-n 1 sh -c` + env vars.
