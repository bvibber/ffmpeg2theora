#!/bin/bash

version=0.4.1
baseurl="http://libkate.googlecode.com/files/libkate-$version.tar.gz"

which wget >& /dev/null
if [ $? -eq 0 ]
then
  test -e "libkate-$version.tar.gz" || wget "$baseurl"
else
  which curl >& /dev/null
  if [ $? -eq 0 ]
  then
    test -e "libkate-$version.tar.gz" || curl "$baseurl" -o "libkate-$version.tar.gz"
  else
    echo "Neither wget nor curl were found, cannot download libkate"
    exit 1
  fi
fi

if [ $? -ne 0 ]
then
  echo "Failed to download libkate"
  exit 1
fi

tar xfz "libkate-$version.tar.gz"
test -L libkate && rm libkate
ln -fs "libkate-$version" libkate
cd libkate && ./configure && make && cd lib

