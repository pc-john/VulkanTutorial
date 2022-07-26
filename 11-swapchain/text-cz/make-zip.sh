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
zip $NAME-text.zip text.html window-Wayland.png

# create tmp environment
mkdir tmp
cp ../main.cpp ../VulkanWindow.h ../VulkanWindow.cpp ../FindWayland.cmake ../GuiMacros.cmake tmp/
echo "cmake_minimum_required(VERSION 3.10.2)" > tmp/CMakeLists.txt
echo >> tmp/CMakeLists.txt
cat < ../CMakeLists.txt >> tmp/CMakeLists.txt
cd tmp

# zip code
zip $NAME.zip main.cpp VulkanWindow.h VulkanWindow.cpp FindWayland.cmake GuiMacros.cmake CMakeLists.txt
mv $NAME.zip ..

# compile and run
cmake .
make
./$NAME
cd ..

# remove tmp
#rm -r tmp
