#!/bin/sh

set -e

git submodule update --init --recursive
if [[ ! -z "${SRT_VERSION}" ]];then 
  cd submodule/srt
  git checkout "${SRT_VERSION}"
  cd ../..
fi
mkdir -p _build
cd _build
cmake ../ ${BUILD_OPTIONS}
cmake --build ./
