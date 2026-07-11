#!/bin/sh

if [ $# -lt 2 ]
then
    echo "Error: Two arguments required."
    exit 1
fi

mkdir -p "$(dirname "$1")"
echo "$2" > "$1"

if [ $? -ne 0 ]
then
    echo "Error: Failed to write to file."
    exit 1
fi
