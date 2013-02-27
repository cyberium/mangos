/*
 * This file is part of the Continued-MaNGOS Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Creature.h"
#include "MapManager.h"
#include "RandomMovementGenerator.h"
#include "Map.h"
#include "Util.h"
#include "movement/MoveSplineInit.h"
#include "movement/MoveSpline.h"

template<>
RandomMovementGenerator<Creature>::RandomMovementGenerator(const Creature& creature)
{
    float respX, respY, respZ, respO, wander_distance;
    creature.GetRespawnCoord(respX, respY, respZ, &respO, &wander_distance);
    i_nextMoveTime = ShortTimeTracker(0);
    i_x = respX;
    i_y = respY;
    i_z = respZ;
    i_radius = wander_distance;
    // TODO - add support for flying mobs using some distance
    i_verticalZ = 0.0f;
}

template<>
void RandomMovementGenerator<Creature>::_setRandomLocation(Creature& creature)
{
    float destX = i_x;
    float destY = i_y;
    float destZ = i_z;

    creature.addUnitState(UNIT_STAT_ROAMING_MOVE);

    // check if new random position is assigned, GetRandomPoint may fail
    if (creature.GetRandomPosition(destX, destY, destZ, i_radius))
    {
        Movement::MoveSplineInit init(creature);
        init.MoveTo(destX, destY, destZ, true);
        init.SetWalk(true);
        init.Launch();
        i_nextMoveTime.Reset(urand(1000, 10000));
    }
    else
        i_nextMoveTime.Reset(0); // Retry in next update
}

template<>
void RandomMovementGenerator<Creature>::Initialize(Creature& creature)
{
    creature.addUnitState(UNIT_STAT_ROAMING);               // _MOVE set in _setRandomLocation

    if (!creature.isAlive() || creature.hasUnitState(UNIT_STAT_NOT_MOVE))
        return;

    _setRandomLocation(creature);
}

template<>
void RandomMovementGenerator<Creature>::Reset(Creature& creature)
{
    Initialize(creature);
}

template<>
void RandomMovementGenerator<Creature>::Interrupt(Creature& creature)
{
    creature.clearUnitState(UNIT_STAT_ROAMING | UNIT_STAT_ROAMING_MOVE);
    creature.SetWalk(!creature.hasUnitState(UNIT_STAT_RUNNING_STATE), false);
}

template<>
void RandomMovementGenerator<Creature>::Finalize(Creature& creature)
{
    creature.clearUnitState(UNIT_STAT_ROAMING | UNIT_STAT_ROAMING_MOVE);
    creature.SetWalk(!creature.hasUnitState(UNIT_STAT_RUNNING_STATE), false);
}

template<>
bool RandomMovementGenerator<Creature>::Update(Creature& creature, const uint32& diff)
{
    if (creature.hasUnitState(UNIT_STAT_NOT_MOVE))
    {
        i_nextMoveTime.Reset(0);  // Expire the timer
        creature.clearUnitState(UNIT_STAT_ROAMING_MOVE);
        return true;
    }

    if (creature.movespline->Finalized())
    {
        i_nextMoveTime.Update(diff);
        if (i_nextMoveTime.Passed())
            _setRandomLocation(creature);
    }
    return true;
}
