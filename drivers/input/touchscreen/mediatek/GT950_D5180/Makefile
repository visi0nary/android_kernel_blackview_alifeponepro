include $(srctree)/drivers/misc/mediatek/Makefile.custom

# Linux driver folder
ccflags-y += -I$(srctree)/arch/arm/mach-$(MTK_PLATFORM)/$(ARCH_MTK_PROJECT)/touchpanel/GT950_1050/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/GT950_1050/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/

obj-y	+=  goodix_tool.o
obj-y	+=  gt9xx_driver.o
obj-y	+=  gt9xx_update.o

