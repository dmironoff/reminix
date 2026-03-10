#!/bin/sh

UBOOT_DIR=""
UBOOT_CONFIG=""
UBOOT_FDT=""
UBOOT_EXTRA_CONFIGS=""
UBOOT_CROSS_COMPILE=""

while getopts "c:f:e:t:d:?" c
do
        case "$c" in
        \?)
                echo "Usage: $0 -d uboot dir -c config name -t toolchain triplet [-f fdt ] [-e extra config options] " >&2
                exit 1
        ;;
        d)
                UBOOT_DIR=$OPTARG
		    ;;
        c)
                UBOOT_CONFIG=$OPTARG
		    ;;
		    f)
                UBOOT_FDT=$OPTARG
      	;;
        e)
                UBOOT_EXTRA_CONFIGS=$OPTARG
        ;;
        t)
                UBOOT_CROSS_COMPILE=$OPTARG
      	;;
	esac
done

if [ -z "$UBOOT_DIR" ]
then
		echo "Missing required parameters "
                echo "Usage: $0 -d uboot dir -c config name -t toolchain triplet [-f fdt ] [-e extra config options] " >&2
                exit 1
fi

if [ -z "$UBOOT_CONFIG" ]
then
		echo "Missing required parameters "
                echo "Usage: $0 -d uboot dir -c config name -t toolchain triplet [-f fdt ] [-e extra config options] " >&2
                exit 1
fi

if [ -z "$UBOOT_CROSS_COMPILE" ]
then
		echo "Missing required parameters "
                echo "Usage: $0 -d uboot dir -c config name -t toolchain triplet [-f fdt ] [-e extra config options] " >&2
                exit 1
fi

(

cd ${UBOOT_DIR}
make mrproper CROSS_COMPILE=${UBOOT_CROSS_COMPILE}
make ${UBOOT_CONFIG} CROSS_COMPILE=${UBOOT_CROSS_COMPILE}

#if [ ! -z "$UBOOT_EXTRA_CONFIGS" ]
#then
#echo <<EOF
#${UBOOT_EXTRA_CONFIGS}
#EOF >> .config

#fi

if [ ! -z "$UBOOT_FDT" ]
then
make CROSS_COMPILE=${UBOOT_CROSS_COMPILE} DEVICE_TREE=${UBOOT_FDT}
else
make CROSS_COMPILE=${UBOOT_CROSS_COMPILE}
fi

)