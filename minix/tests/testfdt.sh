#!/bin/sh

# Test the device tree tools: dtc, fdtdump, fdtget, fdtput, fdtoverlay
# (dtc 1.8.1 and libfdt, docs/devicetree.md).

PATH=/bin:/usr/bin:/sbin:/usr/sbin
export PATH

echo -n "fdt test "

tmp=/tmp/testfdt.$$
trap 'rm -rf $tmp' 0

bomb() {
	echo "$*"
	exit 1
}

# expect <what> <expected> <actual>
expect() {
	[ "$2" = "$3" ] || bomb "$1: expected '$2', got '$3'"
}

mkdir $tmp || bomb "cannot create $tmp"
cd $tmp || bomb "cannot enter $tmp"

expect "dtc -v" "Version: DTC 1.8.1" "`dtc -v`"

cat > t.dts <<'EOF'
/dts-v1/;

/ {
	compatible = "reminix,test";
	model = "ReMinix fdt test";
	#address-cells = <1>;
	#size-cells = <1>;

	memory@40000000 {
		device_type = "memory";
		reg = <0x40000000 0x40000000>;
	};

	cpus {
		#address-cells = <1>;
		#size-cells = <0>;

		cpu0: cpu@0 {
			compatible = "arm,cortex-a7";
			device_type = "cpu";
			reg = <0>;
		};
	};

	chosen {
		bootargs = "console=ttyS0";
	};
};
EOF

# Compile with room for the boot loader and with symbols for overlays, as the
# .dtb build rules do (bsd.dtb.mk).
dtc -q -I dts -O dtb -p 1024 -@ -o t.dtb t.dts || bomb "dtc failed"
[ -s t.dtb ] || bomb "empty t.dtb"

fdtdump t.dtb 2>/dev/null | grep -q 'compatible = "reminix,test"' || \
	bomb "fdtdump: no root compatible"

expect "fdtget compatible" "reminix,test" "`fdtget t.dtb / compatible`"
expect "fdtget reg" "40000000 40000000" \
	"`fdtget -t x t.dtb /memory@40000000 reg`"
expect "fdtget -l" "cpu@0" "`fdtget -l t.dtb /cpus`"

fdtput -t s t.dtb /chosen bootargs "console=tty00" || bomb "fdtput failed"
expect "fdtput bootargs" "console=tty00" "`fdtget t.dtb /chosen bootargs`"
fdtput -c t.dtb /soc || bomb "fdtput -c failed"
fdtget -l t.dtb / | grep -q '^soc$' || bomb "fdtput -c: no /soc"

cat > o.dts <<'EOF'
/dts-v1/;
/plugin/;

&cpu0 {
	clock-frequency = <1008000000>;
};
EOF

dtc -q -@ -I dts -O dtb -o o.dtbo o.dts || bomb "dtc (overlay) failed"
fdtoverlay -i t.dtb -o m.dtb o.dtbo || bomb "fdtoverlay failed"
expect "overlay" "1008000000" "`fdtget -t u m.dtb /cpus/cpu@0 clock-frequency`"

# Back to source: the result must compile again.
dtc -q -I dtb -O dts -o m.dts m.dtb || bomb "dtc (dtb to dts) failed"
grep -q 'clock-frequency = <0x3c14dc00>;' m.dts || bomb "dts: no overlay value"
dtc -q -I dts -O dtb -o m2.dtb m.dts || bomb "dtc (dts to dtb) failed"

echo ok

exit 0
