#!/bin/bash
cd `dirname $0`
version='0.29'
test -e .git && git=`which git`
if [ "x$git" != "x" ]; then
    version=$(cd "$1" && git describe --tags 2> /dev/null)
fi
echo $version
