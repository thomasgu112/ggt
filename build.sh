#!/bin/bash

clear
export PREFIX=/tmp

SRC_DIR=$(pwd)
BUILD_DIR=${SRC_DIR}/obj
mkdir -p $BUILD_DIR && cd $BUILD_DIR && meson -Dprefix=$PREFIX --buildtype=release $SRC_DIR && ninja

cd ..
cp obj/ggt.so ~/.local/share/gegl-0.4/plug-ins
pkill -9 gimp
printf "\n=== PROGRAM START ===\n\n"
#gdb --args gimp /tmp/jelly.png
gimp /tmp/wire.png
