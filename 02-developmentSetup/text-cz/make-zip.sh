#!/bin/sh

NAME="$(basename "$(dirname "`pwd`")")"
echo "Packing \"$NAME\" example..."
if test -f $NAME.zip; then
	rm $NAME.zip
fi
zip $NAME.zip text.html cmake-1-empty.png cmake-2-specifyGenerator.png cmake-3-configured.png msvc-1-solution.png msvc-2-setAsStartUpProject.png msvc-3-run.png
