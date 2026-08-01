#!/bin/sh

set -e

here=$(cd "${0%/*}" && echo "$PWD")
cd "$here"

data_dir=$here/data
bin_dir=$here/bin
mkdir -p "$bin_dir"

build() {
	out=$1
	src=$2
	pkg=$3
	echo "---------------------------------------------------------------"
	echo "Building $out ... "

	cc -O2 -Wall -Wextra -std=c11 -DAPP_DATA_DIR="\"$data_dir\"" \
		$(pkg-config --cflags "$pkg") "$src" \
		$(pkg-config --libs "$pkg") \
		-o "$bin_dir/$out"
}

build gtk3-image-viewer  gtk3-app/src/main.c  gtk+-3.0
build gtk4-image-viewer  gtk4-app/src/main.c  gtk4

echo "built into $bin_dir:"
ls -1 "$bin_dir"

echo "---------------------------------------------------------------"
echo "testing..."
set -- "$bin_dir"/*
for b do
	echo "---------------------------------------------------------------"
	"$b"
	sleep 0.5
done
echo "---------------------------------------------------------------"
