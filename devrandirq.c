/*
* devrandirq.c
*/

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

