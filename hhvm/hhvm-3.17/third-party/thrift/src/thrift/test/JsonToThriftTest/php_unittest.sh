#!/bin/sh
set -e

THRIFT_COMPILER="$1"
if [ -z "$THRIFT_COMPILER" ]; then
  echo "Missing argument: path to thrift compiler must be specified" >&2
  exit 1
fi

WORK="$(mktemp -d -t thrift.test.JsonToThriftTest.php_unittest.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

SRC_ROOT="$WORK/src"
PACKAGE_ROOT="$WORK/packages"
HERE="$(pwd -L)"

mkdir -p "$WORK"
cp -r thrift/lib/hack/src/* "$WORK"
cp thrift/lib/php/src/Thrift.php "$WORK"

START_PATH=thrift/test/JsonToThriftTest
mkdir -p "$SRC_ROOT/$START_PATH"
cp -r "$START_PATH"/*.thrift "$SRC_ROOT/$START_PATH"

EXCLUSIONS="
myCollectionStruct
myMapStruct
myKeyStruct
"

mkdir -p "$PACKAGE_ROOT"
find "$SRC_ROOT" -name '*.thrift' | while read -r THRIFT; do
  BASENAME="$(basename "$THRIFT" .thrift)"
  (echo "$EXCLUSIONS" | grep --line-regexp "$BASENAME" > /dev/null) && continue
  mkdir -p "$PACKAGE_ROOT/$BASENAME"
  "$THRIFT_COMPILER" -out "$PACKAGE_ROOT/$BASENAME" -I "$SRC_ROOT" \
    --gen php:json "$THRIFT"
done

/home/engshare/svnroot/tfb/trunk/www/scripts/bin/hphpi \
  "${HERE}/thrift/test/JsonToThriftTest/SimpleJSONToThriftTest.php" \
  "${WORK}" \
  "${PACKAGE_ROOT}" \
  #
