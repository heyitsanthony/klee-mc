#!/bin/bash

libname="$1"
readelf -sW "$libname" | grep FUNC | awk ' { print $7 " " $8; } ' | grep -v UND | grep -v '@@'