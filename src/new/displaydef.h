
/**
 * @file displaydef.h
 *
 */

#ifndef WARZONE2100_DISPLAYDEF_H
#define WARZONE2100_DISPLAYDEF_H

#include <memory>

#include "lib/ivis_opengl/ivisdef.h"

/**
 * If set to `true`, the fog of war is lifted from the
 * perspective of the client and from the renderer as a result
 */
extern bool god_mode;

struct DisplayData
{
	std::unique_ptr<iIMDShape> imd_shape;
	unsigned frame_number;
	unsigned screen_x, screen_y, screen_r;
};

#endif //WARZONE2100_DISPLAYDEF_H
