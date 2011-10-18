/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
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

#include "WorldSession.h"
#include "Log.h"
#include "Player.h"
#include "WorldPacket.h"
#include "ObjectMgr.h"
#include "World.h"
#include "AccountMgr.h"
#include "LFGMgr.h"

void BuildPlayerLockDungeonBlock(WorldPacket &data, tLfgLockStatusList* lockSet)
{
    data << uint32(lockSet->size());
    for (tLfgLockStatusList::iterator itr = lockSet->begin(); itr != lockSet->end(); ++itr)
    {
        data << uint32((*itr)->dungeonInfo->Entry());              // Dungeon entry + type
        data << uint32((*itr)->lockType);                          // Lock status
    }
}

void BuildPartyLockDungeonBlock(WorldPacket &data, tLfgLockStatusMap* lockMap)
{
    if (!lockMap || !lockMap->size())
    {
        data << uint8(0);
        return;
    }
    data << uint8(lockMap->size());
    for (tLfgLockStatusMap::const_iterator itr = lockMap->begin(); itr != lockMap->end(); ++itr)
    {
        data << uint64(ObjectGuid(HIGHGUID_PLAYER, 0, itr->first).GetRawValue()); // Player guid
        BuildPlayerLockDungeonBlock(data, itr->second);
    }
}

void WorldSession::HandleLfgJoinOpcode( WorldPacket & recv_data )
{
    if (!sWorld.getConfig(CONFIG_BOOL_DUNGEON_FINDER_ENABLE))
    {
        recv_data.rpos(recv_data.wpos());
        sLog.outDebug("CMSG_LFG_JOIN [%u] Dungeon finder disabled", GetPlayer()->GetObjectGuid().GetCounter());
        return;
    }

    uint8 DungeonCount;
    uint32 DungeonEntry;
    uint32 Roles;
    std::string comment;
    pLfgDungeonSet dungeonSet(new tLfgDungeonSet);

    recv_data >> Roles;
    recv_data.read_skip<uint8>();                           // unk - always 0
    recv_data.read_skip<uint8>();                           // unk - always 0
    recv_data >> DungeonCount;
    if (!DungeonCount)
    {
        sLog.outDebug("CMSG_LFG_JOIN [%u] no dungeons selected", GetPlayer()->GetObjectGuid().GetCounter());
        recv_data.rpos(recv_data.wpos());
        return;
    }

    for (int8 i = 0 ; i < DungeonCount; ++i)
    {
        recv_data >> DungeonEntry;
        dungeonSet->insert(DungeonEntry & 0x00FFFFFF);         // remove the type from the dungeon entry
        sLog.outDebug("received dungeon entry : %u",(DungeonEntry & 0x00FFFFFF));
    }
    uint8 Dummy;
    recv_data >> Dummy;                               // unk - always 3
    for (int8 i = 0 ; i < Dummy; ++i)
        recv_data.read_skip<uint8>();                       // unk - always 0

    recv_data >> comment;

    sLog.outDebug("comment: %",comment.c_str());
    sLFGMgr.LfgJoin(_player, dungeonSet, Roles);
}

void WorldSession::HandleLfgLeaveOpcode( WorldPacket & /*recv_data*/ )
{
    DEBUG_LOG("CMSG_LFG_LEAVE");
    Group* grp = _player->GetGroup();
    ObjectGuid guid = _player->GetObjectGuid();
    sLFGMgr.LfgEvent(LFG_EVENT_PLAYER_LEAVE_QUEUE, guid);
}

void WorldSession::SendLfgJoinResult(uint8 checkResult, uint8 checkValue /* = 0 */, tLfgLockStatusMap* playersLockMap /* = NULL */)
{
    uint32 size = 0;
    if (playersLockMap)
    {
        for (tLfgLockStatusMap::const_iterator it = playersLockMap->begin(); it != playersLockMap->end(); ++it)
            size += 8 + 4 + it->second->size() * (4 + 4);
    }

    sLog.outDebug("SMSG_LFG_ROLE_CHOSEN [%u] checkResult: %u checkValue: %u", _player->GetObjectGuid().GetCounter(), checkResult, checkValue);
    WorldPacket data(SMSG_LFG_JOIN_RESULT, 4 + 4 + size);
    data << uint32(checkResult);                            // Check Result
    data << uint32(checkValue);                             // Check Value
    if (playersLockMap)
        BuildPartyLockDungeonBlock(data, playersLockMap);
    SendPacket(&data);
}

void WorldSession::HandleLfgPlayerLockInfoRequestOpcode(WorldPacket &/*recv_data*/)
{
    sLog.outDebug("CMSG_LFD_PLAYER_LOCK_INFO_REQUEST [%u]", _player->GetObjectGuid().GetCounter());

    tLfgDungeonList *random = sLFGMgr.GetOptionalDungeons(_player);
    tLfgLockStatusList *locks = sLFGMgr.GetDungeonsLock(_player);
    uint32 rsize = random->size();
    uint32 lsize = locks->size();
    sLog.outDebug("HandleLfgPlayerLockInfoRequestOpcode> rsize=%u, lsize=%u", rsize,lsize);
    WorldPacket data(SMSG_LFG_PLAYER_INFO);
    //SMSG uint8, for (uint8) { uint32, uint8, uint32, uint32, uint32, uint32, uint8, for (uint8) {uint32,uint32, uint32}}, uint32, for (uint32) {uint32,uint32}
    if (rsize == 0)
        data << uint8(0);
    else
    {
        data << uint8(rsize);                                      // Random Dungeon count
        for (tLfgDungeonList::iterator itr = random->begin(); itr != random->end(); ++itr)
        {
            data << uint32((*itr)->Entry());                       // Entry(ID and type) of random dungeon
            sLFGMgr.BuildRewardBlock(data, (*itr)->ID, _player);
        }
    }
    BuildPlayerLockDungeonBlock(data, locks);
    SendPacket(&data);
    //data.hexlike();
    delete random;
    delete locks;
}

void WorldSession::HandleLfgPartyLockInfoRequestOpcode(WorldPacket & /*recv_data*/)
{
    DEBUG_LOG("WORLD: Received CMSG_LFD_PARTY_LOCK_INFO_REQUEST");

    // TODO: Find out why its sometimes send even when player is not in group...
    if(!_player->GetGroup())
    {
        DEBUG_LOG("Recieved CMSG_LFD_PARTY_LOCK_INFO_REQUEST but player [%u] is not in Group!", _player->GetObjectGuid().GetCounter());
        return;
    }

    if (tLfgLockStatusMap* lockMap = sLFGMgr.GetPartyLockStatusDungeons(_player))
    {
        uint32 size = 0;
        for (tLfgLockStatusMap::const_iterator it = lockMap->begin(); it != lockMap->end(); ++it)
            size += 8 + 4 + it->second->size() * (4 + 4);
        sLog.outDebug("SMSG_LFG_PARTY_INFO [%u]", _player->GetObjectGuid().GetCounter());
        WorldPacket data(SMSG_LFG_PARTY_INFO, 1 + size);
        BuildPartyLockDungeonBlock(data, lockMap);
        lockMap->clear();
        delete lockMap;
        SendPacket(&data);
        //data.hexlike();
    }
}

void WorldSession::SendLfgUpdatePlayer(uint8 updateType)
{
    bool queued = false;
    bool extrainfo = false;
    uint8 size = 0;

    switch(updateType)
    {
    case LFG_UPDATETYPE_JOIN_PROPOSAL:
    case LFG_UPDATETYPE_ADDED_TO_QUEUE:
        queued = true;
        extrainfo = true;
        break;
    //case LFG_UPDATETYPE_CLEAR_LOCK_LIST: // TODO: Sometimes has extrainfo - Check ocurrences...
    case LFG_UPDATETYPE_PROPOSAL_BEGIN:
        extrainfo = true;
        break;

    }
    pLfgDungeonSet dungeons = sLFGMgr.GetDungeonsByGuid(GetPlayer()->GetObjectGuid());
    if (dungeons)
        size = dungeons->size();
    std::string comment = ""; // todo send comment

    sLog.outDebug("SMSG_LFG_UPDATE_PLAYER [%u] updatetype: %u", GetPlayer()->GetObjectGuid().GetCounter(), updateType);
    WorldPacket data(SMSG_LFG_UPDATE_PLAYER, 1 + 1 + (extrainfo ? 1 : 0) * (1 + 1 + 1 + 1 + size * 4 + comment.length()));
    data << uint8(updateType);                              // Lfg Update type
    data << uint8(extrainfo);                               // Extra info
    if (extrainfo)
    {
        data << uint8(queued);                              // Join the queue
        data << uint8(0);                                   // unk - Always 0
        data << uint8(0);                                   // unk - Always 0

        data << uint8(size);
        if (size)
            for (tLfgDungeonSet::const_iterator it = dungeons->begin(); it != dungeons->end(); ++it)
                data << uint32(*it);

        data << comment;
    }
    SendPacket(&data);
}

void WorldSession::SendLfgUpdateParty(uint8 updateType, pLfgDungeonSet dungeonSet /*= NULL*/)
{
    bool join = false;
    bool extrainfo = false;
    bool queued = false;
    pLfgDungeonSet dungeons = sLFGMgr.GetDungeonsByGuid(GetPlayer()->GetObjectGuid());
    switch(updateType)
    {
    case LFG_UPDATETYPE_JOIN_PROPOSAL:
        extrainfo = true;
        dungeons = dungeonSet;
        break;
    case LFG_UPDATETYPE_ADDED_TO_QUEUE:
        extrainfo = true;
        join = true;
        queued = true;
        break;
    case LFG_UPDATETYPE_CLEAR_LOCK_LIST:
        // join = true;  // TODO: Sometimes queued and extrainfo - Check ocurrences...
        queued = true;
        break;
    case LFG_UPDATETYPE_PROPOSAL_BEGIN:
        extrainfo = true;
        join = true;
        break;
    }

    uint8 size = (dungeons == NULL) ? 0 : dungeons->size();
    std::string comment = "";

    sLog.outDebug("SMSG_LFG_UPDATE_PARTY [%u] updatetype: %u", _player->GetObjectGuid().GetCounter(), updateType);
    WorldPacket data(SMSG_LFG_UPDATE_PARTY, 1 + 1 + (extrainfo ? 1 : 0) * (1 + 1 + 1 + 1 + 1 + size * 4 + comment.length()));
    data << uint8(updateType);                              // Lfg Update type
    data << uint8(extrainfo);                               // Extra info
    if (extrainfo)
    {
        data << uint8(join);                                // LFG Join
        data << uint8(queued);                              // Join the queue
        data << uint8(0);                                   // unk - Always 0
        data << uint8(0);                                   // unk - Always 0
        for (uint8 i = 0; i < 3; ++i)
            data << uint8(0);                               // unk - Always 0

        data << uint8(size);
        if (size > 0)
            for (tLfgDungeonSet::const_iterator it = dungeons->begin(); it != dungeons->end(); ++it)
                data << uint32(*it);

        data << comment;
    }
    SendPacket(&data);
}

void WorldSession::SendLfgQueueStatus(uint32 dungeon, int32 waitTime, int32 avgWaitTime, int32 waitTimeTanks, int32 waitTimeHealer, int32 waitTimeDps, uint32 queuedTime, uint8 tanks, uint8 healers, uint8 dps)
{
    sLog.outDebug("SMSG_LFG_QUEUE_STATUS [%u] dungeon: %u\nwaitTime: %d\naverage WaitTime: %d\nwaitTimeTanks: %d\nwaitTimeHealer: %d\nwaitTimeDps: %d\nqueuedTime: %u\ntanks needed: %u\nhealers needed: %u\ndps needed: %u",
        _player->GetObjectGuid().GetCounter(), dungeon, waitTime, avgWaitTime, waitTimeTanks, waitTimeHealer, waitTimeDps, queuedTime, tanks, healers, dps);

    WorldPacket data(SMSG_LFG_QUEUE_STATUS, 4 + 4 + 4 + 4 + 4 +4 + 1 + 1 + 1 + 4);
    data << uint32(dungeon);                                // Dungeon
    data << int32(avgWaitTime);                             // Average Wait time
    data << int32(waitTime);                                // Wait Time
    data << int32(waitTimeTanks);                           // Wait Tanks
    data << int32(waitTimeHealer);                          // Wait Healers
    data << int32(waitTimeDps);                             // Wait Dps
    data << uint8(tanks);                                   // Tanks needed
    data << uint8(healers);                                 // Healers needed
    data << uint8(dps);                                     // Dps needed
    data << uint32(queuedTime);                             // Player wait time in queue
    SendPacket(&data);
}

void WorldSession::SendUpdateProposal(s_LfgProposal const* prop)
{
    ObjectGuid playerGuid = _player->GetObjectGuid();
    uint32 playerGroupId = 0;
    Group* playerGroup = _player->GetGroup();
    if (playerGroup)
        playerGroupId = playerGroup->GetId();
    uint32 dungeonId = prop->NewGroupInfo->DungeonEntry->Entry();
    uint32 isSameDungeon = 0;
    uint8 state = prop->State;
    uint32 groupSize = 5;

    sLog.outDebug("SMSG_LFG_PROPOSAL_UPDATE [%u] state: %u", _player->GetObjectGuid().GetCounter(), state);

    WorldPacket data(SMSG_LFG_PROPOSAL_UPDATE, 4 + 1 + 4 + 4 + 1 + 1 + groupSize * (4 + 1 + 1 + 1 + 1 +1));

    data << uint32(dungeonId);                              // Dungeon
    data << uint8(state);                                   // Result state
    data << uint32(prop->ID);                               // Internal Proposal ID
    data << uint32(0);                                      // Bosses killed - FIXME
    data << uint8(isSameDungeon);                           // Silent (show client window)
    data << uint8(groupSize);                               // Group size

    for (tGuidInfoSet::const_iterator itr = prop->NewGroupInfo->NewGroup.begin(); itr != prop->NewGroupInfo->NewGroup.end(); ++itr)
    {
        if (!(*itr)->PreGrouped)
        {
            Player* currPlr = sObjectMgr.GetPlayer((*itr)->Guid);
            uint32 groupId = 0;
            Group* grp = NULL;
            if (currPlr)
            {
                grp = currPlr->GetGroup();
                if (grp)
                    groupId = grp->GetId();
            }

            data << uint32((*itr)->ChosenRole);                     // Role
            data << uint8((*itr)->Guid == playerGuid);                    // Self player
            data << uint8(0);                               // Not in dungeon
            data << uint8(0);                               // Not same group
            tLfgResultMap::const_iterator resItr = prop->GuidResultMap.find((*itr)->Guid.GetCounter());
            if (resItr != prop->GuidResultMap.end())
            {
                data << uint8(true); // Answered
                data << uint8(resItr->second); // Accepted
            }
            else
            {
                data << uint8(false); // Answered
                data << uint8(false); // Accepted
            }
        }
        else
        {
            for (tMemberInfoMap::const_iterator guidItr = (*itr)->MemberInfo.begin(); guidItr != (*itr)->MemberInfo.end(); ++guidItr)
            {
                Player* grpMember = sObjectMgr.GetPlayer(guidItr->second.Guid);
                uint32 groupId = (*itr)->GroupId;
                Group* grp = NULL;
                if (grpMember)
                {
                    grp = grpMember->GetGroup();
                    if (grp)
                        groupId = grp->GetId();
                }

                data << uint32(guidItr->second.ChosenRole);                     // Role
                data << uint8(guidItr->second.Guid == playerGuid);                    // Self player
                data << uint8(0);  // In dungeon (silent)
                data << uint8(groupId == playerGroupId); // Same Group than player
                tLfgResultMap::const_iterator resItr = prop->GuidResultMap.find(guidItr->first);
                if (resItr != prop->GuidResultMap.end())
                {
                    data << uint8(true); // Answered
                    data << uint8(resItr->second); // Accepted
                }
                else
                {
                    data << uint8(false); // Answered
                    data << uint8(false); // Accepted
                }
            }
        }
    }
    SendPacket(&data);
}

void WorldSession::HandleLfgProposalResultOpcode(WorldPacket &recv_data)
{
    uint32 lfgPropId;                                       // Internal Proposal ID
    bool answer;                                           // Accept to join?
    ObjectGuid guid = _player->GetObjectGuid();
    recv_data >> lfgPropId;
    recv_data >> answer;

    sLog.outDebug("CMSG_LFG_PROPOSAL_RESULT [%u] proposal ID = %u,  %s!", guid.GetCounter(), lfgPropId, answer ? "Accepted" : "Refused");
    sLFGMgr.LfgEvent(LFG_EVENT_PROPOSAL_RESULT, guid, lfgPropId, answer);
}

void WorldSession::SendLfgTeleportError(uint8 err)
{
    sLog.outDebug("SMSG_LFG_TELEPORT_DENIED [%u] reason: %u", _player->GetObjectGuid().GetCounter(), err);
    WorldPacket data(SMSG_LFG_TELEPORT_DENIED, 4);
    data << uint32(err);                                    // Error
    SendPacket(&data);
}

void WorldSession::HandleLfgTeleportOpcode(WorldPacket &recv_data)
{
    bool out;
    recv_data >> out;

    sLog.outDebug("CMSG_LFG_TELEPORT [%u] out: %u", _player->GetObjectGuid().GetCounter(), uint32(out));
    sLFGMgr.LfgEvent(LFG_EVENT_TELEPORT_PLAYER, _player->GetObjectGuid(), out);
}

void WorldSession::HandleLfgSetRolesOpcode(WorldPacket &recv_data)
{
    uint8 roles;
    recv_data >> roles;                                     // Player Group Roles
    sLog.outDebug("CMSG_LFG_SET_ROLES [%u] Roles: %u", _player->GetObjectGuid().GetCounter(), roles);
    sLFGMgr.LfgEvent(LFG_EVENT_SET_ROLES, _player->GetObjectGuid(), uint32(roles));
}

void WorldSession::SendLfgRoleChosen(ObjectGuid guid, uint8 roles)
{
    sLog.outDebug("SMSG_LFG_ROLE_CHOSEN [%u] guid: [%u] roles: %u", _player->GetObjectGuid().GetCounter(), guid.GetCounter(), roles);

    WorldPacket data(SMSG_ROLE_CHOSEN, 8 + 1 + 4);
    data << guid;                                           // Guid
    data << uint8(roles > 0);                               // Ready
    data << uint32(roles);                                  // Roles
    SendPacket(&data);
}

void WorldSession::SendLfgRoleCheckUpdate(s_LfgRolesCheck const* roleCheck)
{
    Player* plr;
    uint8 roles;
    uint32 grpSize = roleCheck->GuidInfo->MemberInfo.size(); //grp size
    sLog.outDebug("SMSG_LFG_ROLE_CHECK_UPDATE [%u], result=%u", _player->GetObjectGuid().GetCounter(), uint32(roleCheck->result));
    WorldPacket data(SMSG_LFG_ROLE_CHECK_UPDATE);//, 4 + 1 + 1 + roleCheck->dungeons->size() * 4 + 1 + grpSize * (8 + 1 + 4 + 1));

    data << uint32(roleCheck->result);                     // Check result
    data << uint8(roleCheck->result == LFG_ROLECHECK_INITIALITING);
    data << uint8(roleCheck->dungeons->size());             // Number of dungeons
    if (roleCheck->dungeons->size())
    {
        for (tLfgDungeonSet::iterator it = roleCheck->dungeons->begin(); it != roleCheck->dungeons->end(); ++it)
        {
            LFGDungeonEntry const* dungeon = sLFGDungeonStore.LookupEntry(*it);
            data << uint32(dungeon ? dungeon->Entry() : 0); // Dungeon
        }
    }

    data << uint8(grpSize);                // Players in group
    if (grpSize)
    {
        // Leader info MUST be sent 1st :S
        tLfgRolesMap::const_iterator roleItr = roleCheck->RolesInfo.find(roleCheck->GuidInfo->Guid.GetCounter());
        if (roleItr != roleCheck->RolesInfo.end())
            roles = roleItr->second.Roles;
        else
            roles = LFG_NO_ROLE;
        data << roleCheck->GuidInfo->Guid;                                       // Guid
        data << uint8(roles != LFG_NO_ROLE);                           // Ready
        data << uint32(roles);             // Roles
        plr = sObjectMgr.GetPlayer(roleCheck->GuidInfo->Guid);
        data << uint8(plr ? plr->getLevel() : 0);           // Level

        for (tMemberInfoMap::const_iterator guidItr = roleCheck->GuidInfo->MemberInfo.begin(); guidItr != roleCheck->GuidInfo->MemberInfo.end(); ++guidItr)
            if (guidItr->second.Guid != roleCheck->GuidInfo->Guid)
            {
                tLfgRolesMap::const_iterator roleItr = roleCheck->RolesInfo.find(guidItr->first);
                if (roleItr != roleCheck->RolesInfo.end())
                    roles = roleItr->second.Roles;
                else
                    roles = LFG_NO_ROLE;
                data << guidItr->second.Guid;                                   // Guid
                data << uint8(roles != LFG_NO_ROLE);                       // Ready
                data << uint32(roles);                          // Roles
                Player*  plrg = sObjectMgr.GetPlayer(guidItr->second.Guid);
                data << uint8(plrg ? plrg->getLevel() : 0);       // Level
            }
    }
    data.hexlike();
    SendPacket(&data);
}

void WorldSession::SendLfgKickStatus(s_LfgKickInfo* kickInfo)
{
    uint32 timeLeft = uint8((kickInfo->ExpireTime - time(NULL)) / 1000);

    ObjectGuid guid = _player->GetObjectGuid();
    tLfgResultMap::const_iterator itr = kickInfo->Votes.find(guid.GetCounter());
    bool voted = false;
    bool vote = false;
    if (itr != kickInfo->Votes.end())
    {
        voted = true;
        vote = itr->second;
    }

    sLog.outDebug("SMSG_LFG_BOOT_PLAYER [%u] inProgress: %u - didVote: %u - agree: %u - victim: [%u] - reason %s",
        guid.GetCounter(), uint8(kickInfo->InProgress), uint8(kickInfo->Votes.size()), uint8(kickInfo->Accepted), kickInfo->VictimGuid.GetCounter(), kickInfo->Reason.c_str());

    WorldPacket data(SMSG_LFG_BOOT_PLAYER, 1 + 1 + 1 + 8 + 4 + 4 + 4 + 4 + kickInfo->Reason.length());
    data << uint8(kickInfo->InProgress);                    // Vote in progress
    data << uint8(voted);                                   // Did Vote
    data << uint8(vote);                                    // Agree
    data << kickInfo->VictimGuid;                           // Victim GUID
    data << uint32(kickInfo->Votes.size());                 // Total Votes
    data << uint32(kickInfo->Accepted);                     // Agree Count
    data << uint32(timeLeft);                               // Time Left
    data << uint32(LFG_KICK_VOTE_NEEDED);                   // Needed Votes
    data << kickInfo->Reason.c_str();                       // Kick reason
    SendPacket(&data);
}

void WorldSession::HandleLfgSetKickVoteOpcode(WorldPacket &recv_data)
{
    bool vote;                                             // Agree to kick player
    recv_data >> vote;

    sLog.outDebug("CMSG_LFG_SET_BOOT_VOTE [%u] voted : %s", _player->GetObjectGuid(), vote ? "Accept" : "Refuse");
    sLFGMgr.LfgEvent(LFG_EVENT_KICK_VOTE, _player->GetObjectGuid(), vote);
}

void WorldSession::SendLfgDisabled()
{
    sLog.outDebug("SMSG_LFG_DISABLED [" UI64FMTD "]", _player->GetObjectGuid().GetCounter());
    WorldPacket data(SMSG_LFG_DISABLED, 0);
    SendPacket(&data);
}

void WorldSession::HandleSetLfgCommentOpcode( WorldPacket & recv_data )
{
    DEBUG_LOG("CMSG_SET_LFG_COMMENT");
    //recv_data.hexlike();

    std::string comment;
    recv_data >> comment;
    DEBUG_LOG("LFG comment %s", comment.c_str());

    //_player->m_lookingForGroup.comment = comment;
}
