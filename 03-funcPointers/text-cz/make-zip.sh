#!/bin/sh

NAME="$(basename "$(dirname "`pwd`")")"
echo "Testing and packing \"$NAME\" example..."
if test -f $NAME.zip; then
	rm $NAME.zip
fi
mkdir tmp
cp ../main.cpp tmp/
echo "cmake_minimum_required(VERSION 3.10.2)" > tmp/CMakeLists.txt
echo >> tmp/CMakeLists.txt
cat < ../CMakeLists.txt >> tmp/CMakeLists.txt
cp text.html \
	architecture.svg architecture-trampoline.svg architecture-instance-and-device-level.svg architecture-dynamic-calls.svg \
	tmp/
cd tmp
zip $NAME.zip main.cpp CMakeLists.txt text.html	\
	architecture.svg architecture-trampoline.svg architecture-instance-and-device-level.svg architecture-dynamic-calls.svg
mv $NAME.zip ..
cmake .
make
./$NAME
cd ..
#rm -r tmp
