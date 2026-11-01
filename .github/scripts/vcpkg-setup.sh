#!/usr/bin/env bash
# vcpkg at $VCPKG_COMMIT (release.yml) in $RUNNER_TEMP/vcpkg, bootstrapped;
# exports VCPKG_ROOT for the next steps and creates the binary cache folder.
set -eu
root="$RUNNER_TEMP/vcpkg"
mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE" "$root"
cd "$root"
git init -q
git remote add origin https://github.com/microsoft/vcpkg
for attempt in 1 2 3; do
	git fetch -q --depth 1 origin "$VCPKG_COMMIT" && break
	echo "fetch attempt $attempt failed, retrying"
	sleep 10
done
git checkout -q FETCH_HEAD
if [ "$RUNNER_OS" = Windows ]; then ./bootstrap-vcpkg.bat -disableMetrics; else ./bootstrap-vcpkg.sh -disableMetrics; fi
echo "VCPKG_ROOT=$root" >> "$GITHUB_ENV"
