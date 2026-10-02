#!/bin/sh
#
# Get the MINIX u-boot tree (prebuilt MLO/u-boot.img for the TI boards under
# build/<config>/) at a fixed commit into OUTPUT_DIR.
#
# The original server, git://git.minix3.org/u-boot, is gone; the repository
# is mirrored on GitHub. Only the wanted commit is fetched (no history).
# OUTPUT_DIR should live in the object tree (e.g. $OBJ/u-boot), not in the
# source tree.
#
: ${UBOOT_REPO_URL=https://github.com/Stichting-MINIX-Research-Foundation/u-boot.git}

OUTPUT_DIR=""
GIT_VERSION=""
while getopts "o:n:?" c
do
	case "$c" in
	\?)
		echo "Usage: $0 -o output dir -n version " >&2
		exit 1
		;;
	o)
		OUTPUT_DIR=$OPTARG
		;;
	n)
		GIT_VERSION=$OPTARG
		;;
	esac
done

if [ -z "$OUTPUT_DIR" -o -z "$GIT_VERSION" ]
then
	echo "Missing required parameters OUTPUT_DIR=$OUTPUT_DIR GIT_VERSION=$GIT_VERSION"
	echo "Usage: $0 -o output dir -n version " >&2
	exit 1
fi

if [ -d "$OUTPUT_DIR/.git" ] &&
   [ "`git -C "$OUTPUT_DIR" rev-parse HEAD 2>/dev/null`" = "$GIT_VERSION" ]
then
	exit 0		# already there
fi

echo "Fetching u-boot $GIT_VERSION from $UBOOT_REPO_URL into $OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
(
	cd "$OUTPUT_DIR" || exit 1
	[ -d .git ] || git init -q || exit 1
	git fetch -q --depth 1 "$UBOOT_REPO_URL" "$GIT_VERSION" || exit 1
	git checkout -q --force FETCH_HEAD || exit 1
) || { echo "u-boot fetch failed" >&2; exit 1; }
