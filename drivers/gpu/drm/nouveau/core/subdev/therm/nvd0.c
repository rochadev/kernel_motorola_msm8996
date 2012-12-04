/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "priv.h"

struct nvd0_therm_priv {
	struct nouveau_therm_priv base;
};

static int
pwm_info(struct nouveau_therm *therm, int line)
{
	u32 gpio = nv_rd32(therm, 0x00d610 + (line * 0x04));
	if (gpio & 0x00000040) {
		switch (gpio & 0x0000001f) {
		case 0x19: return 1;
		case 0x1c: return 0;
		default:
			break;
		}
	}

	nv_error(therm, "GPIO %d unknown PWM: 0x%08x\n", line, gpio);
	return -EINVAL;
}

static int
nvd0_fan_pwm_get(struct nouveau_therm *therm, int line, u32 *divs, u32 *duty)
{
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return indx;

	*divs = nv_rd32(therm, 0x00e114 + (indx * 8));
	*duty = nv_rd32(therm, 0x00e118 + (indx * 8));
	return 0;
}

static int
nvd0_fan_pwm_set(struct nouveau_therm *therm, int line, u32 divs, u32 duty)
{
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return indx;

	nv_wr32(therm, 0x00e114 + (indx * 8), divs);
	nv_wr32(therm, 0x00e118 + (indx * 8), duty | 0x80000000);
	return 0;
}

static int
nvd0_fan_pwm_clock(struct nouveau_therm *therm)
{
	return (nv_device(therm)->crystal * 1000) / 20;
}

static int
nvd0_therm_ctor(struct nouveau_object *parent,
		struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvd0_therm_priv *priv;
	int ret;

	ret = nouveau_therm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.fan.pwm_get = nvd0_fan_pwm_get;
	priv->base.fan.pwm_set = nvd0_fan_pwm_set;
	priv->base.fan.pwm_clock = nvd0_fan_pwm_clock;
	priv->base.base.temp_get = nv50_temp_get;
	return 0;
}

struct nouveau_oclass
nvd0_therm_oclass = {
	.handle = NV_SUBDEV(THERM, 0xd0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvd0_therm_ctor,
		.dtor = _nouveau_therm_dtor,
		.init = _nouveau_therm_init,
		.fini = _nouveau_therm_fini,
	},
};
