################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
build-62969583:
	@$(MAKE) --no-print-directory -Onone -f subdir_rules.mk build-62969583-inproc

build-62969583-inproc: ../release.cfg
	@echo 'Building file: "$<"'
	@echo 'Invoking: XDCtools'
	"C:/ti/xdctools_3_50_05_12_core/xs" --xdcpath="C:/ti/simplelink_cc13x0_sdk_2_10_00_36/source;C:/ti/simplelink_cc13x0_sdk_2_10_00_36/kernel/tirtos/packages;" xdc.tools.configuro -o configPkg -t ti.targets.arm.elf.M3 -p ti.platforms.simplelink:CC1350F128 -r release -c "C:/ti/ccsv8/tools/compiler/ti-cgt-arm_18.1.2.LTS" --compileOptions " -DDeviceFamily_CC13X0 " "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

configPkg/linker.cmd: build-62969583 ../release.cfg
configPkg/compiler.opt: build-62969583
configPkg/: build-62969583


