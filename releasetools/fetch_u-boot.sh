#!/bin/sh 
#
# Perform a checkout / update the MINIX u-boot git repo if needed
# 
: ${UBOOT_REPO_URL=https://github.com/dmironoff/u-boot}

# -o output dir
OUTPUT_DIR=""
while getopts "o:?" c
do
        case "$c" in
        \?)
                echo "Usage: $0 -o output dir " >&2
                exit 1
        	;;
        o)
                OUTPUT_DIR=$OPTARG
		;;

	esac
done


#
# check arguments
#
if [ -z "$OUTPUT_DIR" ]
then
		echo "Missing required parameters OUTPUT_DIR=$OUTPUT_DIR "
                echo "Usage: $0 -o output dir  " >&2
                exit 1
fi


if  [ ! -e "$OUTPUT_DIR" ]
then
	git clone ${UBOOT_REPO_URL} $OUTPUT_DIR
fi

