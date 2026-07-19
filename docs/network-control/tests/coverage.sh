#!/bin/sh
# Measure line coverage of pure/probe network units (gtest network-backend-test).
# Requires: meson, ninja, llvm-cov (or gcov), -Db_coverage=true
#
# Usage:
#   docs/network-control/tests/coverage.sh
#   COVERAGE_BUILD=build-coverage docs/network-control/tests/coverage.sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"
BUILD="${COVERAGE_BUILD:-build-coverage}"

if [ ! -d "$BUILD" ]; then
  meson setup "$BUILD" -Db_coverage=true --buildtype=debug
else
  meson configure "$BUILD" -Db_coverage=true --buildtype=debug >/dev/null 2>&1 || true
fi

# Stale .gcda after recompiles confuse clang
find "$BUILD" -name '*.gcda' -delete 2>/dev/null || true

ninja -C "$BUILD" tests/network-backend-test
"$BUILD/tests/network-backend-test"

GCOV="llvm-cov gcov"
command -v llvm-cov >/dev/null 2>&1 || GCOV=gcov

cd "$BUILD"
rm -f network-*.gcov freebsd-network.cpp.gcov network-backend-factory.cpp.gcov 2>/dev/null || true
for o in \
  src/util/libutil.a.p/network_network-types.cpp.o \
  src/util/libutil.a.p/network_network-info.cpp.o \
  src/util/libutil.a.p/network_freebsd-network.cpp.o \
  src/util/libutil.a.p/network_network-backend-factory.cpp.o
do
  [ -f "$o" ] || continue
  # shellcheck disable=SC2086
  $GCOV -b -o src/util/libutil.a.p "$o" >/dev/null 2>&1 || true
done

echo
echo "######## Line coverage: network pure/probe units ########"
printf '%-40s %6s %6s %6s\n' "FILE" "HIT" "MISS" "PCT"
total_hit=0
total_miss=0
for g in \
  network-types.cpp.gcov \
  network-info.cpp.gcov \
  freebsd-network.cpp.gcov \
  network-backend-factory.cpp.gcov
do
  if [ ! -f "$g" ]; then
    printf '%-40s %s\n' "$g" "(missing — run from coverage build)"
    continue
  fi
  hit=$(grep -cE '^[ ]*[0-9]+:' "$g" || true)
  miss=$(grep -cE '^[ ]*#####:' "$g" || true)
  tot=$((hit + miss))
  pct=0
  [ "$tot" -gt 0 ] && pct=$((hit * 100 / tot))
  printf '%-40s %6d %6d %5d%%\n' "$g" "$hit" "$miss" "$pct"
  total_hit=$((total_hit + hit))
  total_miss=$((total_miss + miss))
done
tot=$((total_hit + total_miss))
pct=0
[ "$tot" -gt 0 ] && pct=$((total_hit * 100 / tot))
echo "-----------------------------------------------"
printf '%-40s %6d %6d %5d%%\n' "TOTAL" "$total_hit" "$total_miss" "$pct"
echo
echo "Notes:"
echo "  • Scope: network-types, network-info, freebsd-network, backend-factory."
echo "  • NOT in unit coverage: network-widget.cpp, manager.cpp GTK/NM D-Bus,"
echo "    panel/widgets/network.cpp tray chrome (integration)."
echo "  • Residual misses: getifaddrs error path, fork/popen failures, euid==0"
echo "    root branch when tests run as non-root."
echo
echo "Run suite: meson test -C $BUILD --suite unit"
