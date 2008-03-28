#!/bin/bash

version=0.1.0
baseurl="http://libkate.googlecode.com/files/libkate-$version.tar.gz"

which wget >& /dev/null
if [ $? -eq 0 ]
then
  wget "$baseurl"
else
  which curl >& /dev/null
  if [ $? -eq 0 ]
  then
    curl "$baseurl"
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
ln -fs "libkate-$version" libkate
cd libkate && make staticlib

