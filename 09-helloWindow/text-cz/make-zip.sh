#!/bin/sh

NAME="$(basename "$(dirname "`pwd`")")"
echo "Testing and packing \"$NAME\" example..."

# remove old files
if test -f $NAME.zip; then
	rm $NAME.zip
fi
if test -f $NAME-text.zip; then
	rm $NAME-text.zip
fi

# zip text and images
zip $NAME-text.zip text.html window-Win32.png window-Xlib.png

# create tmp environment
mkdir tmp
cp ../main-Win32.cpp ../main-Xlib.cpp ../main-Wayland.cpp ../FindWayland.cmake tmp/
echo "cmake_minimum_required(VERSION 3.10.2)" > tmp/CMakeLists.txt
echo >> tmp/CMakeLists.txt
cat < ../CMakeLists.txt >> tmp/CMakeLists.txt
cd tmp

# zip code
zip $NAME.zip main-Win32.cpp main-Xlib.cpp main-Wayland.cpp FindWayland.cmake CMakeLists.txt
mv $NAME.zip ..

# compile and run
cmake .
make
if test -f ./$NAME-Win32; then
	./$NAME-Win32
fi
if test -f ./$NAME-Xlib; then
	./$NAME-Xlib
fi
if test -f ./$NAME-Wayland; then
	./$NAME-Wayland
fi
cd ..

# remove tmp
#rm -r tmp
