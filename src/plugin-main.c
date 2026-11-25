/*
LajiCast OBS plugin
Copyright (C) 2026 ProfeCésar

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>

extern bool lajicast_ui_load(void);
extern void lajicast_ui_unload(void);

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("ProfeCésar (revisión y adaptación escolar)")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Emisión escolar WebRTC y seguimiento de aulas para OBS Studio";
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "LajiCast cargado (version %s)", PLUGIN_VERSION);
	return lajicast_ui_load();
}

void obs_module_unload(void)
{
	lajicast_ui_unload();
	obs_log(LOG_INFO, "LajiCast descargado");
}
