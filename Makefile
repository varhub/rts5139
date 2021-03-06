# Driver for Realtek RTS51xx USB card reader
#
# Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.  
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <http://www.gnu.org/licenses/>.
#
# Author:
#   wwang (wei_wang@realsil.com.cn)
#   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
#
# Makefile for the RTS51xx USB Card Reader drivers.
#

TARGET_MODULE := rts5139

EXTRA_CFLAGS := -Idrivers/scsi -I$(PWD)

obj-m += $(TARGET_MODULE).o

common-obj := rts51x_transport.o rts51x_common.o rts51x_scsi.o rts51x_fop.o

$(TARGET_MODULE)-objs := $(common-obj) rts51x.o rts51x_chip.o rts51x_card.o \
		xd.o sd.o ms.o sd_cprm.o ms_mg.o

default:
	sed "s/RTSX_MK_TIME/`date +%y.%m.%d.%H.%M`/" timestamp.in > timestamp.h
	cp -f define.release define.h
	make -C /lib/modules/$(shell uname -r)/build/ SUBDIRS=$(PWD) modules
debug:
	sed "s/RTSX_MK_TIME/`date +%y.%m.%d.%H.%M`/" timestamp.in > timestamp.h
	cp -f define.debug define.h
	make -C /lib/modules/$(shell uname -r)/build/ SUBDIRS=$(PWD) modules
install:
	cp $(TARGET_MODULE).ko /lib/modules/$(shell uname -r)/kernel/drivers/scsi -f
clean:
	rm -f *.o *.ko
	rm -f $(TARGET_MODULE).mod.c


