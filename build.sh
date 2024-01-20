#!/bin/bash

export PREFIX=/tmp

SRC_DIR=$(pwd)
BUILD_DIR=${SRC_DIR}/obj
mkdir -p $BUILD_DIR && cd $BUILD_DIR && meson -Dprefix=$PREFIX --buildtype=release $SRC_DIR && ninja

cp ../obj/ggt.so ~/.local/share/gegl-0.4/plug-ins

gimp ~/frags/img/jellyfish/1018654.jpg 
