#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds the SEN core HPKG from bin/sen_server (run ./build.sh first).
# The package is written to the repository root; set REVISION to override the revision (default 1).
set -e
cd "$(dirname "$0")"

APP=bin/sen_server
RDEF=src/resources/sen_server.rdef
[ -f "$APP" ] || { echo "$APP not found, run ./build.sh first" >&2; exit 1; }

# version from the app_version resource, a development build becomes a pre-release, a beta is 1.0.0~beta1
major=$(sed -n 's/^[[:space:]]*major[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
middle=$(sed -n 's/^[[:space:]]*middle[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
minor=$(sed -n 's/^[[:space:]]*minor[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
VERSION="$major.$middle.$minor"
# SEN_VERSION_SUFFIX replaces dev for a preview (SEN_VERSION_SUFFIX=preview1: 1.0.0~preview1)
grep -q 'variety[[:space:]]*=[[:space:]]*B_APPV_DEVELOPMENT' $RDEF && VERSION="$VERSION~${SEN_VERSION_SUFFIX:-dev}"
if grep -q 'variety[[:space:]]*=[[:space:]]*B_APPV_BETA' $RDEF; then
	internal=$(sed -n 's/^[[:space:]]*internal[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
	VERSION="$VERSION~beta${internal:-1}"
fi
VERSION="$VERSION-${REVISION:-1}"
ARCH=$(getarch)

STAGE=package-build
rm -rf $STAGE
mkdir -p $STAGE/bin $STAGE/data/user_launch $STAGE/data/sen-core $STAGE/documentation/packages/sen_core

# the server, and the command "sen" for its options (it starts the server by its path in the package)
cp -a $APP $STAGE/bin/
./create-sen-wrapper.sh $STAGE/bin/sen_server $STAGE/bin /boot/system/bin/sen_server >/dev/null

# the server is started with the session, like the mail server (it waits for the Deskbar to put its item there)
cp -a package/user_launch $STAGE/data/user_launch/sen_server

# the script that repairs the indices of a system (the server and the ontology installer create them, too)
cp -a create-sen-indices.sh $STAGE/data/sen-core/

# the documentation: the page that the About window opens for donations, and the texts
cp -a docs/support.html README.md LICENSE $STAGE/documentation/packages/sen_core/

sed -e "s|@VERSION@|$VERSION|g" -e "s|@ARCH@|$ARCH|g" package/PackageInfo.in > $STAGE/.PackageInfo

PACKAGE=sen_core-$VERSION-$ARCH.hpkg
rm -f $PACKAGE
package create -C $STAGE $PACKAGE
rm -rf $STAGE
echo "created $PACKAGE"
