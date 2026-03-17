#!/usr/bin/env bash

echo "# Set the command to be executed"
echo "bootcmd=run mmcbootcmd"
echo "mmcbootcmd=echo starting from MMC ; fatload mmc 0:1 49000000 fitMinix ; bootm 49000000"

exit 0
