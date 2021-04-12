#!/bin/bash

echo
echo "-----------------------------"
echo "Welcome to the Peek installer"
echo "-----------------------------"

PROGNAME=$(basename $0)

handle_error () {
	echo "${PROGNAME}: ${1:-"Unknown Error"}. Exiting." 1>&2
	exit 1
}

INSTALL_PATH="/media/fat/peek"
VERSION_PATH="${INSTALL_PATH}/VERSION"

echo
echo "This will install (or upgrade) at: ${INSTALL_PATH}"
read -r -p "-- Is this OK? [Y/n] " response
if [[ ! "$response" =~ ^(|[yY]|[yY][eE][sS])$ ]]; then
    echo "Exiting the installer per user request."
    exit 0
fi

echo
echo -n "Checking for installed version... "
if [[ -d "${INSTALL_PATH}" ]]; then
    if [[ -f "${VERSION_PATH}" ]]; then
        INSTALLED=$(cat "${VERSION_PATH}")
    else
        INSTALLED="unknown"
    fi
else
    INSTALLED="not installed"
fi
echo "${INSTALLED}"

echo -n "Checking for latest version... "
VERSION_URL="https://api.github.com/repos/mrsonicblue/peek/releases/latest"
LATEST=$(wget -q -O - "${VERSION_URL}" | jq -r .tag_name)
if [[ "${LATEST}" = "" ]]; then
    handle_error "${LINENO}: Unable to get latest version from ${VERSIONURL}. Check internet connection."
fi
echo "${LATEST}"

echo -n "Stopping any existing services... "
PID=$(pidof peek)
if [[ "${PID}" != "" ]]; then
    kill "${PID}" || handle_error "${LINENO}: Unable to stop peek at PID: ${PID}"
fi
echo "OK"

echo -n "Ensure install directory... "
mkdir -p "${INSTALL_PATH}" || handle_error "${LINENO}: Unable to create install path: ${INSTALL_PATH}. Check filesystem permissions."
echo "OK"

echo -n "Downloading... "
DOWNLOAD_URL="https://github.com/mrsonicblue/peek/releases/download/${LATEST}/peek-${LATEST}.tgz"
wget -q -O - "$DOWNLOAD_URL" | tar xfz - --strip-components=1 -C "${INSTALL_PATH}" || handle_error "${LINENO}: Unable to download and install from: ${DOWNLOAD_URL} to ${INSTALL_PATH}. Check internet connection and filesystem permissions."
echo "OK"

echo -n "Linking scanner... "
if [[ ! -f "${INSTALL_PATH}/scan" ]]; then
    ln -s "${INSTALL_PATH}/scan-lib/Peek.Scan" "${INSTALL_PATH}/scan" || handle_error "${LINENO}: Unable to create symbolic link from ${INSTALL_PATH}/scan-lib/Peek.Scan to ${INSTALL_PATH}/scan. Check file path and permissions."
fi
echo "OK"

echo -n "Copying service script... "
cp "${INSTALL_PATH}/S99peek" "/etc/init.d/S99peek" || handle_error "${LINENO}: Unable to copy service script from ${INSTALL_PATH}/S99peek to /etc/init.d/S99peek. Check file path and permissions."
echo "OK"

echo -n "Starting service... "
/etc/init.d/S99peek start > /dev/null 2>&1 || handle_error "${LINENO}: Unable to start service."
echo "OK"

echo -n "Writing version... "
echo "${LATEST}" > "${VERSION_PATH}" || handle_error "${LINENO}: Unable to write version ${LATEST} to path: ${VERSION_PATH}."
echo "OK"

echo
echo "Installation has completed successfully. If this is a new install, you must scan your"
echo "ROM files to load metadata into the filter database. A scan application is included to"
echo "make this easy. You can run it the following command:"
echo
echo "       ${INSTALL_PATH}/scan"
echo
