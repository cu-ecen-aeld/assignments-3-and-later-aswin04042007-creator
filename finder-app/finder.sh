#!/bin/sh

if [ $# -lt 2 ]
then
    echo "Error: Two arguments required."
    exit 1
fi

if [ ! -d "$1" ]
then
    echo "Error: Directory does not exist."
    exit 1
fi

X=$(find "$1" -type f | wc -l)
Y=$(grep -r "$2" "$1" | wc -l)

echo "The number of files are $X and the number of matching lines are $Y"
