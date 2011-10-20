#include "LFGMgr.h"
#include "World.h"

#include "Policies/SingletonImp.h"
#include <map>

INSTANTIATE_SINGLETON_1( LFGMgr );



//=============================================================================
//=============== ThreadSafe class container for ActionMsg ====================
//=============================================================================
void ThreadSafeActionList::Add(s_LfgActionMsg* pMess)
{
    m_Mutex.acquire();
    m_List.push_back(pMess);
    m_Mutex.release();
}

s_LfgActionMsg* ThreadSafeActionList::GetMsg()
{
    s_LfgActionMsg* result = NULL;

    if ((m_List.empty()) || (m_CurrItr == m_List.end()))
    {
        return NULL;
    }

    //m_Mutex->acquire();
    //m_Mutex->release();
    m_CurrItr = m_NextItr;
    ++m_NextItr;
    return *m_CurrItr;
}

void ThreadSafeActionList::RemoveCurrent()
{
   if (m_CurrItr == m_List.end())
       return;
   if (m_CurrItr == m_List.begin())
   {
       m_Mutex.acquire();
       m_List.erase(m_CurrItr);
       m_Mutex.release();
       m_CurrItr = m_List.begin();
       m_NextItr = m_List.begin();
       if (m_CurrItr == m_List.end())
           return;
       ++m_NextItr;
       return;
   }
   else
   {
       m_Mutex.acquire();
       m_List.erase(m_CurrItr);
       m_CurrItr = m_NextItr;
       m_Mutex.release();
   }
}

s_LfgActionMsg* ThreadSafeActionList::GetFirstMsg()
{
   m_CurrItr = m_List.begin();
   m_NextItr = m_List.begin();
   if (m_CurrItr == m_List.end())
       return NULL;
   ++m_NextItr;
   return *m_CurrItr;
}

void ThreadSafeActionList::Remove(e_LfgActionType action, ObjectGuid guid)
{
    for (tLfgActionMsgList::iterator itr=m_List.begin(); itr != m_List.end(); ++itr)
    {
        if (((*itr)->Guid == guid) && ((*itr)->Action == action))
        {
           if (itr == m_CurrItr)
               return;
           if (itr == m_NextItr)
                ++m_NextItr;
            m_Mutex.acquire();
            m_List.erase(itr);
            m_Mutex.release();
            break;
        }
    }
}

//===============================================================================================
// LFGMgr class is used to encapsulate complete LFG operation
//===============================================================================================
// Need LFGQMgr class.
//===============================================================================================
LFGMgr::LFGMgr() : m_ProposalId (0)
{
}

LFGMgr::~LFGMgr()
{
}

void LFGMgr::Initialize()
{
    if (!sWorld.getConfig(CONFIG_BOOL_DUNGEON_FINDER_ENABLE))
        return;

    LFGQMgr::Initialize();

    // Start of the main thread
    int code = ACE_Thread_Manager::instance()->spawn((ACE_THR_FUNC) LFGMgr::UpdateThread , this);
    if (code == -1)
        sLog.outError("LFGMgr::Initialize> Error spawning LFG thread");
}

// Main heart of LFG system is started in LFGMgr::Initialize()
void* LFGMgr::UpdateThread(void* arg)
{
    uint32 prevTime = WorldTimer::getMSTime();

    // Yes using type cast is not so elegant
    LFGMgr * pThis = (LFGMgr*)arg;
    Team currTeam = ALLIANCE;

    // how to get 100ms sleep...
    ACE_Time_Value tv;
    tv.set_msec(100);
    // need to add conditional to close correctly this thread
    uint32  Timer1 = 0;
    uint32  Timer2 = 0;
    while (1)
    {
        // 100ms sleep.
        ACE_OS::sleep(tv);
        uint32 currTime = WorldTimer::getMSTime();
        uint32 diff = WorldTimer::getMSTimeDiff(WorldTimer::getMSTime(), prevTime);
        prevTime=currTime;
        // most important part, ProcessActionMsg(diff) handle all operation about lfg queue
        pThis->DoProcessActionMsg(diff);
        
        Timer1 += diff;
        Timer2 += diff;
        if (Timer1 > LFG_TIMER_UPDATE_CLIENT_INFO)
        {
            Timer1 = 0;
            // Verify proposal timers
            if (pThis->m_ProposalMap.size()>0)
                pThis->DoDeleteExpiredProposal();


            // Verify roles check timers
            if (pThis->m_RolesCheckMap.size()>0)
                pThis->DoDeleteExpiredRolesCheck();

            // Verify vote kiks timers
            if (pThis->m_KicksMap.size()>0)
                pThis->DoDeleteExpiredVoteKick();
        }

        if (Timer2 < LFG_TIMER_UPDATE_QUEUE)
            continue;
        Timer2 = 0;
        // FindGroup will check if there is any possibility to create new group from actual queued members
        // it work one team at a time separed by LFG_UPDATE_QUEUE_TIMER (7 sec by default)
        //pThis->DoFindGroup(currTeam);
        // send to clients actual status of the queue
        pThis->DoUpdateQueuedStatus(currTeam);

        sLog.outDebug("LFG::UpdateThread > %s Player queue size = %u", (currTeam == HORDE)?"Horde":"Alliance", pThis->GetLFGSize(currTeam));
        sLog.outDebug("LFG::UpdateThread > In use Proposal:%u, CheckRole:%u, VoteKiks:%u", pThis->m_ProposalMap.size(), pThis->m_RolesCheckMap.size(), pThis->m_KicksMap.size());
        pThis->ShowQueueInfo(currTeam);
        if (currTeam == HORDE)
            currTeam = ALLIANCE;
        else
            currTeam = HORDE;
    }
}

void LFGMgr::LfgJoin(Player* plr, pLfgDungeonSet DungeonSet, uint8 Roles)
{
    s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_ADD_PLAYER_TO_QUEUE, plr, DungeonSet, uint32(Roles));
    m_ActionMsgList.Add(actionMsg);
}

// main event entry for LFG system, most of event simply create an action
// this way we release asap the focus and let thread process this even
void LFGMgr::LfgEvent(e_LfgEventType eventType, ObjectGuid guid, ...)
{
    va_list Arguments;
    va_start(Arguments, guid);

    switch (eventType)
    {
    case LFG_EVENT_PLAYER_LOG_ON :
        {
            if (!IsInQueue(guid))
                return;

            Player* plr = sObjectMgr.GetPlayer(guid);
            Group* grp = (plr ) ? plr->GetGroup() : NULL;

            if ((grp) && (grp->IsLFGGroup()))
                return;

            sLog.outDebug("LFGMgr::LfgEvent > Player [%u] Log On", guid.GetCounter());
            m_ActionMsgList.Remove(LFG_ACTION_REMOVE_PLAYER_FROM_QUEUE_DELAYED, guid);
            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_REVALID_QUEUE, guid);
            m_ActionMsgList.Add(actionMsg);
            break;
        }

    case LFG_EVENT_PLAYER_LOG_OFF :
        {
            if (!IsInQueue(guid))
                return;
            s_GuidInfo const* guidInfo = GetGuidInfos(guid);
            switch (guidInfo->Status)
            {
            case LFG_PLAYER_STATUS_IN_QUEUE :
            {
                sLog.outDebug("LFGMgr::LfgEvent > Player [%u] Log Off start timer!", guid.GetCounter());
                s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_REMOVE_PLAYER_FROM_QUEUE_DELAYED, guid);
                m_ActionMsgList.Add(actionMsg);
                break;
            }
            default :
            {
                sLog.outDebug("LFGMgr::LfgEvent > Player [%u] Loged off so leave queue", guid.GetCounter());
                s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_REMOVE_PLAYER_FROM_QUEUE_NOW, guid);
                m_ActionMsgList.Add(actionMsg);
            }
            }
            break;
        }

    case LFG_EVENT_PLAYER_LEAVE_QUEUE :
        {
            sLog.outDebug("LFGMgr::LfgEvent > Player [%u] Leave", guid.GetCounter());
            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_REMOVE_PLAYER_FROM_QUEUE_NOW, guid);
            m_ActionMsgList.Add(actionMsg);
            break;
        }

    case LFG_EVENT_PLAYER_LEAVED_GROUP :
        {
            Player* plr = sObjectMgr.GetPlayer(guid);
            Group* grp = (plr ) ? plr->GetGroup() : NULL;

            if ((grp) && (grp->IsLFGGroup()))
            {
                // Replace player to his original position
                sLog.outDebug("GroupRemove> ");
                WorldLocation const* dungeonLocation = grp->GetLfgDestination();
                if ((!plr->GetMap()->IsDungeon()) || (dungeonLocation->mapid != plr->GetMapId()))
                    break;
                plr->TeleportTo(*grp->GetLfgOriginalPos(guid));
                break;
            }
            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_REMOVE_PLAYER_FROM_QUEUE_NOW, guid);
            m_ActionMsgList.Add(actionMsg);
            sLog.outDebug("LFGMgr::LfgEvent > Player [%u] Leaved his group so group is removed from queue. (plr is not null)", guid.GetCounter());
            break;
        }

    case LFG_EVENT_PROPOSAL_RESULT :
        {
            uint32 PropId = va_arg(Arguments, uint32);
            bool   result = va_arg(Arguments, bool);
            sLog.outDebug("LFGMgr::LfgEvent > Player [%u] Send proposal result -> %s", guid.GetCounter(), result?"Ok":"Refuse");
            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_UPDATE_PROPOSAL, guid, PropId, (uint32) result);
            m_ActionMsgList.Add(actionMsg);
            break;
        }

    case LFG_EVENT_AREA_TRIGGER :
        {
            uint32 MapId = va_arg(Arguments, uint32);

            Player* plr = sObjectMgr.GetPlayer(guid);
            Group* grp = (plr ) ? plr->GetGroup() : NULL;

            sLog.outDebug("LFGMgr::LfgEvent > AREA TRIGGER for Player [%u]", guid.GetCounter());

            if (plr->GetMap()->IsDungeon())
            {
                if (MapId != plr->GetMapId())
                    plr->TeleportTo(*grp->GetLfgOriginalPos(guid));
            }
            else
            {
                if (MapId == grp->GetLfgDestination()->mapid)
                {
                    plr->TeleportTo(*grp->GetLfgDestination());
                }
                else
                    sLog.outDebug("TODO> Send message to player> Your are not able to enter another dungeon while in LFG group");
            }
            break;
        }

    case LFG_EVENT_TELEPORT_PLAYER :
        {
            bool out = va_arg(Arguments, bool);
            sLog.outDebug("LFGMgr::LfgEvent > TELEPORT Player [%u] out = %s", guid.GetCounter(), out ? "true" : "false");
            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_TELEPORT_PLAYER, guid, (uint32) out);
            m_ActionMsgList.Add(actionMsg);
            break;
        }

    case LFG_EVENT_SET_ROLES :
        {
            Player* plr = sObjectMgr.GetPlayer(guid);
            Group* grp = (plr ) ? plr->GetGroup() : NULL;

            uint32 roles = va_arg(Arguments, uint32);

            if ((!plr) || (!grp))
            {
                sLog.outError("LFGMgr::LfgEvent > Failled to set roles for Player [%u], roles = %u. ", guid.GetCounter(), roles);
                break;
            }
            sLog.outDebug("LFGMgr::LfgEvent > Set roles for Player [%u], roles = %u", guid.GetCounter(), roles);
            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_SET_ROLESCHECK_ANSWER, guid, roles);
            m_ActionMsgList.Add(actionMsg);
            break;
        }

    case LFG_EVENT_INIT_KICK_VOTE :
        {
            ObjectGuid* victim = va_arg(Arguments, ObjectGuid*);
            std::string* reason = va_arg(Arguments, std::string*);

            sLog.outDebug("LFGMgr::LfgEvent > Init kick vote from Player [%u], victim = [%u]. ", guid.GetCounter(), victim->GetCounter());
            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_INIT_KICK_VOTE, guid, *victim, (*reason).c_str());
            m_ActionMsgList.Add(actionMsg);
            break;
        }

    case LFG_EVENT_KICK_VOTE :
        {
            Player* plr = sObjectMgr.GetPlayer(guid);
            Group* grp = (plr ) ? plr->GetGroup() : NULL;

            bool vote = va_arg(Arguments, bool);

            if ((!plr) || (!grp))
            {
                sLog.outError("LFGMgr::LfgEvent > Failled to set kick vote for Player [%u], vote = %s. ", guid.GetCounter(), vote ? "accept" : "refuse");
                break;
            }
            sLog.outDebug("LFGMgr::LfgEvent > Set vote for Player [%u], vote = %s. ", guid.GetCounter(), vote ? "accept" : "refuse");
            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_SET_VOTEKICK_ANSWER, guid, (uint32) vote);
            m_ActionMsgList.Add(actionMsg);
            break;
        }
    }
    va_end(Arguments);
}

//=============================================================================
//======= All of these method called by LFGThread (Start by 'Do') =============
//=============================================================================
// This method will make all interaction with LFGQ class.
void LFGMgr::DoProcessActionMsg(uint32 diff)
{
    s_LfgActionMsg* actionMsg = m_ActionMsgList.GetFirstMsg();
    bool deleteItr=false;
    while ( actionMsg )
    {
        Player * plr = HashMapHolder<Player>::Find(actionMsg->Guid);
        deleteItr=false;
        switch (actionMsg->Action)
        {
        case LFG_ACTION_ADD_PLAYER_TO_QUEUE :
        {
            e_LfgJoinResult result = AddPlayerToQueue(actionMsg->Plr, actionMsg->DungeonSet, actionMsg->Uint32Value);
            Group* grp = actionMsg->Plr->GetGroup();

            if (result != LFG_JOIN_OK)
            {
                actionMsg->Plr->GetSession()->SendLfgJoinResult(result, 0);
            }
            else
            {
                if (!grp)
                {
                    actionMsg->Plr->GetSession()->SendLfgJoinResult(result, 0);
                    SendLFGUpdate(actionMsg->Plr, LFG_UPDATETYPE_JOIN_PROPOSAL);
                }
                else
                    DoSendGroupRolesCheck(actionMsg->Plr, actionMsg->DungeonSet, actionMsg->Uint32Value);
                SetGuidStatus(actionMsg->Plr->GetObjectGuid(), LFG_PLAYER_STATUS_IN_QUEUE);
            }
            deleteItr=true;
            break;
        }

        case LFG_ACTION_REVALID_QUEUE :
        {
            if (!plr)
            {
                DoRemovePlayerFromQueue(actionMsg->Guid);
                deleteItr = true;
                break;
            }
            if (plr->IsInWorld())
            {
                plr->GetSession()->SendLfgJoinResult(LFG_JOIN_OK, 0);
                plr->GetSession()->SendLfgUpdatePlayer(LFG_UPDATETYPE_JOIN_PROPOSAL);
                if (!IsOnLine(actionMsg->Guid))
                    SetOnLineStatus(actionMsg->Guid, true);
                //EnableQueueStatsUpdate(plr->GetTeam());
                deleteItr=true;
            }
            break;
        }

        case LFG_ACTION_REMOVE_PLAYER_FROM_QUEUE_DELAYED :
        {
            if (actionMsg->ElapsedTime > LFG_TIMER_DELAYED_REMOVE_LOG_OFF)
            {
                sLog.outDebug("Timelaps=%u", actionMsg->ElapsedTime);
                DoRemovePlayerFromQueue(actionMsg->Guid);
                deleteItr=true;
            }
            else
                if (IsOnLine(actionMsg->Guid))
                    SetOnLineStatus(actionMsg->Guid, false);
                actionMsg->ElapsedTime += diff;
            break;
        }

        case LFG_ACTION_REMOVE_PLAYER_FROM_QUEUE_NOW :
        {
            DoRemovePlayerFromQueue(actionMsg->Guid);
            deleteItr=true;
            break;
        }

        case LFG_ACTION_SEND_PROPOSAL :
        {
            DoSendProposal(actionMsg->newGroupInfo);
            deleteItr=true;
            break;
        }

        case LFG_ACTION_UPDATE_PROPOSAL :
        {
            DoSetProposalAnswer(actionMsg->Guid, actionMsg->Uint32Value, (bool) actionMsg->Uint32Value2);
            deleteItr=true;
            break;
        }

        case LFG_ACTION_PROPOSAL_FAILLED :
        {
            DoCancelProposal(actionMsg->Uint32Value, actionMsg->Guid, LFG_UPDATETYPE_PROPOSAL_FAILED);
            deleteItr=true;
            break;
        }

        case LFG_ACTION_PROPOSAL_DECLINED :
        {
            DoCancelProposal(actionMsg->Uint32Value, actionMsg->Guid, LFG_UPDATETYPE_PROPOSAL_DECLINED);
            deleteItr=true;
            break;
        }

        case LFG_ACTION_CREATE_GROUP :
        {
            tLfgProposalMap::iterator propitr = m_ProposalMap.find(actionMsg->Uint32Value);
            if (propitr!=m_ProposalMap.end())
            {
                DoCreateGroup(&propitr->second);
                m_ProposalMap.erase(propitr);
            }
            deleteItr=true;
            break;
        }

        case LFG_ACTION_SET_ROLESCHECK_ANSWER :
        {
            DoSetRolesCheckAnswer(actionMsg->Guid, actionMsg->Uint32Value);
            deleteItr = true;
            break;
        }
        case LFG_ACTION_INIT_KICK_VOTE :
        {
            DoInitVoteKick(actionMsg->Guid, actionMsg->Guid2, actionMsg->Reason);
            deleteItr = true;
            break;
        }
        case LFG_ACTION_SET_VOTEKICK_ANSWER :
        {
            DoSetKickVote(actionMsg->Guid, bool(actionMsg->Uint32Value));
            deleteItr = true;
            break;
        }
        case LFG_ACTION_TELEPORT_PLAYER :
        {
            if (actionMsg->ElapsedTime >= 0000)
            {
                    sLog.outDebug("LFGMgr::ProcessActionMsg > Enterring TELEPORT message processing for [%u]", actionMsg->Guid.GetCounter());

                    if (plr)
                    {
                        Group* grp = plr->GetGroup();
                        if ((grp) && (grp->IsLFGGroup(actionMsg->Guid)))
                        {

                            if (!actionMsg->Uint32Value2)
                            {
                                if (actionMsg->Uint32Value)
                                    DoTeleportPlayer(plr, grp->GetLfgOriginalPos(actionMsg->Guid));
                                else
                                {
                                    DoTeleportPlayer(plr, grp->GetLfgDestination());
                                    actionMsg->Uint32Value2 = 1;
                                    sLog.outDebug("LFGMgr::ProcessActionMsg > TELEPORT Player [%u] out = %s", actionMsg->Guid.GetCounter(), (actionMsg->Uint32Value) ? "true" : "false");
                                    sLog.outDebug("LFGMgr::ProcessActionMsg > Player [%u] %s the leader", actionMsg->Guid.GetCounter(), (grp->IsLeader(actionMsg->Guid)) ? "is" : "is not");
                                    actionMsg->ElapsedTime = 0;
                                    break;
                                }
                            }
                            else
                            {
                                 if (!plr->IsBeingTeleported())
                                 {
                                     if (grp->CanHaveLuckOfTheDraw())
                                        plr->CastSpell(plr, LFG_SPELL_LUCK_OF_THE_DRAW, true);
                                     plr->CastSpell(plr, LFG_SPELL_DUNGEON_COOLDOWN, true);
                                     sLog.outDebug("LFGMgr::ProcessActionMsg > Player [%u] received LFG_SPELL_DUNGEON_COOLDOWN", actionMsg->Guid.GetCounter());
                                 }
                                 else
                                 {
                                     actionMsg->ElapsedTime = 0;
                                     break;
                                 }
                            }

                            //grp->SendUpdate();
                        }
                        deleteItr=true;
                    }
                    else
                    {
                        sLog.outError("LFGMgr::ProcessActionMsg > ERROR TELEPORTING> Player [%u] out = %s", actionMsg->Guid.GetCounter(), (actionMsg->Uint32Value) ? "true" : "false");
                        deleteItr=true;
                    }

            }
            else
                actionMsg->ElapsedTime += diff;
            break;
        }

        case LFG_ACTION_DELAYED_GROUP_UPDATE :
        {
            if (actionMsg->ElapsedTime > 5000)
            {
                if (Group* grp = sObjectMgr.GetGroupById(actionMsg->Uint32Value))
                {
                    if (grp)
                    {
                        sLog.outDebug("LFGMgr::ProcessActionMsg >>>>>>>>>>>>>>>> SENDED DELAYED UPDATE <<<<<<<<<<<<<<<<<<<<<<<<<");
                        grp->SendUpdate();
                    }
                }
                deleteItr=true;
            }
            else
                actionMsg->ElapsedTime += diff;
            break;
        }
        }

        if (deleteItr)
            m_ActionMsgList.RemoveCurrent();
        actionMsg = m_ActionMsgList.GetMsg();
    }
}

void LFGMgr::DoSendProposal(pLfgNewGroup newGroupInfo)
{
    uint32 newPropId = GetNewProposalID();
    m_ProposalMap[newPropId].ID = newPropId; // to be sure this element is created
    m_ProposalMap[newPropId].NewGroupInfo=newGroupInfo;
    m_ProposalMap[newPropId].State = LFG_PROPOSAL_INITIATING;
    m_ProposalMap[newPropId].ProposalExpire = time(NULL) + LFG_TIMELAPS_PROPOSAL;

    SetGuidStatus(&newGroupInfo->NewGroup,LFG_PLAYER_STATUS_IN_PROPOSAL, newPropId);

    for (tGuidInfoSet::iterator playitr = newGroupInfo->NewGroup.begin(); playitr != newGroupInfo->NewGroup.end(); ++playitr)
    {
        if (!(*playitr)->PreGrouped)
        {
            Player* grpMember = sObjectMgr.GetPlayer((*playitr)->Guid);
            if (grpMember)
            {
                SendLFGUpdate(grpMember, LFG_UPDATETYPE_PROPOSAL_BEGIN);
                grpMember->GetSession()->SendUpdateProposal(&m_ProposalMap[newPropId]);
            }
        }
        else
        {
            for (tMemberInfoMap::const_iterator guidItr = (*playitr)->MemberInfo.begin(); guidItr != (*playitr)->MemberInfo.end(); ++guidItr)
            {
                Player* grpMember = sObjectMgr.GetPlayer(guidItr->second.Guid);
                if (grpMember)
                {
                    SendLFGUpdate(grpMember, LFG_UPDATETYPE_PROPOSAL_BEGIN);
                    grpMember->GetSession()->SendUpdateProposal(&m_ProposalMap[newPropId]);
                }
            }
        }
    }
}

void LFGMgr::DoSetProposalAnswer(ObjectGuid guid, uint32 propId, bool answer)
{
    tLfgProposalMap::iterator propitr = m_ProposalMap.find(propId);
    if (propitr!=m_ProposalMap.end())
    {
        s_LfgProposal* prop = &propitr->second;
        if (!answer)
        {
            //prop->State = LFG_PROPOSAL_DECLINED;
            DoCancelProposal(prop->ID, guid, LFG_UPDATETYPE_PROPOSAL_DECLINED);
            m_ProposalMap.erase(propitr);
            return;
        }

        prop->GuidResultMap[guid.GetCounter()] = answer;

        if (prop->GuidResultMap.size() != LFG_GROUP_SIZE)
        {

            for (tGuidInfoSet::iterator playitr = prop->NewGroupInfo->NewGroup.begin(); playitr != prop->NewGroupInfo->NewGroup.end(); ++playitr)
            {
                if (!(*playitr)->PreGrouped)
                {
                    Player* grpMember = sObjectMgr.GetPlayer((*playitr)->Guid);
                    if (grpMember)
                        grpMember->GetSession()->SendUpdateProposal(prop);
                }
                else
                {
                    for (tMemberInfoMap::const_iterator guidItr = (*playitr)->MemberInfo.begin(); guidItr != (*playitr)->MemberInfo.end(); ++guidItr)
                    {
                        Player* grpMember = sObjectMgr.GetPlayer(guidItr->second.Guid);
                        if (grpMember)
                            grpMember->GetSession()->SendUpdateProposal(prop);
                    }
                }
            }
        }
        else
        {
            prop->State = LFG_PROPOSAL_SUCCESS;

            tGuidInfoList memberList;

            for (tGuidInfoSet::const_iterator itr = prop->NewGroupInfo->NewGroup.begin(); itr != prop->NewGroupInfo->NewGroup.end(); ++itr)
            {
                if (!(*itr)->PreGrouped)
                {
                    Player* grpMember = sObjectMgr.GetPlayer((*itr)->Guid);
                    if (grpMember)
                    {
                        grpMember->GetSession()->SendUpdateProposal(prop);
                        SendLFGUpdate(grpMember, LFG_UPDATETYPE_GROUP_FOUND);
                    }
                }
                else
                {
                    for (tMemberInfoMap::const_iterator guidItr = (*itr)->MemberInfo.begin(); guidItr != (*itr)->MemberInfo.end(); ++guidItr)
                    {
                        Player* grpMember = sObjectMgr.GetPlayer(guidItr->second.Guid);
                        if (grpMember)
                        {
                            grpMember->GetSession()->SendUpdateProposal(prop);
                            SendLFGUpdate(grpMember,LFG_UPDATETYPE_GROUP_FOUND);
                        }
                    }
                }
            }

            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_CREATE_GROUP, prop->ID);
            m_ActionMsgList.Add(actionMsg);
        }
    }
}

void LFGMgr::DoCancelProposal(uint32 proposalId, ObjectGuid senderGuid, e_LfgUpdateType raison)
{
    Player* senderPlr = sObjectMgr.GetPlayer(senderGuid);
    tLfgProposalMap::iterator propitr = m_ProposalMap.find(proposalId);
    if (propitr!=m_ProposalMap.end())
    {
        s_LfgProposal* prop = &propitr->second;
        ObjectGuid* senderLeader = NULL;
        s_GuidInfo const* guidInfos = GetGuidInfos(senderGuid);
        if (!guidInfos)
        {
            sLog.outError("DoCancelProposal> Error cannot retrieve queue guid info of [%u]", senderGuid.GetCounter());
            return;
        }

        bool Reseted = false;
        // Update status of all other player
        for (tGuidInfoSet::const_iterator itr = prop->NewGroupInfo->NewGroup.begin(); itr != prop->NewGroupInfo->NewGroup.end(); ++itr)
        {
            if (!(*itr)->PreGrouped)
            {
                Player* grpMember = sObjectMgr.GetPlayer((*itr)->Guid);
                if (grpMember)
                {
                    grpMember->GetSession()->SendUpdateProposal(prop);
                    if ((*itr)->Guid != senderGuid)
                    {
                        SendLFGUpdate(grpMember, LFG_UPDATETYPE_ADDED_TO_QUEUE);
                        SetGuidStatus((*itr)->Guid, LFG_PLAYER_STATUS_IN_QUEUE, 0);
                    }
                    else
                        SendLFGUpdate(grpMember, LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
                }
            }
            else
            {
                if ((*itr)->Guid != guidInfos->Guid)
                {
                    for (tMemberInfoMap::const_iterator guidItr = (*itr)->MemberInfo.begin(); guidItr != (*itr)->MemberInfo.end(); ++guidItr)
                    {
                        Player* grpMember = sObjectMgr.GetPlayer(guidItr->second.Guid);
                        if (grpMember)
                        {
                            grpMember->GetSession()->SendUpdateProposal(prop);
                            SendLFGUpdate(grpMember,LFG_UPDATETYPE_ADDED_TO_QUEUE);
                            SetGuidStatus((*itr)->Guid, LFG_PLAYER_STATUS_IN_QUEUE, 0);
                        }
                    }
                }
                else
                {
                    for (tMemberInfoMap::const_iterator guidItr = (*itr)->MemberInfo.begin(); guidItr != (*itr)->MemberInfo.end(); ++guidItr)
                    {
                        Player* grpMember = sObjectMgr.GetPlayer(guidItr->second.Guid);
                        if (grpMember)
                        {
                            grpMember->GetSession()->SendUpdateProposal(prop);
                            SendLFGUpdate(grpMember, LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
                        }
                    }
                }
            }
        }
        RemovePlayerFromQueue(senderGuid);
    }
}

void LFGMgr::DoSendGroupRolesCheck(Player* plr, pLfgDungeonSet dungeonSet, uint32 Roles)
{
    if (!plr)
        return;
    Group* grp = plr->GetGroup();
    if (!grp)
        return;
    ObjectGuid guid = plr->GetObjectGuid();

    sLog.outDebug("DoSendGroupRolesCheck> Send roles check from guid [%u] with group ID = %u", guid.GetCounter(), grp->GetId());
    s_LfgRolesCheck rolesCheck;
    rolesCheck.LeaderGuid = guid;
    rolesCheck.GuidInfo = GetGuidInfos(guid);
    rolesCheck.dungeons = dungeonSet;
    rolesCheck.result = LFG_ROLECHECK_INITIALITING;
    rolesCheck.expireTime = time(NULL) + LFG_TIMELAPS_ROLECHECK;
    rolesCheck.RolesInfo[guid.GetCounter()].Roles = Roles;
    m_RolesCheckMap[grp->GetId()] = rolesCheck;
    SetGuidStatus(guid, LFG_PLAYER_STATUS_IN_ROLECHECK, grp->GetId());
    Player* plrg = NULL;
    for (tMemberInfoMap::const_iterator guidItr = rolesCheck.GuidInfo->MemberInfo.begin(); guidItr != rolesCheck.GuidInfo->MemberInfo.end(); ++guidItr)
    {
        plrg = sObjectMgr.GetPlayer(guidItr->second.Guid);
        if (!plrg)
            continue;
        plrg->GetSession()->SendLfgUpdateParty(LFG_UPDATETYPE_CLEAR_LOCK_LIST);
        plrg->GetSession()->SendLfgUpdateParty(LFG_UPDATETYPE_JOIN_PROPOSAL, dungeonSet);
        //if (plrg->GetObjectGuid() == guid)
            plrg->GetSession()->SendLfgRoleCheckUpdate(&rolesCheck);
    }
}

void LFGMgr::DoSetRolesCheckAnswer(ObjectGuid guid, uint32 roles)
{
    Player* plr = sObjectMgr.GetPlayer(guid);
    if (!plr)
        return;
    Group* grp = plr->GetGroup();
    if (!grp)
        return;

    tRolesCheckMap::iterator rolesCheckItr = m_RolesCheckMap.find(grp->GetId());
    if (rolesCheckItr == m_RolesCheckMap.end())
    {
        sLog.outDebug("LFGMgr::DoSetRolesCheckAnswer > Set roles failled! GroupId not found.");
        sLog.outDebug("LFGMgr::DoSetRolesCheckAnswer > RolesCheck Size = %u, Group ID = %u", m_RolesCheckMap.size(), grp->GetId());
        return;
    }
    s_LfgRolesCheck* rolesCheck = &rolesCheckItr->second;
    s_GuidInfo const* guidInfo = GetGuidInfos(rolesCheck->LeaderGuid);
    if ((!guidInfo) || (roles==LFG_NO_ROLE))
    {
        DoCancelRoleCheck(rolesCheck);
        m_RolesCheckMap.erase(rolesCheckItr);
        return;
    }

    rolesCheck->RolesInfo[guid.GetCounter()].Roles = roles;
    for (GroupReference* memberItr = grp->GetFirstMember(); memberItr != NULL; memberItr = memberItr->next())
    {
        Player* plr1 = memberItr->getSource();
        if (!plr1)
            continue;
        if (plr1->GetObjectGuid() != guid)
            plr1->GetSession()->SendLfgRoleChosen(guid, roles);
    }

    uint32 memberCount = 0;
    tLfgRolesList rolesList;
    for (tLfgRolesMap::const_iterator roleItr = rolesCheck->RolesInfo.begin(); roleItr != rolesCheck->RolesInfo.end(); ++roleItr)
    {
        if (roleItr->second.Roles != LFG_NO_ROLE)
        {
            rolesList.push_back(roleItr->second.Roles);
            ++memberCount;
        }
    }

    sLog.outDebug("answered = %u, membercount=%u", memberCount, guidInfo->MemberInfo.size());
    if (memberCount == guidInfo->MemberInfo.size())
    {
        bool addedToQueue = false;
        if (CheckValidRoles(rolesList))
        {
            rolesCheck->result = LFG_ROLECHECK_FINISHED;
            tLfgRolesMap::const_iterator roleItr = rolesCheck->RolesInfo.begin();
            while (roleItr != rolesCheck->RolesInfo.end())
            {
                sLog.outDebug("role for [%u] = %u", roleItr->first, roleItr->second.Roles);
                ++roleItr;
            }

            addedToQueue = true;
            SetGroupRoles(*rolesCheck);
        }
        else
        {
            rolesCheck->result = LFG_ROLECHECK_WRONG_ROLES;
            RemovePlayerFromQueue(rolesCheck->LeaderGuid);
        }

        for (tMemberInfoMap::const_iterator guidItr = guidInfo->MemberInfo.begin(); guidItr != guidInfo->MemberInfo.end(); ++guidItr)
        {
            Player* plr = sObjectMgr.GetPlayer(guidItr->second.Guid);
            if (!plr)
                continue;

            ObjectGuid guid = plr->GetObjectGuid();
            plr->GetSession()->SendLfgRoleCheckUpdate(rolesCheck);

            if ((rolesCheck->result == LFG_ROLECHECK_FINISHED) && (addedToQueue))
            {
                plr->GetSession()->SendLfgRoleCheckUpdate(rolesCheck);
                plr->GetSession()->SendLfgUpdateParty(LFG_UPDATETYPE_ADDED_TO_QUEUE);

            }
            else
            {
                plr->GetSession()->SendLfgRoleCheckUpdate(rolesCheck);
                plr->GetSession()->SendLfgJoinResult(LFG_JOIN_FAILED, rolesCheck->result);
                plr->GetSession()->SendLfgUpdateParty(LFG_UPDATETYPE_ROLECHECK_FAILED);
            }
        }
        if (addedToQueue)
            m_RolesCheckMap.erase(rolesCheckItr);
    }
}

void LFGMgr::DoCancelRoleCheck(s_LfgRolesCheck* rolesCheck)
{
    s_GuidInfo const* guidInfo = GetGuidInfos(rolesCheck->LeaderGuid);
    if (!guidInfo)
        return;
    rolesCheck->result = LFG_ROLECHECK_NO_ROLE;
    Player* plr;
    ObjectGuid guid;
    for (tMemberInfoMap::const_iterator guidItr = guidInfo->MemberInfo.begin(); guidItr != guidInfo->MemberInfo.end(); ++guidItr)
    {
        plr = sObjectMgr.GetPlayer(guidItr->second.Guid);
        if (!plr)
            continue;
        guid = plr->GetObjectGuid();
        plr->GetSession()->SendLfgRoleCheckUpdate(rolesCheck);
        if (guid==guidInfo->Guid)
        {
            plr->GetSession()->SendLfgJoinResult(LFG_JOIN_FAILED, LFG_ROLECHECK_NO_ROLE);
            SendLFGUpdate(plr, LFG_UPDATETYPE_ROLECHECK_FAILED);
        }
    }
    RemovePlayerFromQueue(guidInfo->Guid);
}

bool LFGMgr::DoRemovePlayerFromQueue(ObjectGuid guid, e_LfgUpdateType raison)
{
    s_GuidInfo const* guidInfo = GetGuidInfos(guid);
    if (!guidInfo)
    {
        sLog.outDebug("LFGMgr::DoRemovePlayerFromQueue> Cannot found guid info for [%u]", guid.GetCounter());
        return false;
    }

    switch (guidInfo->Status)
    {
    case LFG_PLAYER_STATUS_IN_ROLECHECK :
    {
        sLog.outDebug("LFGMgr::DoRemovePlayerFromQueue> [%u] In Rolecheck with id = %u.MapSize=%u", guid.GetCounter(), guidInfo->StatusId, m_RolesCheckMap.size());
        tRolesCheckMap::iterator rolesCheckItr = m_RolesCheckMap.find(guidInfo->StatusId);
        if (rolesCheckItr != m_RolesCheckMap.end())
        {
            DoCancelRoleCheck(&rolesCheckItr->second);
            m_RolesCheckMap.erase(rolesCheckItr);
        }
        break;
    }
    case LFG_PLAYER_STATUS_IN_PROPOSAL :
    {
        sLog.outDebug("LFGMgr::DoRemovePlayerFromQueue> [%u] In Proposal with id = %u ", guid.GetCounter(), guidInfo->StatusId);
        DoCancelProposal(guidInfo->StatusId, guid, LFG_UPDATETYPE_PROPOSAL_FAILED);
        break;
    }
    default :
        sLog.outDebug("LFGMgr::DoRemovePlayerFromQueue> [%u] In Default with id = %u ", guid.GetCounter(), guidInfo->StatusId);
        if (guidInfo->PreGrouped)
        {
            for (tMemberInfoMap::const_iterator guidItr = guidInfo->MemberInfo.begin(); guidItr != guidInfo->MemberInfo.end(); ++guidItr)
            {
                SendLFGUpdate(guidItr->second.Guid, LFG_UPDATETYPE_REMOVED_FROM_QUEUE);

            }
            RemovePlayerFromQueue(guid);
        }
        else
        {
            RemovePlayerFromQueue(guid);
            SendLFGUpdate(guid, LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
        }
    }
    return true;
}

void LFGMgr::DoSetKickVote(ObjectGuid const& guid, bool vote)
{
    Player* plr = sObjectMgr.GetPlayer(guid);
    if (!plr)
        return;
    Group* grp = plr->GetGroup();
    if (!grp)
        return;

    tLfgKicksMap::iterator itr = m_KicksMap.find(grp->GetId());
    if (itr == m_KicksMap.end())
        return;
    // Member already voted?
    tLfgResultMap::iterator voteItr = itr->second.Votes.find(guid.GetCounter());
    if (voteItr != itr->second.Votes.end())
        return;

    itr->second.Votes[guid.GetCounter()] = vote;
    if (vote)
        ++itr->second.Accepted;

    if ((grp->GetMembersCount() <= itr->second.Votes.size()) || (itr->second.Accepted >= LFG_KICK_VOTE_NEEDED))
    {
        itr->second.InProgress = false;
        for (GroupReference* membItr = grp->GetFirstMember(); membItr != NULL; membItr = membItr->next())
        {
            Player* membPlr = membItr->getSource();
            if (!membPlr)
                continue;
            if (membPlr->GetObjectGuid() != itr->second.VictimGuid)
                membPlr->GetSession()->SendLfgKickStatus(&itr->second);
        }
        grp->IncLfgKickCount();
        plr = sObjectMgr.GetPlayer(itr->second.VictimGuid);
        grp->RemoveMember(itr->second.VictimGuid, 1);
        /*
        if (plr)
            plr->UninviteFromGroup();
        else
            Player::RemoveFromGroup(grp, itr->second.VictimGuid);*/
        m_KicksMap.erase(itr);
    }
}

void LFGMgr::DoInitVoteKick(ObjectGuid const& senderGuid, ObjectGuid const& victimGuid, std::string reason)
{
    Player* plr = sObjectMgr.GetPlayer(senderGuid);
    if (!plr)
        return;
    Group* grp = plr->GetGroup();
    if (!grp)
        return;

    m_KicksMap[grp->GetId()].Accepted = 1;
    m_KicksMap[grp->GetId()].ExpireTime = time(NULL) + LFG_TIMELAPS_KICK_VOTE;
    m_KicksMap[grp->GetId()].InProgress = true;
    m_KicksMap[grp->GetId()].Reason = reason;
    m_KicksMap[grp->GetId()].VictimGuid = victimGuid;
    m_KicksMap[grp->GetId()].Votes[senderGuid.GetCounter()] = true;

    for (GroupReference* itr = grp->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* membPlr = itr->getSource();
        if (!membPlr)
            continue;
        ObjectGuid membGuid = membPlr->GetObjectGuid();
        if  ((membGuid == senderGuid) || (membGuid == victimGuid))
            continue;
        membPlr->GetSession()->SendLfgKickStatus(&m_KicksMap[grp->GetId()]);
    }
}

void LFGMgr::DoDeleteExpiredVoteKick()
{
    tLfgKicksMap::iterator itr = m_KicksMap.begin();
    time_t currTime = time(NULL);
    while (itr != m_KicksMap.end())
    {
        if (currTime < itr->second.ExpireTime)
        {
            ++itr;
            continue;
        }
        sLog.outDebug("DeleteExpiredVoteKick> id=%u", itr->first);
        Group* grp = sObjectMgr.GetGroupById(itr->first);
        if (grp)
        {
            itr->second.InProgress = false;
            for (GroupReference* membItr = grp->GetFirstMember(); membItr != NULL; membItr = membItr->next())
            {
                Player* membPlr = membItr->getSource();
                if (!membPlr)
                    continue;
                if (membPlr->GetObjectGuid() != itr->second.VictimGuid)
                    membPlr->GetSession()->SendLfgKickStatus(&itr->second);
            }
        }
        itr=m_KicksMap.erase(itr);
    }
}

void LFGMgr::DoCreateGroup(s_LfgProposal* prop)
{
    sLog.outDebug("<<<<<<<<<<<<<<<<<<<<<<<<<<< LFG CREATE GROUP >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");

    //typedef std::list < s_GuidInfo* > tGuidInfoList;

    Group* FormedGroup = new Group();

    tGuidInfoList memberList;
    ObjectGuid const* leaderGuid = NULL;
    uint32 leaderRoles = 0;
    for (tGuidInfoSet::const_iterator itr = prop->NewGroupInfo->NewGroup.begin(); itr != prop->NewGroupInfo->NewGroup.end(); ++itr)
    {
        if (!(*itr)->PreGrouped)
        {
            if ((*itr)->ChosenRole & LFG_LEADER)
            {
                leaderGuid = &(*itr)->Guid;
                leaderRoles = (*itr)->ChosenRole;
                break;
            }
        }
        else
        {
            for (tMemberInfoMap::const_iterator guidItr = (*itr)->MemberInfo.begin(); guidItr != (*itr)->MemberInfo.end(); ++guidItr)
                if (guidItr->second.ChosenRole & LFG_LEADER)
                {
                    leaderGuid = &guidItr->second.Guid;
                    leaderRoles = guidItr->second.ChosenRole;
                    break;
                }
            if (leaderGuid)
                break;
        }
    }

    if (!leaderGuid)
    {
        tGuidInfoSet::const_iterator itr = prop->NewGroupInfo->NewGroup.begin();
        if ((*itr)->PreGrouped)
        {
            leaderGuid = &(*itr)->MemberInfo.begin()->second.Guid;
            leaderRoles = (*itr)->MemberInfo.begin()->second.ChosenRole;
        }
        else
        {
            leaderGuid = &(*itr)->Guid;
            leaderRoles = (*itr)->ChosenRole;
        }
    }

    Player * plr = sObjectMgr.GetPlayer(*leaderGuid);
    if (!plr)
        return;
    Group* grp = plr->GetGroup();
    if (grp)
    {
        grp->Disband();
        sObjectMgr.RemoveGroup(grp);
        delete grp;
    }

    // if NewGroup size > 1 this mean there is at least 1 pickup.
    bool LfgBonus = prop->NewGroupInfo->NewGroup.size() > 1;
    if (LfgBonus)
        FormedGroup->SetLuckOfTheDraw();
    FormedGroup->Create(*leaderGuid, plr->GetName(), leaderRoles, prop->NewGroupInfo->DungeonEntry);
    sObjectMgr.AddGroup(FormedGroup);

    //s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_TELEPORT_PLAYER, *leaderGuid);
    //m_ActionMsgList.Add(actionMsg);

    bool groupRemoved = false;
    for (tGuidInfoSet::const_iterator itr = prop->NewGroupInfo->NewGroup.begin(); itr != prop->NewGroupInfo->NewGroup.end(); ++itr)
    {
        if ((*itr)->PreGrouped)
        {
            for (tMemberInfoMap::const_iterator guidItr = (*itr)->MemberInfo.begin(); guidItr != (*itr)->MemberInfo.end(); ++guidItr)
            {
                if (guidItr->second.Guid == *leaderGuid)
                {
                   s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_TELEPORT_PLAYER, *leaderGuid);
                   m_ActionMsgList.Add(actionMsg);
                   sLog.outDebug("LFGMgr::DoCreateGroup> [%u] is added for teleport", (guidItr)->second.Guid.GetCounter());
                   RemovePlayerFromQueue(*leaderGuid);
                   continue;
                }
                Player * plr = sObjectMgr.GetPlayer(guidItr->second.Guid);
                if (!plr)
                    continue;
                Group* grp = plr->GetGroup();
                if (grp)
                {
                    grp->Disband();
                    sObjectMgr.RemoveGroup(grp);
                    delete grp;
                }
                FormedGroup->AddMember(guidItr->second.Guid, plr->GetName(), guidItr->second.ChosenRole);
                s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_TELEPORT_PLAYER, guidItr->second.Guid);
                m_ActionMsgList.Add(actionMsg);
                sLog.outDebug("LFGMgr::DoCreateGroup> [%u] is added for teleport", (guidItr)->second.Guid.GetCounter());
            }
        }
        else
        {
            if ((*itr)->Guid == *leaderGuid)
            {
               s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_TELEPORT_PLAYER, *leaderGuid);
               m_ActionMsgList.Add(actionMsg);
               sLog.outDebug("LFGMgr::DoCreateGroup> [%u] is added for teleport", (*itr)->Guid.GetCounter());
               RemovePlayerFromQueue(*leaderGuid);
               continue;
            }
            Player * plr = sObjectMgr.GetPlayer((*itr)->Guid);
            if (!plr)
                continue;
            Group* grp = plr->GetGroup();
            if (grp)
            {
                grp->Disband();
                sObjectMgr.RemoveGroup(grp);
                delete grp;
            }

            FormedGroup->AddMember((*itr)->Guid, plr->GetName(), (*itr)->ChosenRole);
            s_LfgActionMsg* actionMsg = new s_LfgActionMsg(LFG_ACTION_TELEPORT_PLAYER, (*itr)->Guid);
            m_ActionMsgList.Add(actionMsg);
           sLog.outDebug("LFGMgr::DoCreateGroup> [%u] is added for teleport", (*itr)->Guid.GetCounter());
        }
        RemovePlayerFromQueue((*itr)->Guid);
    }
    //RemovePlayerFromQueue(*leaderGuid);

    s_LfgActionMsg* actionMsg1 = new s_LfgActionMsg(LFG_ACTION_DELAYED_GROUP_UPDATE, FormedGroup->GetId());
    m_ActionMsgList.Add(actionMsg1);

    FormedGroup->SetDungeonDifficulty(Difficulty(prop->NewGroupInfo->DungeonEntry->difficulty));
}

void LFGMgr::DoDeleteExpiredRolesCheck()
{
    tRolesCheckMap::iterator itr = m_RolesCheckMap.begin();
    while (itr != m_RolesCheckMap.end())
    {
        //Player* leader = sObjectMgr.GetPlayer(itr->second.LeaderGuid);
        //Group* grp = sObjectMgr.GetGroupById(itr->first);
        s_GuidInfo const* guidInfo = GetGuidInfos(itr->second.LeaderGuid);

        if (!guidInfo)
        {
            sLog.outDebug("DeleteExpiredRolesCheck> guidinfo not found, deleting RolesCheck number %u", itr->first);
            itr = m_RolesCheckMap.erase(itr);
            continue;
        }

        if (time(NULL) > itr->second.expireTime)
        {
            sLog.outDebug("DeleteExpiredRolesCheck> Delete RolesCheck number %u", itr->first);
            itr->second.result = LFG_ROLECHECK_MISSING_ROLE;
            Player* plr;
            for (tMemberInfoMap::const_iterator guidItr = guidInfo->MemberInfo.begin(); guidItr != guidInfo->MemberInfo.end(); ++guidItr)
            {
                plr = sObjectMgr.GetPlayer(guidItr->second.Guid);
                if (plr)
                {
                    plr->GetSession()->SendLfgRoleCheckUpdate(&itr->second);
                    if (guidItr->second.Guid == guidInfo->Guid)
                        plr->GetSession()->SendLfgJoinResult(LFG_JOIN_FAILED, LFG_ROLECHECK_MISSING_ROLE);
                    SendLFGUpdate(plr, LFG_UPDATETYPE_ROLECHECK_FAILED);
                }
            }
            RemovePlayerFromQueue(guidInfo->Guid);
            itr = m_RolesCheckMap.erase(itr);
            continue;
        }
        ++itr;
    }
}

void LFGMgr::DoDeleteExpiredProposal()
{
    tLfgProposalMap::iterator itr = m_ProposalMap.begin();
    while (itr != m_ProposalMap.end())
    {
        if (time(NULL) < itr->second.ProposalExpire)
        {
            ++itr;
            continue;
        }

        DoCancelProposal(itr->first, ObjectGuid(), LFG_UPDATETYPE_PROPOSAL_FAILED);
        itr=m_ProposalMap.erase(itr);
    }
}

void LFGMgr::DoUpdateQueuedStatus(Team team)
{
    time_t currTime = time(NULL);
    uint32 queuedTime;

    for (tGuidInfoQueueConstItr qitr=QueueBegin(team); qitr!=QueueEnd(team); ++qitr)
    {
        if ((!qitr->second.LoggedOff) && (qitr->second.Status == LFG_PLAYER_STATUS_IN_QUEUE))
        {
            if (!qitr->second.PreGrouped)
            {
                Player * plr = sObjectMgr.GetPlayer(qitr->second.Guid);
                if (plr)
                {
                    queuedTime = uint32(currTime - qitr->second.JoinedTime);

                    pLfgDungeonStats dungeonStats = GetDungeonStats(qitr->second.Dungeon, team);
                    if (!dungeonStats)
                    {
                        sLog.outError("BOOOOOOOOOOOOOOOOOMMMMMMMMMMMMMMMM");
                        continue;
                    }
                    plr->GetSession()->SendLfgQueueStatus(  *(qitr->second.Dungeon->begin()),
                        1000,
                        1500,
                        2000,
                        2500,
                        3000,
                        queuedTime,
                        (dungeonStats->Tanks>0)? 0 : 1,
                        (dungeonStats->Healers>0)? 0 : 1,
                        (dungeonStats->DPS>2)? 0 : (3-dungeonStats->DPS));
                }
            }
            else
            {
                queuedTime = uint32(currTime - qitr->second.JoinedTime);
                pLfgDungeonStats dungeonStats = GetDungeonStats(qitr->second.Dungeon, team);
                if (!dungeonStats)
                {
                   sLog.outError("BOOOOOOOOOOOOOOOOOMMMMMMMMMMMMMMMM");
                   continue;
                }
                for (tMemberInfoMap::const_iterator guidItr = qitr->second.MemberInfo.begin(); guidItr != qitr->second.MemberInfo.end(); ++guidItr)
                {
                    Player * plr = sObjectMgr.GetPlayer(guidItr->second.Guid);
                    if (plr)
                    {

                        plr->GetSession()->SendLfgQueueStatus(  *(qitr->second.Dungeon->begin()),
                            1000,
                            1500,
                            2000,
                            2500,
                            3000,
                            queuedTime,
                            (dungeonStats->Tanks>0)? 0 : 1,
                            (dungeonStats->Healers>0)? 0 : 1,
                            (dungeonStats->DPS>2)? 0 : (3-dungeonStats->DPS));

                    }
                }
            }
        }
    }
}

void LFGMgr::DoTeleportPlayer(Player* plr, WorldLocation const* location)
{
    sLog.outDebug("LFGMgr::TeleportPlayer: [%u] is being teleported", plr->GetObjectGuid().GetCounter());

    LfgTeleportError error = LFG_TELEPORTERROR_OK;

    if (plr->GetMapId() == location->mapid)
        return;

    if (!plr->isAlive())
        error = LFG_TELEPORTERROR_PLAYER_DEAD;

    if (error == LFG_TELEPORTERROR_OK)
    {
        if (plr->TeleportTo(*location))
            plr->Unmount();
        else
        {
            plr->GetSession()->SendLfgTeleportError(LFG_TELEPORTERROR_INVALID_LOCATION);
            sLog.outError("LfgMgr::TeleportPlayer: Failed to teleport [%u] to map %u: ",  plr->GetObjectGuid().GetCounter(), location->mapid);
        }
    }
    else
        plr->GetSession()->SendLfgTeleportError(error);
}

void LFGMgr::GroupFound(pLfgNewGroup groupInfo)
{
    DoSendProposal(groupInfo);
}

//=============================================================================
//======= CanUninviteFromGroup called by GroupHandler =========================
//=============================================================================
s_LfgPartyResult LFGMgr::CanUninviteFromGroup(Group* grp, Player* plr) const
{
    s_LfgPartyResult partyResult;
    partyResult.result = ERR_PARTY_RESULT_OK;
    partyResult.seconds = 0;

    if (!grp)
    {
        partyResult.result = ERR_NOT_IN_GROUP;
        return partyResult;
    }

    tLfgKicksMap::const_iterator itr = m_KicksMap.find(grp->GetId());
    if (itr != m_KicksMap.end())
    {
        partyResult.result = ERR_PARTY_LFG_BOOT_IN_PROGRESS;
        return partyResult;
    }

    if (!plr)
        return partyResult;

    if (!grp->CanDoLfgKick())
    {
        partyResult.result = ERR_PARTY_LFG_BOOT_LIMIT;
        return partyResult;
    }

    if (grp->GetMembersCount() <= LFG_KICK_VOTE_NEEDED)
    {
        partyResult.result = ERR_PARTY_LFG_BOOT_TOO_FEW_PLAYERS;
        return partyResult;
    }

    if (grp->IsRollLootActive())
    {
        partyResult.result = ERR_PARTY_LFG_BOOT_LOOT_ROLLS;
        return partyResult;
    }

    for (GroupReference* itr = grp->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* membPlr = itr->getSource();

        if (!membPlr)
            continue;
        if (membPlr->isInCombat())
        {
            partyResult.result = ERR_PARTY_LFG_BOOT_IN_COMBAT; // TODO >also must have a cooldown (some secs after combat finish)
            return partyResult;
        }
        if (membPlr->HasAura(LFG_SPELL_DUNGEON_COOLDOWN))
        {
            Aura* aura = membPlr->GetAura(LFG_SPELL_DUNGEON_COOLDOWN, SpellEffectIndex(0));
            partyResult.result = ERR_PARTY_LFG_BOOT_NOT_ELIGIBLE_S;
            if (aura)
                partyResult.seconds = uint32(aura->GetAuraDuration()/1000);
            return partyResult;
        }
    }

    if ((LFG_KICK_COOLDOWN_ACTIVE) && (grp->GetLfgKickCoolDown()))
    {
        partyResult.result = ERR_PARTY_LFG_BOOT_COOLDOWN_S;
        partyResult.seconds = grp->GetLfgKickCoolDown();
        return partyResult;
    }

    /* Missing support for these types
    return ERR_PARTY_LFG_BOOT_DUNGEON_COMPLETE;
    */
    return partyResult;
}

//=============================================================================
//======= LFGMgr Methods ======================================================
//=============================================================================
uint32 LFGMgr::GetNewProposalID()
{
    ++m_ProposalId;
    if (m_ProposalId < std::numeric_limits< uint32 >::max())
        return m_ProposalId;
    m_ProposalId=1;
    return 1;
}

bool LFGMgr::CheckValidRoles(tLfgRolesList& roleList)
{
    uint32 tank = 0;
    uint32 heal = 0;
    uint32 dps = 0;
    for (tLfgRolesList::iterator itr = roleList.begin(); itr != roleList.end(); ++itr)
    {
        uint8 roles = (*itr);
        if ((roles == LFG_TANK) || (roles == (LFG_TANK | LFG_LEADER)))
            ++tank;
        else if ((roles == LFG_HEALER) || (roles == (LFG_HEALER | LFG_LEADER)))
                ++heal;
        else if ((roles == LFG_DPS) || (roles == (LFG_DPS | LFG_LEADER)))
                ++dps;
    }

    if ((dps>3) || (heal>1) || (tank>1))
        return false;
    return true;
}

//---------------------
// Dungeon methods
//---------------------


void LFGMgr::BuildRewardBlock(WorldPacket &data, uint32 dungeon, Player *plr)
{
    s_LfgReward const* reward = NULL;//GetDungeonReward(dungeon, plr->getLevel());

    if (!reward)
    {
        data << uint8(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint8(0);
        return;
    }

    uint8   done=0;
    Quest const* qRew = sObjectMgr.GetQuestTemplate(reward->reward[0].questId);

    if (qRew)
    {
        done = plr->CanRewardQuest(qRew,false);
        if (done)
            qRew = sObjectMgr.GetQuestTemplate(reward->reward[1].questId);
    }

    if (qRew)
    {
        sLog.outDebug("QuestID=%uand reward is %u, done=%s",reward->reward[0].questId,qRew->RewItemId[0],(done)?"True":"False");
        data << uint8(done);
        data << uint32(qRew->GetRewOrReqMoney());
        data << uint32(qRew->XPValue(plr));
        data << uint32(reward->reward[done].variableMoney);
        data << uint32(reward->reward[done].variableXP);
        data << uint8(qRew->GetRewItemsCount());
        if (qRew->GetRewItemsCount())
        {
            ItemPrototype const* iProto = NULL;
            for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
            {
                if (!qRew->RewItemId[i])
                    continue;

                iProto = ObjectMgr::GetItemPrototype(qRew->RewItemId[i]);

                data << uint32(qRew->RewItemId[i]);
                data << uint32(iProto ? iProto->DisplayInfoID : 0);
                data << uint32(qRew->RewItemCount[i]);
            }
        }
    }
    else
    {
        sLog.outDebug("No reward??");
        data << uint8(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint8(0);
    }
}

//===============================================================================================
// LFGQMgr class is used to encapsulate complete LFG queue
//===============================================================================================
// Handler cannot call this class directly
//===============================================================================================

LFGQMgr::LFGQMgr()
{
    m_StatsUpdateAlliance = false;
    m_StatsUpdateHorde = false;
}

LFGQMgr::~LFGQMgr()
{
}

void LFGQMgr::Initialize()
{
    LFGDungeonMgr::Initialize();
}

e_LfgJoinResult LFGQMgr::AddPlayerToQueue(Player* plr, pLfgDungeonSet dungeonSet, uint32 roles)
{
    ObjectGuid guid = plr->GetObjectGuid();
    Group* grp = plr->GetGroup();
    // Check player or group member restrictions
    if (plr->InBattleGround() || plr->InArena() || plr->InBattleGroundQueue())
        return LFG_JOIN_USING_BG_SYSTEM;
    else if (plr->HasAura(LFG_SPELL_DUNGEON_DESERTER))
        return LFG_JOIN_DESERTER;
    else if (plr->HasAura(LFG_SPELL_DUNGEON_COOLDOWN))
        return LFG_JOIN_RANDOM_COOLDOWN;
    else if (!dungeonSet->size())
        return LFG_JOIN_NOT_MEET_REQS;
    else if (grp)
    {
        if (grp->GetMembersCount() > MAX_GROUP_SIZE)
            return LFG_JOIN_TOO_MUCH_MEMBERS;
        else
        {
            uint8 memberCount = 0;
            for (GroupReference* itr = grp->GetFirstMember(); itr != NULL ; itr = itr->next())
            {
                if (Player* plrg = itr->getSource())
                {
                    if (plrg->HasAura(LFG_SPELL_DUNGEON_DESERTER))
                        return LFG_JOIN_PARTY_DESERTER;
                    else if (plrg->HasAura(LFG_SPELL_DUNGEON_COOLDOWN))
                        return LFG_JOIN_PARTY_RANDOM_COOLDOWN;
                    else if (plrg->InBattleGround() || plrg->InArena() || plrg->InBattleGroundQueue())
                        return LFG_JOIN_USING_BG_SYSTEM;
                    ++memberCount;
                }
            }
            if (memberCount != grp->GetMembersCount())
                return LFG_JOIN_DISCONNECTED;
            if (grp->IsLFGGroup() && (memberCount >= MAX_GROUP_SIZE))
                return LFG_JOIN_GROUPFULL;
        }
    }

    time_t now = time(NULL);
    tGuidInfoQueue* guidInfoQueue = &m_AllGuidInfoQueue[(Team) plr->GetTeam()];
    uint32 counter = guid.GetCounter();
    sLog.outDebug("DF::AddPlayerToQueue > Player '%s' is joining LFG queue with roles=%u.", plr->GetName(), roles);
    (*guidInfoQueue)[counter].Roles = roles;
    (*guidInfoQueue)[counter].Dungeon = dungeonSet;
    (*guidInfoQueue)[counter].JoinedTime = now;
    (*guidInfoQueue)[counter].Guid = guid;
    (*guidInfoQueue)[counter].AverageStuffLvl = plr->GetEquipGearScore();
    (*guidInfoQueue)[counter].team = plr->GetTeam();
    if (grp)
    {
        uint32 membCount = 0;
        (*guidInfoQueue)[counter].PreGrouped = true;
        (*guidInfoQueue)[counter].GroupId = grp->GetId();
        (*guidInfoQueue)[counter].IsContinue = grp->IsLFGGroup();
        for (GroupReference* memberItr = grp->GetFirstMember(); memberItr != NULL; memberItr = memberItr->next())
        {
            Player* membPlr = memberItr->getSource();
            if (!membPlr) //check not needed?
                continue;
            ObjectGuid membGuid = membPlr->GetObjectGuid();
            (*guidInfoQueue)[counter].MemberInfo[membGuid.GetCounter()].Guid = membGuid;
            (*guidInfoQueue)[counter].MemberInfo[membGuid.GetCounter()].AverageStuffLvl = membPlr->GetEquipGearScore();
            m_GuidInfoMapIndex[membGuid.GetCounter()]=&(*guidInfoQueue)[counter]; // Helper map to get fast access to all info in any map
        }
    }
    else
    {
        m_GuidInfoMapIndex[counter]=&(*guidInfoQueue)[counter]; // Helper map to get fast access to all info in any map
    }

    return LFG_JOIN_OK;
}

bool LFGQMgr::GetQueueIndex(ObjectGuid guid, Team& team, tGuidInfoQueueItr& itr)
{
    tGuidInfoMap::iterator it = m_GuidInfoMapIndex.find(guid.GetCounter());
    if (it == m_GuidInfoMapIndex.end())
        return false;

    team = it->second->team;
    if (!it->second->PreGrouped)
    {
        itr = m_AllGuidInfoQueue[team].find(guid.GetCounter());
        return (itr != m_AllGuidInfoQueue[team].end());
    }
    // it's group so point to his leader
    itr = m_AllGuidInfoQueue[team].find(it->second->Guid.GetCounter());
    return (itr != m_AllGuidInfoQueue[team].end());
}

bool LFGQMgr::RemovePlayerFromQueue(ObjectGuid guid)
{
    Team team;
    tGuidInfoQueue::iterator it;
    if (GetQueueIndex(guid, team, it))
    {
        if (!it->second.PreGrouped)
        {
            tGuidInfoMap::iterator itr = m_GuidInfoMapIndex.find(guid.GetCounter());
            if (itr != m_GuidInfoMapIndex.end())
            m_GuidInfoMapIndex.erase(itr);
        }
        else
        {
            for (tMemberInfoMap::iterator guidItr = it->second.MemberInfo.begin(); guidItr != it->second.MemberInfo.end(); ++guidItr)
            {
                tGuidInfoMap::iterator itr = m_GuidInfoMapIndex.find(guidItr->first);
                if (itr != m_GuidInfoMapIndex.end())
                    m_GuidInfoMapIndex.erase(itr);
            }
        }
        sLog.outDebug("RemovePlayerFromQueue> Removing [%u] ...", guid.GetCounter());
        if (it->second.Status == LFG_PLAYER_STATUS_IN_QUEUE)
            RemoveGuidInfoFromStat(&it->second);
        m_AllGuidInfoQueue[team].erase(it);
        return true;
    }
    sLog.outDebug("RemovePlayerFromQueue> Failled to found [%u] ...", guid.GetCounter());
    return false;
}

void LFGQMgr::SetGroupRoles(s_LfgRolesCheck& rolesCheck)
{
    sLog.outDebug("IN setGRoup");
    s_GuidInfo* guidInfo = GetGuidInfo(rolesCheck.LeaderGuid);
    if (!guidInfo)
        return;
    sLog.outDebug("Status changed");

    for (tMemberInfoMap::iterator guidItr = guidInfo->MemberInfo.begin(); guidItr != guidInfo->MemberInfo.end(); ++guidItr)
    {
        uint32 roles = LFG_NO_ROLE;
        tLfgRolesMap::const_iterator rolesItr = rolesCheck.RolesInfo.find(guidItr->first);
        if (rolesItr != rolesCheck.RolesInfo.end())
            roles = rolesItr->second.Roles;
        guidItr->second.Roles = roles;
    }
    SetGuidStatus(guidInfo, LFG_PLAYER_STATUS_IN_QUEUE);
}

bool LFGQMgr::IsInQueue(ObjectGuid const& guid)
{
    tGuidInfoMap::iterator it = m_GuidInfoMapIndex.find(guid.GetCounter());
    if (it == m_GuidInfoMapIndex.end())
        return false;
     return true;
}

bool LFGQMgr::IsOnLine(ObjectGuid const& guid)
{
    tGuidInfoQueueItr it;
    Team team;
    if (GetQueueIndex(guid, team, it))
    {
        if (it->second.PreGrouped)
        {
            tMemberInfoMap::const_iterator itr = it->second.MemberInfo.find(guid.GetCounter());
            if (itr != it->second.MemberInfo.end())
                return (itr->second.LoggedOff);
            return false;
        }
        return (it->second.LoggedOff);
    }
    return false;
}

s_GuidInfo* LFGQMgr::GetGuidInfo(ObjectGuid guid)
{
    tGuidInfoQueueItr it;
    Team team;
    if (GetQueueIndex(guid, team, it))
        return &(it->second);
    return NULL;
}

s_GuidInfo const* LFGQMgr::GetGuidInfos(ObjectGuid guid)
{
    return GetGuidInfo(guid);
}

s_GuidInfo const* LFGQMgr::GetGuidInfos(ObjectGuid guid, Team team)
{
    tGuidInfoQueueItr itr;
    itr = m_AllGuidInfoQueue[team].find(guid.GetCounter());
    if (itr != m_AllGuidInfoQueue[team].end())
        return &(itr->second);
    return NULL;
}

void LFGQMgr::SetOnLineStatus(ObjectGuid const& guid, bool status)
{
    s_GuidInfo* guidInfo = GetGuidInfo(guid);
    if (!guidInfo)
        return;
    if (guidInfo->PreGrouped)
    {
       tMemberInfoMap::iterator itr = guidInfo->MemberInfo.find(guid.GetCounter());
       if (itr != guidInfo->MemberInfo.end())
          itr->second.LoggedOff = status;
    }
    else
        guidInfo->LoggedOff = status;
}

pLfgDungeonSet LFGQMgr::GetDungeonsByGuid(ObjectGuid guid)
{
    tGuidInfoQueueItr it;
    Team team;
    if (GetQueueIndex(guid, team, it))
        return it->second.Dungeon;
    return NULL;
}

void LFGQMgr::SetGuidStatus(s_GuidInfo* guidInfo, e_LfgPlayerStatus status, uint32 statusId)
{
    if (guidInfo)
    {


        switch (status)
        {
        case LFG_PLAYER_STATUS_IN_QUEUE :
        {
            switch (guidInfo->Status)
            {
            case LFG_PLAYER_STATUS_IN_PROPOSAL :
                guidInfo->ChosenRole = LFG_NO_ROLE;
            case LFG_PLAYER_STATUS_INITIALIZING :
            case LFG_PLAYER_STATUS_IN_ROLECHECK :
                pLfgNewGroup groupInfo = AddGuidInfoToDungeonQueue(guidInfo);
                if (groupInfo)
                {
                    guidInfo->Status = status;
                    GroupFound(groupInfo);
                    return;
                }
                break;
            }
            break;
        }
        case LFG_PLAYER_STATUS_IN_ROLECHECK :
        case LFG_PLAYER_STATUS_IN_PROPOSAL :
        {
            switch (guidInfo->Status)
            {
            case LFG_PLAYER_STATUS_IN_QUEUE :
                RemoveGuidInfoFromStat(guidInfo);
                break;
            }
            break;
        }
        }
        guidInfo->Status = status;
        guidInfo->StatusId = statusId;
    }
}

void LFGQMgr::SetGuidStatus(ObjectGuid const& guid, e_LfgPlayerStatus status, uint32 statusId)
{
    SetGuidStatus(GetGuidInfo(guid), status, statusId);
}

void LFGQMgr::SetGuidStatus(tGuidInfoSet* newGroup, e_LfgPlayerStatus status, uint32 statusId)
{
    for (tGuidInfoSet::iterator itr = newGroup->begin(); itr != newGroup->end(); ++itr)
    {
        SetGuidStatus(GetGuidInfo((*itr)->Guid), status, statusId);
    }
}

bool LFGQMgr::GetGroup(tGuidInfoSet* dispoGuid, tGuidInfoSet& groupedSet, uint32 neededTank, uint32 neededHealer, uint32 neededDps)
{
    sLog.outDebug("GetGroup> dispoGuid=%u, groupset=%u, neededTank=%u, neededHeal=%u, neededDps=%u", dispoGuid->size(), groupedSet.size(), neededTank, neededHealer, neededDps);
    if ((!neededTank) && (!neededHealer) && (!neededDps))
        return true;
    for (tGuidInfoSet::iterator itr = dispoGuid->begin(); itr != dispoGuid->end(); ++itr)
    {
        if ((*itr)->ChosenRole != LFG_NO_ROLE)
                continue;

        if (!(*itr)->PreGrouped)
        {
            if (neededTank)
            {
                if ((*itr)->Roles & LFG_TANK)
                {
                    (*itr)->ChosenRole = LFG_TANK;
                    if (GetGroup(dispoGuid, groupedSet, neededTank-1, neededHealer, neededDps))
                    {
                        (*itr)->ChosenRole |= ((*itr)->Roles & LFG_LEADER);
                        groupedSet.insert(*itr);
                        sLog.outDebug("GetGroup> Found tank");
                        return true;
                    }
                    (*itr)->ChosenRole = LFG_NO_ROLE;
                }
                continue;
            }

            if (neededHealer)
            {
                if ((*itr)->Roles & LFG_HEALER)
                {
                    (*itr)->ChosenRole = LFG_HEALER;
                    if (GetGroup(dispoGuid, groupedSet, neededTank, neededHealer-1, neededDps))
                    {
                        (*itr)->ChosenRole |= ((*itr)->Roles & LFG_LEADER);
                        groupedSet.insert(*itr);
                        sLog.outDebug("GetGroup> Found healer");
                        return true;
                    }
                    (*itr)->ChosenRole = LFG_NO_ROLE;
                }
                continue;
            }

            if (neededDps)
            {
                if ((*itr)->Roles & LFG_DPS)
                {
                    (*itr)->ChosenRole = LFG_DPS;
                    if (GetGroup(dispoGuid, groupedSet, neededTank, neededHealer, neededDps-1))
                    {
                        (*itr)->ChosenRole |= ((*itr)->Roles & LFG_LEADER);
                        groupedSet.insert(*itr);
                        sLog.outDebug("GetGroup> Found dps");
                        return true;
                    }
                    (*itr)->ChosenRole = LFG_NO_ROLE;
                }
                continue;
            }
        }
        else
        {
            uint32 memberCount = (*itr)->MemberInfo.size();
            if (neededTank+neededDps+neededHealer < memberCount)
                continue;   // this group is too big
            uint32 tempTank = neededTank;
            uint32 tempHealer = neededHealer;
            uint32 tempDps = neededDps;
            for (tMemberInfoMap::iterator guidItr = (*itr)->MemberInfo.begin(); guidItr != (*itr)->MemberInfo.end(); ++guidItr)
            {
                if (tempTank)
                {
                    if (guidItr->second.Roles & LFG_TANK)
                    {
                        guidItr->second.ChosenRole = LFG_TANK | (guidItr->second.Roles & LFG_LEADER);
                        --tempTank;
                        --memberCount;
                        continue;
                    }
                }
                if (tempHealer)
                {
                    if (guidItr->second.Roles & LFG_HEALER)
                    {
                        guidItr->second.ChosenRole = LFG_HEALER | (guidItr->second.Roles & LFG_LEADER);
                        --tempHealer;
                        --memberCount;
                        continue;
                    }

                }
                if (tempDps)
                {
                    if (guidItr->second.Roles & LFG_DPS)
                    {
                        guidItr->second.ChosenRole = LFG_DPS | (guidItr->second.Roles & LFG_LEADER);
                        --tempDps;
                        --memberCount;
                        continue;
                    }
                }
            }
            // if memberCount > 0 some member of the group are have not assigned role
            (*itr)->ChosenRole = LFG_TANK; //just to make it assigned
            if ((memberCount) || (!GetGroup(dispoGuid, groupedSet, tempTank, tempHealer, tempDps)))
            {
                // this loop can perhaps be ignored...
                for (tMemberInfoMap::iterator guidItr = (*itr)->MemberInfo.begin(); guidItr != (*itr)->MemberInfo.end(); ++guidItr)
                     guidItr->second.ChosenRole = LFG_NO_ROLE;
                (*itr)->ChosenRole = LFG_NO_ROLE;
                continue;
            }
            else
            {
                groupedSet.insert(*itr);
                sLog.outDebug("GetGroup> Group added!");
                return true;
            }
        }
    }
    sLog.outDebug("GetGroup> All possibility tested, no group possible.");
    return false;
}

void LFGQMgr::RemoveGuidInfoFromStat(s_GuidInfo* guidInfo)
{
    uint32 tank=0;
    uint32 heal=0;
    uint32 dps=0;
    uint32 roles;
    tLfgDungeonsStatsMap* dungeonsStatsMap = &m_AllDungeonsStatsMap[guidInfo->team];
    if (!guidInfo->PreGrouped)
    {
        roles = guidInfo->Roles;
        if (roles & LFG_TANK)
           ++tank;
        else if (roles & LFG_HEALER)
           ++heal;
        else if (roles & LFG_DPS)
           ++dps;
    }
    else
    {
        for (tMemberInfoMap::iterator guidItr = guidInfo->MemberInfo.begin(); guidItr != guidInfo->MemberInfo.end(); ++guidItr)
        {
            roles = guidItr->second.Roles;
            if (roles & LFG_TANK)
                ++tank;
            else if (roles & LFG_HEALER)
                ++heal;
            else if (roles & LFG_DPS)
                ++dps;
         }
    }
    for (tLfgDungeonSet::iterator dungeonIt = guidInfo->Dungeon->begin(); dungeonIt != guidInfo->Dungeon->end(); ++dungeonIt )
    {
        if ((*dungeonsStatsMap)[*dungeonIt].DungeonStats.Tanks >= tank)
            (*dungeonsStatsMap)[*dungeonIt].DungeonStats.Tanks -= tank;
        else
            (*dungeonsStatsMap)[*dungeonIt].DungeonStats.Tanks = 0;

        if ((*dungeonsStatsMap)[*dungeonIt].DungeonStats.Healers >= heal)
            (*dungeonsStatsMap)[*dungeonIt].DungeonStats.Healers -= heal;
        else
            (*dungeonsStatsMap)[*dungeonIt].DungeonStats.Healers = 0;

        if ((*dungeonsStatsMap)[*dungeonIt].DungeonStats.DPS >= dps)
            (*dungeonsStatsMap)[*dungeonIt].DungeonStats.DPS -= dps;
        else
            (*dungeonsStatsMap)[*dungeonIt].DungeonStats.DPS = 0;

        //if (!guidInfo->PreGrouped)
        {
            tGuidInfoSet::iterator itr = (*dungeonsStatsMap)[*dungeonIt].GuidInfoSet.find(guidInfo);
            if ( itr != (*dungeonsStatsMap)[*dungeonIt].GuidInfoSet.end())
                (*dungeonsStatsMap)[*dungeonIt].GuidInfoSet.erase(itr);
        }
    }
    if (guidInfo->team == ALLIANCE)
        m_StatsUpdateAlliance = true;
    else
        m_StatsUpdateHorde = true;
}

void LFGQMgr::UpdateDungeonsStatsMap(Team team)
{
    if ((team == ALLIANCE) && (!m_StatsUpdateAlliance))
        return;
    if ((team == HORDE) && (!m_StatsUpdateHorde))
        return;
    m_AllDungeonsStatsSet[team].clear();
    // Next loop will automaticaly sort all iterator by join time. (first in the list is the oldest)
    for (tLfgDungeonsStatsMap::const_iterator itr=m_AllDungeonsStatsMap[team].begin(); itr!=m_AllDungeonsStatsMap[team].end(); ++itr)
        m_AllDungeonsStatsSet[team].insert(itr);

    if (team == ALLIANCE)
        m_StatsUpdateAlliance = false;
    else
        m_StatsUpdateHorde = false;
}

void LFGQMgr::EvalGroupStuff(pLfgNewGroup groupInfo)
{
    uint32 minLvl = std::numeric_limits <uint32>::max();
    uint32 maxLvl = 0;
    for (tGuidInfoSet::iterator playitr = groupInfo->NewGroup.begin(); playitr != groupInfo->NewGroup.end(); ++playitr)
    {
        if (!(*playitr)->PreGrouped)
        {
            if ((*playitr)->AverageStuffLvl > maxLvl)
                maxLvl = (*playitr)->AverageStuffLvl;
            if ((*playitr)->AverageStuffLvl < minLvl)
                minLvl = (*playitr)->AverageStuffLvl;
            sLog.outDebug("EvalGroupStuff> Membrer[%u] have average stuff level=%u", (*playitr)->Guid.GetCounter(), (*playitr)->AverageStuffLvl);
        }
        else
        {
            for (tMemberInfoMap::const_iterator guidItr = (*playitr)->MemberInfo.begin(); guidItr != (*playitr)->MemberInfo.end(); ++guidItr)
            {
                sLog.outDebug("EvalGroupStuff> Membrer[%u] have average stuff level=%u", (*guidItr).first, (*guidItr).second.AverageStuffLvl);
                if ((*guidItr).second.AverageStuffLvl > maxLvl)
                    maxLvl = (*guidItr).second.AverageStuffLvl;
                if ((*guidItr).second.AverageStuffLvl < minLvl)
                    minLvl = (*guidItr).second.AverageStuffLvl;
            }
        }
    }

    if (maxLvl >= minLvl)
        groupInfo->Homogeneity = maxLvl - minLvl;
    sLog.outDebug("EvalGroupStuff> max=%u, min=%u, Homo= %u", maxLvl, minLvl, groupInfo->Homogeneity);
}

pLfgNewGroup LFGQMgr::AddGuidInfoToDungeonQueue(s_GuidInfo* guidInfo)
{
    uint32 tank=0;
    uint32 heal=0;
    uint32 dps=0;
    uint32 roles;

    pLfgNewGroup result;//(new s_LfgNewGroup);

    tLfgDungeonsStatsMap* dungeonsStatsMap = &m_AllDungeonsStatsMap[guidInfo->team];
    if (!guidInfo->PreGrouped)
    {
        roles = guidInfo->Roles;
        if (roles & LFG_TANK)
           ++tank;
        else if (roles & LFG_HEALER)
           ++heal;
        else if (roles & LFG_DPS)
           ++dps;
    }
    else
    {
        for (tMemberInfoMap::iterator guidItr = guidInfo->MemberInfo.begin(); guidItr != guidInfo->MemberInfo.end(); ++guidItr)
        {
            roles = guidItr->second.Roles;
            if (roles & LFG_TANK)
                ++tank;
            else if (roles & LFG_HEALER)
                ++heal;
            else if (roles & LFG_DPS)
                ++dps;
         }
    }
    for (tLfgDungeonSet::iterator dungeonIt = guidInfo->Dungeon->begin(); dungeonIt != guidInfo->Dungeon->end(); ++dungeonIt )
    {
        s_LfgDungeonInfo* currDungeonInfo = &(*dungeonsStatsMap)[*dungeonIt];
        currDungeonInfo->DungeonStats.Tanks += tank;
        currDungeonInfo->DungeonStats.Healers += heal;
        currDungeonInfo->DungeonStats.DPS += dps;
        currDungeonInfo->GuidInfoSet.insert(guidInfo);


        if (GetSetSize(&currDungeonInfo->GuidInfoSet) < LFG_GROUP_SIZE)
            continue;

        pLfgNewGroup newGroupInfo(new s_LfgNewGroup);
        if (GetGroup(&currDungeonInfo->GuidInfoSet, newGroupInfo->NewGroup))
        {
            LFGDungeonEntry const* dungeonEntry = GetRandomDungeon(*dungeonIt);
            if (!dungeonEntry)
            {
                sLog.outError("GetNewParty> Error didn't get DungeonEntry for ID=%u", *dungeonIt);
                continue;
            }

            //Just for debug
            sLog.outDebug("==================================================================");
            sLog.outDebug("GetNewParty> Group found for dungeonID=%u",dungeonEntry->ID);
            sLog.outDebug("==================================================================");
            for (tGuidInfoSet::iterator itr = newGroupInfo->NewGroup.begin(); itr != newGroupInfo->NewGroup.end(); ++itr)
                sLog.outDebug("GetNewParty> guid [%u]", (*itr)->Guid.GetCounter());
            sLog.outDebug("==================================================================");

            EvalGroupStuff(newGroupInfo);

            if ((!result) || (result->Homogeneity < newGroupInfo->Homogeneity))
            {
                newGroupInfo->DungeonEntry = dungeonEntry;
                newGroupInfo->DungeonId = *dungeonIt;
                result = newGroupInfo;
            }
        }
        else
        {
            // clear new party list (possibly filed by getgroup)
            newGroupInfo->NewGroup.clear();
            sLog.outDebug("GetNewParty> newParty is empty try next");
        }
    }
    return result;
}

pLfgDungeonStats LFGQMgr::GetDungeonStats(pLfgDungeonSet dungeonSet, Team team)
{
    tLfgDungeonsStatsMap::const_iterator dsitr;
    pLfgDungeonStats dungeonStats(new s_LfgDungeonStats);
    for (tLfgDungeonSet::const_iterator dungeonIt = dungeonSet->begin(); dungeonIt != dungeonSet->end(); ++dungeonIt)
    {
        dsitr = m_AllDungeonsStatsMap[team].find(*dungeonIt);
        if (dsitr != m_AllDungeonsStatsMap[team].end())
        {
            if ((dsitr->second.DungeonStats.DPS + dsitr->second.DungeonStats.Healers + dsitr->second.DungeonStats.Tanks) > (dungeonStats->DPS + dungeonStats->Healers + dungeonStats->Tanks))
            {
                dungeonStats->DPS=dsitr->second.DungeonStats.DPS;
                dungeonStats->Tanks=dsitr->second.DungeonStats.Tanks;
                dungeonStats->Healers=dsitr->second.DungeonStats.Healers;
            }
        }
    }
    return dungeonStats;
}

uint32 LFGQMgr::GetSetSize(tGuidInfoSet const* guidSet)
{
    uint32 size = 0;
    for (tGuidInfoSet::iterator itr = guidSet->begin(); itr != guidSet->end(); ++itr)
    {
        if ((*itr)->PreGrouped)
            size += (*itr)->MemberInfo.size();
        else
            ++size;
    }
    return size;
}

void LFGQMgr::SaveToDB()
{
}

void LFGQMgr::LoadFromDB()
{
}

void LFGQMgr::ShowQueueInfo(Team team)
{
    for (tGuidInfoQueueConstItr qitr=QueueBegin(team); qitr!=QueueEnd(team); ++qitr)
    {
        //if ((!qitr->second.LoggedOff) && (qitr->second.Status == LFG_PLAYER_STATUS_IN_QUEUE))
        {
            if (!qitr->second.PreGrouped)
            {
                Player * plr = sObjectMgr.GetPlayer(qitr->second.Guid);
                if (plr)
                {
                    sLog.outDebug("Player '%s'[%u] is in queue with status[%u] and roles[%u]", plr->GetName(), qitr->second.Guid.GetCounter(), qitr->second.Status, qitr->second.Roles);
                }
                else
                {
                    sLog.outDebug("Guid[%u] is in queue with status[%u] and roles[%u], player %s logged", qitr->second.Guid.GetCounter(), qitr->second.Status, qitr->second.Roles, (qitr->second.LoggedOff)?"is":"is not");
                }
                    
            }
            else
            {
                for (tMemberInfoMap::const_iterator guidItr = qitr->second.MemberInfo.begin(); guidItr != qitr->second.MemberInfo.end(); ++guidItr)
                {
                    Player * plr = sObjectMgr.GetPlayer(guidItr->second.Guid);
                    if (plr)
                    {
                        sLog.outDebug("Player '%s'[%u] is in queue with status[%u] and roles[%u]", plr->GetName(), qitr->second.Guid.GetCounter(), qitr->second.Status, qitr->second.Roles);
                    }
                    else
                    {
                        sLog.outDebug("Guid[%u] is in queue with status[%u] and roles[%u], player %s logged", qitr->second.Guid.GetCounter(), qitr->second.Status, qitr->second.Roles, (qitr->second.LoggedOff)?"is":"is not");
                    }
                }
            }
        }
    }
}

//===============================================================================================
// LFGDungeonMgr class is used to encapsulate complete LFG Dungeon operations
//===============================================================================================
// Handler can call this class directly
//===============================================================================================
LFGDungeonMgr::LFGDungeonMgr()
{
}

LFGDungeonMgr::~LFGDungeonMgr()
{
}

void LFGDungeonMgr::Initialize()
{
    LFGDungeonEntry const* dungeonEntry;

    for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
    {
        dungeonEntry = sLFGDungeonStore.LookupEntry(i);
        if (!dungeonEntry || dungeonEntry->type == LFG_TYPE_ZONE)
            continue;
        //if (dungeonEntry->type != LFG_TYPE_RANDOM_DUNGEON) m_RandDungList->push_back(dungeonEntry);
        m_AllDungList[dungeonEntry->ID]=dungeonEntry;
        switch (dungeonEntry->grouptype)
        {
        case LFG_GROUPTYPE_CLASSIC      :
            m_DungeonClassicList.push_back(dungeonEntry);
            break;
        case LFG_GROUPTYPE_BC_NORMAL    :
            m_DungeonBCList.push_back(dungeonEntry);
            break;
        case LFG_GROUPTYPE_BC_HEROIC    :
            m_DungeonBCHeroList.push_back(dungeonEntry);
            break;
        case LFG_GROUPTYPE_WTLK_NORMAL  :
            m_DungeonWOTLKList.push_back(dungeonEntry);
            break;
        case LFG_GROUPTYPE_WTLK_HEROIC  :
            m_DungeonWOTLKHeroList.push_back(dungeonEntry);
            break;
        case LFG_GROUPTYPE_WORLD_EVENT  :
            m_DungeonWorldEventList.push_back(dungeonEntry);
            break;
        }
    }
}

LFGDungeonEntry const* LFGDungeonMgr::GetRandomDungeon(uint32 dungeonId)
{
    uint32 size;
    uint32 randNum;
    LFGDungeonEntry const* resultEntry = NULL;
    switch (dungeonId)
    {
    case LFG_RANDOM_CLASSIC     :
        size = m_DungeonClassicList.size();
        if (size!=0)
        {
            randNum = urand(0,size-1);
            resultEntry = m_DungeonClassicList[randNum];
        }
        else
            sLog.outError("Dungeon Classic list is empty!");

        break;
    case LFG_RANDOM_BC_NORMAL   :
        size = m_DungeonBCList.size();
        if (size!=0)
        {
            randNum = urand(0,size-1);
            resultEntry = m_DungeonBCList[randNum];
        }
        else
            sLog.outError("Dungeon BC list is empty!");
        break;
    case LFG_RANDOM_BC_HEROIC   :
        size = m_DungeonBCHeroList.size();
        if (size!=0)
        {
            randNum = urand(0,size-1);
            resultEntry = m_DungeonBCHeroList[randNum];
        }
        else
            sLog.outError("Dungeon BC Hero list is empty!");
        break;
    case LFG_RANDOM_LK_NORMAL   :
        size = m_DungeonWOTLKList.size();
        if (size!=0)
        {
            randNum = urand(0,size-1);
            resultEntry = m_DungeonWOTLKList[randNum];
        }
        else
            sLog.outError("Dungeon WOTLK list is empty!");
        break;
    case LFG_RANDOM_LK_HEROIC   :
        size = m_DungeonWOTLKHeroList.size();
        if (size!=0)
        {
            randNum = urand(0,size-1);
            resultEntry = m_DungeonWOTLKHeroList[randNum];
        }
        else
            sLog.outError("Dungeon WOTLK Hero list is empty!");
        break;
    default :
        sLog.outDebug("LFGDungeonMgr::GetDungeon> Not randomType = %u", dungeonId);
        tLfgDungeonMap::const_iterator itr = m_AllDungList.find(dungeonId);
        if (itr!=m_AllDungList.end())
            resultEntry = itr->second;
        else
            resultEntry = NULL;
    }
    return resultEntry;
}

tLfgDungeonList* LFGDungeonMgr::GetDungeonsForPlayer(Player *plr)
{
    tLfgDungeonList *dungeons = new tLfgDungeonList();

    for (tLfgDungeonMap::iterator itr=m_AllDungList.begin(); itr != m_AllDungList.end(); ++itr)
    {
        if (itr->second->minlevel <= plr->getLevel() && itr->second->maxlevel >= plr->getLevel() &&
            itr->second->expansion <= plr->GetSession()->Expansion())
            dungeons->push_back(itr->second);
    }
    return dungeons;
}

tLfgDungeonList* LFGDungeonMgr::GetOptionalDungeons(Player *plr)
{
    tLfgDungeonList *dungeons = new tLfgDungeonList();

    for (tLfgDungeonMap::iterator itr=m_AllDungList.begin(); itr != m_AllDungList.end(); ++itr)
    {
        if ((itr->second->grouptype == LFG_GROUPTYPE_WORLD_EVENT) || (itr->second->type == LFG_TYPE_RANDOM_DUNGEON))
            if (itr->second->minlevel <= plr->getLevel() && itr->second->maxlevel >= plr->getLevel() &&
                itr->second->expansion <= plr->GetSession()->Expansion())
                dungeons->push_back(itr->second);
    }
    return dungeons;
}

tLfgLockStatusMap* LFGDungeonMgr::GetPartyLockStatusDungeons(Player* plr)
{
    if (!plr)
        return NULL;

    Group* grp = plr->GetGroup();
    if (!grp)
        return NULL;

    Player* plrg;

    tLfgLockStatusMap* dungeonMap = new tLfgLockStatusMap();
    tLfgLockStatusList* dungeonset = NULL;
    for (GroupReference* itr = grp->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        plrg = itr->getSource();
        if (plrg && plrg != plr)
        {
            dungeonset = GetDungeonsLock(plrg);
            if (dungeonset)
                dungeonMap->insert(std::pair<uint32, tLfgLockStatusList*>(plrg->GetObjectGuid().GetCounter(),dungeonset));
        }
    }
    return dungeonMap;
}

tLfgLockStatusList* LFGDungeonMgr::GetDungeonsLock(Player *plr, tLfgDungeonMap* dungeons /*=NULL*/)
{
    tLfgLockStatusList* locks = new tLfgLockStatusList();
    e_LfgDungeonLockReason locktype;
    AreaTrigger const* at;

    if (!dungeons)
        dungeons=&m_AllDungList;

    for (tLfgDungeonMap::const_iterator itr = dungeons->begin(); itr != dungeons->end(); ++itr)
    {
        at=sObjectMgr.GetMapEntranceTrigger(itr->second->map);
        locktype = LFG_LOCKSTATUS_OK;
        if(itr->second->expansion > plr->GetSession()->Expansion())
            locktype = LFG_LOCKSTATUS_INSUFFICIENT_EXPANSION;
        else if(itr->second->minlevel > plr->getLevel())
            locktype = LFG_LOCKSTATUS_TOO_LOW_LEVEL;
        else if(plr->getLevel() > itr->second->maxlevel)
            locktype = LFG_LOCKSTATUS_TOO_HIGH_LEVEL;
        else if (at)
        {
            if ((at->requiredItem) && (!plr->HasItemCount(at->requiredItem,1)))
                locktype=LFG_LOCKSTATUS_MISSING_ITEM;
            else if ((at->requiredItem2) && (!plr->HasItemCount(at->requiredItem2,1)))
                locktype=LFG_LOCKSTATUS_MISSING_ITEM;
            else if (plr->GetBoundInstance(itr->second->map, Difficulty(itr->second->difficulty)))
                            locktype = LFG_LOCKSTATUS_RAID_LOCKED;
            else
            {
                switch (itr->second->grouptype)
                {
                case LFG_GROUPTYPE_CLASSIC :
                case LFG_GROUPTYPE_BC_NORMAL :
                case LFG_GROUPTYPE_WTLK_NORMAL :
                {
                    if (at->requiredQuest)
                    {
                        QuestStatus qReq = plr->GetQuestStatus(at->requiredQuest);
                        if (qReq != QUEST_STATUS_COMPLETE)
                            locktype=LFG_LOCKSTATUS_QUEST_NOT_COMPLETED;
                    }
                    break;
                }
                case LFG_GROUPTYPE_BC_HEROIC :
                case LFG_GROUPTYPE_WTLK_HEROIC :
                {
                    if ((at->heroicKey) && (!plr->HasItemCount(at->heroicKey,1)))
                        locktype=LFG_LOCKSTATUS_MISSING_ITEM;
                    else if ((at->heroicKey2) && (!plr->HasItemCount(at->heroicKey2,1)))
                        locktype=LFG_LOCKSTATUS_MISSING_ITEM;
                    else if (at->requiredQuestHeroic)
                    {
                        QuestStatus qReq = plr->GetQuestStatus(at->requiredQuestHeroic);
                        if (qReq != QUEST_STATUS_COMPLETE)
                            locktype=LFG_LOCKSTATUS_QUEST_NOT_COMPLETED;
                    }
                    break;
                }

                case LFG_GROUPTYPE_CLASSIC_RAID :
                case LFG_GROUPTYPE_BC_RAID:
                case LFG_GROUPTYPE_WTLK_RAID_10 :
                case LFG_GROUPTYPE_WTLK_RAID_25 :

                case LFG_GROUPTYPE_WORLD_EVENT :
                    break;
                default :
                    sLog.outError("LFGMgr::GetDungeonLock > Can't determine dungeonId[%u] grouptype = %u", itr->second->ID, itr->second->grouptype);
                    break;
                }
            }

        }
        else
        {
            sLog.outError("LFGMgr::GetDungeonLock > Can't determine AreaTRigger for dungeon[%u] difficulty[%u]", itr->second->ID, itr->second->difficulty);
            locktype=LFG_LOCKSTATUS_INSUFFICIENT_EXPANSION;
        }

        if (locktype != LFG_LOCKSTATUS_OK)
        {
            s_LfgLockStatus *lockStatus = new s_LfgLockStatus();
            lockStatus->dungeonInfo = itr->second;
            lockStatus->lockType = locktype;
            locks->push_back(lockStatus);
        }
    }
    return locks;
}

//=============================================================================
//======== Helper Class =======================================================
//=============================================================================

void LFGHelper::SendLFGUpdate(ObjectGuid guid, e_LfgUpdateType updateType)
{
    Player* plr = sObjectMgr.GetPlayer(guid);
    if (plr)
    {
        if (plr->GetGroup())
        {
            plr->GetSession()->SendLfgUpdateParty(updateType);
        }
        else
            plr->GetSession()->SendLfgUpdatePlayer(updateType);
    }
}

void LFGHelper::SendLFGUpdate(Player* plr, e_LfgUpdateType updateType)
{
    if (!plr)
        return;
    if (plr->GetGroup())
    {
        plr->GetSession()->SendLfgUpdateParty(updateType);
    }
    else
        plr->GetSession()->SendLfgUpdatePlayer(updateType);
}
