#!/bin/bash

PYTHON_LIB="/build/python3-install/lib/python3.5"

rm -rf release
mkdir -p release/scan-inc
cd release

cp -f ${PYTHON_LIB}/lib-dynload/_ssl.cpython-35m-arm-linux-gnueabihf.so scan-inc/
cp -f ${PYTHON_LIB}/lib-dynload/zlib.cpython-35m-arm-linux-gnueabihf.so scan-inc/
cp -f ${PYTHON_LIB}/site-packages/_sqlite3.cpython-35m-arm-linux-gnueabihf.so scan-inc/
cp -rf ${PYTHON_LIB}/sqlite3 scan-inc/
cp -rf ${PYTHON_LIB}/site-packages/{certifi,chardet,idna,requests,urllib3} scan-inc/
cp -f ../main.py scan-inc/

tar cfz scan-inc.tgz scan-inc