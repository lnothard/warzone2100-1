/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * @file bucket3d.cpp
 *
 * Stores object render calls in a linked list renders
 * after bucket sorting objects
 */

#include <vector>

#include "lib/framework/fixedpoint.h"
#include "lib/framework/vector.h"

#include "atmos.h"
#include "bucket3d.h"
#include "component.h"
#include "display3d.h"
#include "displaydef.h"
#include "effects.h"
#include "feature.h"
#include "miscimd.h"
#include "positiondef.h"
#include "projectile.h"

struct BodyStats;
struct Droid;
struct iIMDShape;
struct BaseObject;
int pie_RotateProject(const Vector3i*, const glm::mat4&, Vector2i*);
static std::vector<BUCKET_TAG> bucketArray;


BUCKET_TAG::BUCKET_TAG(RENDER_TYPE objectType, void* objPtr, int z)
  : objectType{objectType}, pObject{objPtr}, actualZ{z}
{
}

bool BUCKET_TAG::operator<(BUCKET_TAG const& b) const
{
  return actualZ > b.actualZ; // sort in reverse z order
}

static int bucketCalculateZ(RENDER_TYPE objectType, void* pObject, glm::mat4 const& viewMatrix)
{
	int z = 0, radius;
	Vector2i pixel(0, 0);
	Vector3i position(0, 0, 0);
	unsigned droidSize;
	BodyStats const* psBStats;
  BaseObject* psSimpObj;
	iIMDShape* pImd;
	Spacetime spacetime;

	switch (objectType) {
    using enum RENDER_TYPE;
	  case RENDER_PARTICLE:
	  	position.x = static_cast<int>(((Particle*)pObject)->position.x);
	  	position.y = static_cast<int>(((Particle*)pObject)->position.y);
	  	position.z = static_cast<int>(((Particle*)pObject)->position.z);

	  	position.x = position.x - playerPos.p.x;
	  	position.z = -(position.z - playerPos.p.z);

      /* 16 below is HACK!!! */
	  	z = pie_RotateProject(&position, viewMatrix, &pixel) - 16;
      if (z <= 0) break;

      //particle use the image radius
      radius = ((Particle*)pObject)->imd->radius;
      radius *= SCALE_DEPTH;
      radius /= z;
      if (pixel.x + radius < CLIP_LEFT || pixel.x - radius > CLIP_RIGHT ||
          pixel.y + radius < CLIP_TOP || pixel.y - radius > CLIP_BOTTOM) {
        z = -1;
      }
      break;

	  case RENDER_PROJECTILE: {
      auto psProj = static_cast<Projectile*>(pObject);
      if (psProj && (psProj->getWeaponStats()->weaponSubClass == WEAPON_SUBCLASS::FLAME ||
                     psProj->getWeaponStats()->weaponSubClass == WEAPON_SUBCLASS::COMMAND ||
                     psProj->getWeaponStats()->weaponSubClass == WEAPON_SUBCLASS::EMP)) {
        /* We don't do projectiles from these guys, cos there's an effect instead */
        z = -1;
        break;
      }
      //the weapon stats holds the reference to which graphic to use
      pImd = psProj->getWeaponStats()->pInFlightGraphic;

      psSimpObj = static_cast<BaseObject*>(pObject);
      position.x = psSimpObj->getPosition().x - playerPos.p.x;
      position.z = -(psSimpObj->getPosition().y - playerPos.p.z);
      position.y = psSimpObj->getPosition().z;

      z = pie_RotateProject(&position, viewMatrix, &pixel);

      if (z <= 0)
        break;

      //particle use the image radius
      radius = pImd->radius * SCALE_DEPTH / z;
      if (pixel.x + radius < CLIP_LEFT || pixel.x - radius > CLIP_RIGHT ||
          pixel.y + radius < CLIP_TOP || pixel.y - radius > CLIP_BOTTOM) {
        z = -1;
      }
      break;
    }

	  case RENDER_STRUCTURE: //not depth sorted
	  	psSimpObj = (BaseObject*)pObject;
	  	position.x = psSimpObj->getPosition().x - playerPos.p.x;
	  	position.z = -(psSimpObj->getPosition().y - playerPos.p.z);

	  	if (((Structure *) pObject)->getStats()->type == STRUCTURE_TYPE::DEFENSE ||
          ((Structure *) pObject)->getStats()->type == STRUCTURE_TYPE::WALL ||
          ((Structure *) pObject)->getStats()->type == STRUCTURE_TYPE::WALL_CORNER) {
	  		position.y = psSimpObj->getPosition().z + 64;
	  		radius = ((Structure*)pObject)->getDisplayData()->imd_shape->radius; //walls guntowers and tank traps clip tightly
	  	}
	  	else {
	  		position.y = psSimpObj->getPosition().z;
        radius = ((Structure *) pObject)->getDisplayData()->imd_shape->radius;
	  	}

	  	z = pie_RotateProject(&position, viewMatrix, &pixel);

	  	if (z <= 0)
        break;

      //particle use the image radius
      radius *= SCALE_DEPTH;
      radius /= z;
      if (pixel.x + radius < CLIP_LEFT || pixel.x - radius > CLIP_RIGHT ||
          pixel.y + radius < CLIP_TOP || (pixel.y - radius > CLIP_BOTTOM)) {
        z = -1;
      }
	  	break;

	  case RENDER_FEATURE: //not depth sorted
	  	psSimpObj = static_cast<BaseObject*>(pObject);
      position = {psSimpObj->getPosition().x - playerPos.p.x,
                  psSimpObj->getPosition().z + 2,
                  -(psSimpObj->getPosition().y - playerPos.p.z)};

	  	z = pie_RotateProject(&position, viewMatrix, &pixel);

	  	if (z <= 0)
        break;

      //particle use the image radius
      radius = ((Feature*)pObject)->getDisplayData()->imd_shape->radius;
      radius *= SCALE_DEPTH;
      radius /= z;

      if (pixel.x + radius < CLIP_LEFT || pixel.x - radius > CLIP_RIGHT ||
          pixel.y + radius < CLIP_TOP || pixel.y - radius > CLIP_BOTTOM) {
        z = -1;
      }
	  	break;

	  case RENDER_DROID:
    {
      auto psDroid = static_cast<Droid*>(pObject);
      psSimpObj = static_cast<BaseObject*>(pObject);
      position.x = psSimpObj->getPosition().x - playerPos.p.x;
      position.z = -(psSimpObj->getPosition().y - playerPos.p.z);
      position.y = psSimpObj->getPosition().z;

      psBStats = dynamic_cast<BodyStats const *>(psDroid->getComponent(COMPONENT_TYPE::BODY));
      droidSize = psBStats->pIMD->radius;
      z = pie_RotateProject(&position, viewMatrix, &pixel) - (droidSize * 2);

      if (z <= 0)
        break;

      //particle use the image radius
      radius = droidSize * SCALE_DEPTH / z;
      if (pixel.x + radius < CLIP_LEFT || pixel.x - radius > CLIP_RIGHT ||
          pixel.y + radius < CLIP_TOP || pixel.y - radius > CLIP_BOTTOM) {
        z = -1;
      }
      break;
    }

	  case RENDER_PROXMSG:
	  	if (((PROXIMITY_DISPLAY*)pObject)->type == POSITION_TYPE::POS_PROXDATA) {
	  		auto ptr = (PROXIMITY_DISPLAY*)pObject;
	  		position.x = ((VIEW_PROXIMITY*)ptr->psMessage->pViewData->pData)->x - playerPos.p.x;
	  		position.z = -(((VIEW_PROXIMITY*)ptr->psMessage->pViewData->pData)->y - playerPos.p.z);
	  		position.y = ((VIEW_PROXIMITY*)ptr->psMessage->pViewData->pData)->z;
	  	}
	  	else if (((PROXIMITY_DISPLAY*)pObject)->type == POSITION_TYPE::POS_PROXOBJ) {
	  		auto ptr = (PROXIMITY_DISPLAY*)pObject;
	  		position.x = ptr->psMessage->psObj->getPosition().x - playerPos.p.x;
	  		position.z = -(ptr->psMessage->psObj->getPosition().y - playerPos.p.z);
	  		position.y = ptr->psMessage->psObj->getPosition().z;
	  	}
	  	z = pie_RotateProject(&position, viewMatrix, &pixel);

      if (z <= 0)
        break;

      //particle use the image radius
      pImd = getImdFromIndex(MI_BLIP_ENEMY);//use MI_BLIP_ENEMY as all are same radius
      radius = pImd->radius * SCALE_DEPTH / z;
      if (pixel.x + radius < CLIP_LEFT || pixel.x - radius > CLIP_RIGHT ||
          pixel.y + radius < CLIP_TOP || pixel.y - radius > CLIP_BOTTOM) {
        z = -1;
      }

    case RENDER_EFFECT:
	  	position.x = static_cast<int>(((EFFECT*)pObject)->position.x - playerPos.p.x);
	  	position.z = static_cast<int>(-(((EFFECT*)pObject)->position.z - playerPos.p.z));
	  	position.y = static_cast<int>(((EFFECT*)pObject)->position.y);

	  /* 16 below is HACK!!! */
	  	z = pie_RotateProject(&position, viewMatrix, &pixel) - 16;

      if (z <= 0)
        break;

      //particle use the image radius
      pImd = ((EFFECT*)pObject)->imd.get();
      if (pImd == nullptr)
        break;

      radius = pImd->radius;
      radius *= SCALE_DEPTH;
      radius /= z;
      if (pixel.x + radius < CLIP_LEFT || pixel.x - radius > CLIP_RIGHT ||
          pixel.y + radius < CLIP_TOP || pixel.y - radius > CLIP_BOTTOM) {
        z = -1;
      }
      break;

    case RENDER_DELIVPOINT:
	  	position.x = ((FlagPosition*)pObject)->coords.x - playerPos.p.x;
	  	position.z = -(((FlagPosition*)pObject)->coords.y - playerPos.p.z);
	  	position.y = ((FlagPosition*)pObject)->coords.z;

	  	z = pie_RotateProject(&position, viewMatrix, &pixel);

      if (z <= 0)
        break;

      //particle use the image radius
      radius = pAssemblyPointIMDs[((FlagPosition*)pObject)->factoryType][((FlagPosition*)pObject)->factoryInc]->radius;
      radius *= SCALE_DEPTH;
      radius /= z;
      if (pixel.x + radius < CLIP_LEFT || pixel.x - radius > CLIP_RIGHT ||
          pixel.y + radius < CLIP_TOP || pixel.y - radius > CLIP_BOTTOM) {
        z = -1;
      }
      break;
    default:
	  	break;
	}
	return z;
}

/* add an object to the current render list */
void bucketAddTypeToList(RENDER_TYPE objectType, void* pObject, const glm::mat4& viewMatrix)
{
	const iIMDShape* pie;
	auto z = bucketCalculateZ(objectType, pObject, viewMatrix);

	if (z < 0) {
		/* Object will not be rendered - has been clipped! */
		if (objectType == RENDER_TYPE::RENDER_DROID || objectType == RENDER_TYPE::RENDER_STRUCTURE) {
			/* Won't draw selection boxes */
			((BaseObject *)pObject)->setFrameNumber(0);
		}
		return;
	}

  switch (objectType) {
    using enum RENDER_TYPE;
    using enum EFFECT_GROUP;
    case RENDER_EFFECT:
      switch (((EFFECT*)pObject)->group)
      {
        case EXPLOSION:
        case CONSTRUCTION:
        case SMOKE:
        case FIREWORK:
          // Use calculated Z
          break;

        case WAYPOINT:
          pie = ((EFFECT*)pObject)->imd.get();
          z = INT32_MAX - pie->texpage;
          break;

        default:
          z = INT32_MAX - 42;
          break;
      }
      break;
    case RENDER_DROID:
      pie = BODY_IMD(((Droid *)pObject), 0);
      z = INT32_MAX - pie->texpage;
      break;
    case RENDER_STRUCTURE:
      pie = ((Structure*)pObject)->getDisplayData()->imd_shape.get();
      z = INT32_MAX - pie->texpage;
      break;
    case RENDER_FEATURE:
      pie = ((Feature*)pObject)->getDisplayData()->imd_shape.get();
      z = INT32_MAX - pie->texpage;
      break;
    case RENDER_DELIVPOINT:
      pie = pAssemblyPointIMDs[((FlagPosition*)pObject)->
              factoryType][((FlagPosition*)pObject)->factoryInc];
      z = INT32_MAX - pie->texpage;
      break;
    case RENDER_PARTICLE:
      z = 0;
      break;
    default:
      // Use calculated Z
      break;
  }

	//put the object data into the tag
  auto newTag = BUCKET_TAG{objectType, pObject, z};

	//add tag to bucketArray
	bucketArray.push_back(newTag);
}

/* render Objects in list */
void bucketRenderCurrentList(const glm::mat4& viewMatrix)
{
  std::sort(bucketArray.begin(), bucketArray.end());

  for (auto& thisTag : bucketArray)
  {
    switch (thisTag.objectType) {
      using enum RENDER_TYPE;
      case RENDER_PARTICLE:
        ::renderParticle((Particle*)thisTag.pObject, viewMatrix);
        break;
      case RENDER_EFFECT:
        renderEffect((EFFECT*)thisTag.pObject, viewMatrix);
        break;
      case RENDER_DROID:
        displayComponentObject((Droid*)thisTag.pObject, viewMatrix);
        break;
      case RENDER_STRUCTURE:
        renderStructure((Structure*)thisTag.pObject, viewMatrix);
        break;
      case RENDER_FEATURE:
        renderFeature((Feature*)thisTag.pObject, viewMatrix);
        break;
      case RENDER_PROXMSG:
        renderProximityMsg((PROXIMITY_DISPLAY*)thisTag.pObject, viewMatrix);
        break;
      case RENDER_PROJECTILE:
        renderProjectile((Projectile*)thisTag.pObject, viewMatrix);
        break;
      case RENDER_DELIVPOINT:
        renderDeliveryPoint((FlagPosition*)thisTag.pObject, false, viewMatrix);
        break;
    }
  }

	//reset the bucket array as we go
	bucketArray.resize(0);
}
