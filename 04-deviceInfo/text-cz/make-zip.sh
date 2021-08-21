#!/bin/sh

NAME="$(basename "$(dirname "`pwd`")")"
echo "Testing and packing \"$NAME\" example..."
if test -f $NAME.zip; then
	rm $NAME.zip
fi
if test -f $NAME-text.zip; then
	rm $NAME-text.zip
fi
zip $NAME-text.zip text.html
mkdir tmp
cp ../main.cpp tmp/
echo "cmake_minimum_required(VERSION 3.10.2)" > tmp/CMakeLists.txt
echo >> tmp/CMakeLists.txt
cat < ../CMakeLists.txt >> tmp/CMakeLists.txt
cd tmp
zip $NAME.zip main.cpp CMakeLists.txt
mv $NAME.zip ..
cmake .
make
./$NAME
cd ..
#rm -r tmp
