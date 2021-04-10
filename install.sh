#!/bin/bash

echo
echo "-----------------------------"
echo "Welcome to the Peek installer"
echo "-----------------------------"

handle_error () {
	echo "failed"
	exit 1
}

INSTALL_PATH="/media/fat/peek"
VERSION_PATH="${INSTALL_PATH}/VERSION"

echo
echo "This will install (or upgrade) at: ${INSTALL_PATH}"
read -r -p "-- Is this OK? [Y/n] " response
if [[ ! "$response" =~ ^(|[yY]|[yY][eE][sS])$ ]]; then
    echo "ok bye"
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
LATEST=$(wget -q -O - "https://api.github.com/repos/mrsonicblue/peek/releases/latest" | jq -r .tag_name)
if [[ "${LATEST}" = "" ]]; then
    handle_error
fi
echo "${LATEST}"

echo -n "Stopping any existing services... "
PID=$(pidof peek)
if [[ "${PID}" != "" ]]; then
    kill "${PID}" || handle_error
fi
echo "OK"

echo -n "Ensure install directory... "
mkdir -p "${INSTALL_PATH}" || handle_error
echo "OK"

echo -n "Downloading... "
DOWNLOAD_URL="https://github.com/mrsonicblue/peek/releases/download/${LATEST}/peek-${LATEST}.tgz"
wget -q -O - "$DOWNLOAD_URL" | tar xfz - --strip-components=1 -C "${INSTALL_PATH}" || handle_error
echo "OK"

echo -n "Linking scanner... "
if [[ ! -f "${INSTALL_PATH}/scan" ]]; then
    ln -s "${INSTALL_PATH}/scan-lib/Peek.Scan" "${INSTALL_PATH}/scan" || handle_error
fi
echo "OK"

echo -n "Copying service script... "
cp "${INSTALL_PATH}/S99peek" "/etc/init.d/S99peek" || handle_error
echo "OK"

echo -n "Starting service... "
/etc/init.d/S99peek start > /dev/null 2>&1 || handle_error
echo "OK"

echo -n "Writing version... "
echo "${LATEST}" > "${VERSION_PATH}" || handle_error
echo "OK"

echo
echo "All done!"