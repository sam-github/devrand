//
// devrandirq.c
//
// Copyright (c) 2000, Sam Roberts
// 
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 1, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
//  I can be contacted as sroberts@uniserve.com.
//

#include <sys/proxy.h>
#include <sys/irqinfo.h>
#include <sys/inline.h>

#include "devrandirq.h"

static int irqProxy;

static pid_t far IrqHook()
{
	return irqProxy;
}

int HookIrqNo(int irq)
{
	if(!irqProxy) {
		irqProxy = qnx_proxy_attach(0, 0, 0, -1);
		if(irqProxy == -1)
			return -1;
	}
	if(qnx_hint_attach(irq, IrqHook, _ds()) == -1)
		return -1;

	return 0;
}

pid_t IrqProxy()
{
	return irqProxy;
}

