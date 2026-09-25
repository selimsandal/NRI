#!/bin/bash
set -eu

ROOT=$(pwd)
SELF=$(dirname "$0")
SDK=${1:-_NRI_SDK}

echo ${SDK}: ROOT=${ROOT}, SELF=${SELF}

rm -rf "${SDK}"

mkdir -p "${SDK}/Include/Extensions"
mkdir -p "${SDK}/Lib"

cp -R "${SELF}/Include/." "${SDK}/Include"
cp "${SELF}/LICENSE.txt" "${SDK}"
cp "${SELF}/README.md" "${SDK}"
cp "${SELF}/nri.natvis" "${SDK}"

case $(uname -s) in
    Darwin)
        if [ -f "${ROOT}/_Bin/libNRI.dylib" ]; then
            cp -L "${ROOT}/_Bin/libNRI.dylib" "${SDK}/Lib"
        elif [ -f "${ROOT}/_Bin/libNRI.a" ]; then
            cp -L "${ROOT}/_Bin/libNRI.a" "${SDK}/Lib"
            find "${ROOT}/_Build" -name 'libNRI_*.a' -type f -exec cp -p {} "${SDK}/Lib" \;
        else
            echo "NRI library not found in ${ROOT}/_Bin; build NRI first" >&2
            exit 1
        fi
        ;;
    *)
        cp -L "${ROOT}/_Bin/libNRI.so" "${SDK}/Lib"
        ;;
esac

# Metal Shader Converter is distributed under Apple's license. The SDK package
# deliberately does not copy its headers or dylib; applications using converted
# DXIL must install it separately.
