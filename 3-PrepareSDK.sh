#!/bin/bash

ROOT=$(pwd)
SELF=$(dirname "$0")
SDK=_NRI_SDK

echo ${SDK}: ROOT=${ROOT}, SELF=${SELF}

rm -rf "${SDK}"

mkdir -p "${SDK}/Include/Extensions"
mkdir -p "${SDK}/Lib"

cp -r "${SELF}/Include/." "${SDK}/Include"
cp -r "${SELF}/Include/Extensions/." "${SDK}/Include/Extensions"
cp "${SELF}/LICENSE.txt" "${SDK}"
cp "${SELF}/README.md" "${SDK}"
cp "${SELF}/nri.natvis" "${SDK}"

if [ "$(uname -s)" = "Darwin" ]; then
    cp -H "${ROOT}/_Bin/libNRI.dylib" "${SDK}/Lib"

    if [ -f "${ROOT}/_Bin/libmetalirconverter.dylib" ]; then
        cp -H "${ROOT}/_Bin/libmetalirconverter.dylib" "${SDK}/Lib"
        echo "${SDK}: 'libmetalirconverter.dylib' is distributed under Apple's Metal Shader Converter license terms (see README.md)"
    fi
else
    cp -H "${ROOT}/_Bin/libNRI.so" "${SDK}/Lib"
fi
