obj-$(CONFIG_VIDEO_FIMC)	+= fimc_dev.o fimc_v4l2.o fimc_capture.o fimc_output.o fimc_overlay.o fimc_regs.o
ifneq ($(wildcard drivers/media/video/samsung/fimc/tq_auto_detect.c),)
obj-$(CONFIG_VIDEO_FIMC)		+= tq_auto_detect.o
else
obj-$(CONFIG_VIDEO_FIMC)		+= tq_auto_detect.module
endif

#log_msg.o
obj-$(CONFIG_VIDEO_FIMC_MIPI)	+= csis.o

ifeq ($(CONFIG_CPU_S5PV210),y)
EXTRA_CFLAGS += -DCONFIG_MIPI_CSI_ADV_FEATURE
endif

EXTRA_CFLAGS += -Idrivers/media/video

ifeq ($(CONFIG_VIDEO_FIMC_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif


ifneq ($(wildcard drivers/media/video/samsung/fimc/ov3640.c),)
obj-$(CONFIG_VIDEO_OV3640)		+= ov3640.o
else
obj-$(CONFIG_VIDEO_OV3640)		+= ov3640.module
endif
ifneq ($(wildcard drivers/media/video/samsung/fimc/ov9650.c),)
obj-$(CONFIG_VIDEO_OV9650)		+= ov9650.o
else
obj-$(CONFIG_VIDEO_OV9650)		+= ov9650.module
endif
ifneq ($(wildcard drivers/media/video/samsung/fimc/tvp5150.c),)
obj-$(CONFIG_VIDEO_TVP5150)		+= tvp5150.o
else
obj-$(CONFIG_VIDEO_TVP5150)		+= tvp5150.module
endif
ifneq ($(wildcard drivers/media/video/samsung/fimc/saa7113.c),)
obj-$(CONFIG_VIDEO_SAA7113)		+= saa7113.o
else
obj-$(CONFIG_VIDEO_SAA7113)		+= saa7113.module
endif

