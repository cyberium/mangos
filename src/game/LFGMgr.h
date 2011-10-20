#ifndef _LFGMGR_H
#define _LFGMGR_H

#include "Common.h"
#include <ace/Singleton.h>
#include "DBCStructure.h"
#include "Group.h"
#include "SpellAuras.h"
#include "WorldSession.h"
#include <ace/Thread_Manager.h>
#include <ace/Thread.h>

#include <memory>

#define LFG_TIMER_UPDATE_QUEUE 7*1000                       // Update client timer. (msec)
#define LFG_TIMER_UPDATE_CLIENT_INFO 2*1000                 // Update proposal, kiks, rolescheck timer.(msec)
#define LFG_TIMER_DELAYED_REMOVE_LOG_OFF 5*MINUTE*1000      // Delayed remove player from queue timer default 5 min.(msec)
#define LFG_TIMELAPS_PROPOSAL 2*MINUTE
#define LFG_TIMELAPS_KICK_VOTE 2*MINUTE
#define LFG_TIMER_UPDATE_ROLES_CHECK 1000
#define LFG_TIMELAPS_ROLECHECK 2*MINUTE
#define LFG_DELAY_KICK_COOLDOWN 5*MINUTE
#define LFG_MIN_TANK 1
#define LFG_MIN_HEALER 1
#define LFG_MIN_DPS 3
#define LFG_GROUP_SIZE 5
#define LFG_KICK_VOTE_NEEDED 3
#define LFG_MAX_KICKS 2
#define LFG_SPELL_DUNGEON_COOLDOWN 71328
#define LFG_SPELL_DUNGEON_DESERTER 71041
#define LFG_SPELL_LUCK_OF_THE_DRAW 72221

#define LFG_KICK_COOLDOWN_ACTIVE 1


enum e_LfgRoles
{
    LFG_NO_ROLE = 0x00,
    LFG_LEADER  = 0x01,
    LFG_TANK    = 0x02,
    LFG_HEALER  = 0x04,
    LFG_DPS     = 0x08
};

enum e_LfgJoinResult
{
    LFG_JOIN_OK                             = 0,  // Joined (no client msg)
    LFG_JOIN_FAILED                         = 1,  // RoleCheck Failed
    LFG_JOIN_GROUPFULL                      = 2,  // Your group is full
    LFG_JOIN_UNK3                           = 3,  // No client reaction
    LFG_JOIN_INTERNAL_ERROR                 = 4,  // Internal LFG Error
    LFG_JOIN_NOT_MEET_REQS                  = 5,  // You do not meet the requirements for the chosen dungeons
    LFG_JOIN_PARTY_NOT_MEET_REQS            = 6,  // One or more party members do not meet the requirements for the chosen dungeons
    LFG_JOIN_MIXED_RAID_DUNGEON             = 7,  // You cannot mix dungeons, raids, and random when picking dungeons
    LFG_JOIN_MULTI_REALM                    = 8,  // The dungeon you chose does not support players from multiple realms
    LFG_JOIN_DISCONNECTED                   = 9,  // One or more party members are pending invites or disconnected
    LFG_JOIN_PARTY_INFO_FAILED              = 10, // Could not retrieve information about some party members
    LFG_JOIN_DUNGEON_INVALID                = 11, // One or more dungeons was not valid
    LFG_JOIN_DESERTER                       = 12, // You can not queue for dungeons until your deserter debuff wears off
    LFG_JOIN_PARTY_DESERTER                 = 13, // One or more party members has a deserter debuff
    LFG_JOIN_RANDOM_COOLDOWN                = 14, // You can not queue for random dungeons while on random dungeon cooldown
    LFG_JOIN_PARTY_RANDOM_COOLDOWN          = 15, // One or more party members are on random dungeon cooldown
    LFG_JOIN_TOO_MUCH_MEMBERS               = 16, // You can not enter dungeons with more that 5 party members
    LFG_JOIN_USING_BG_SYSTEM                = 17, // You can not use the dungeon system while in BG or arenas
    LFG_JOIN_FAILED2                        = 18, // RoleCheck Failed
};

enum e_LfgEventType
{
    LFG_EVENT_PLAYER_LEAVE_QUEUE           ,
    LFG_EVENT_GROUP_LEAVE_QUEUE            ,
    LFG_EVENT_PLAYER_LOG_ON                ,
    LFG_EVENT_PLAYER_LOG_OFF               ,
    LFG_EVENT_PLAYER_LEAVED_GROUP          ,
    LFG_EVENT_PROPOSAL_RESULT              ,
    LFG_EVENT_AREA_TRIGGER                 ,
    LFG_EVENT_TELEPORT_PLAYER              ,
    LFG_EVENT_SET_ROLES                    ,
    LFG_EVENT_KICK_VOTE                    ,
    LFG_EVENT_INIT_KICK_VOTE               ,
};

enum e_LfgType
{
    LFG_TYPE_NONE                           = 0,
    LFG_TYPE_DUNGEON                        = 1,
    LFG_TYPE_RAID                           = 2,
    LFG_TYPE_QUEST                          = 3,
    LFG_TYPE_ZONE                           = 4,
    LFG_TYPE_HEROIC_DUNGEON                 = 5,
    LFG_TYPE_RANDOM_DUNGEON                 = 6
};

enum e_LfgGroupType
{
    LFG_GROUPTYPE_CLASSIC                   = 1,
    LFG_GROUPTYPE_BC_NORMAL                 = 2,
    LFG_GROUPTYPE_BC_HEROIC                 = 3,
    LFG_GROUPTYPE_WTLK_NORMAL               = 4,
    LFG_GROUPTYPE_WTLK_HEROIC               = 5,
    LFG_GROUPTYPE_CLASSIC_RAID              = 6,
    LFG_GROUPTYPE_BC_RAID                   = 7,
    LFG_GROUPTYPE_WTLK_RAID_10              = 8,
    LFG_GROUPTYPE_WTLK_RAID_25              = 9,
    LFG_GROUPTYPE_WORLD_EVENT               = 11,
};

#define MAX_LFG_GROUPTYPE                   12

enum e_LfgDungeonLockReason
{
    LFG_LOCKSTATUS_OK                        = 0, // Internal use only
    LFG_LOCKSTATUS_INSUFFICIENT_EXPANSION    = 1,
    LFG_LOCKSTATUS_TOO_LOW_LEVEL             = 2,
    LFG_LOCKSTATUS_TOO_HIGH_LEVEL            = 3,
    LFG_LOCKSTATUS_TOO_LOW_GEAR_SCORE        = 4,
    LFG_LOCKSTATUS_TOO_HIGH_GEAR_SCORE       = 5,
    LFG_LOCKSTATUS_RAID_LOCKED               = 6,
    LFG_LOCKSTATUS_ATTUNEMENT_TOO_LOW_LEVEL  = 1001,
    LFG_LOCKSTATUS_ATTUNEMENT_TOO_HIGH_LEVEL = 1002,
    LFG_LOCKSTATUS_QUEST_NOT_COMPLETED       = 1022,
    LFG_LOCKSTATUS_MISSING_ITEM              = 1025,
    LFG_LOCKSTATUS_NOT_IN_SEASON             = 1031,
};

enum LfgRandomDungeonEntries
{
    LFG_ALL_DUNGEONS                        = 0,
    LFG_RANDOM_CLASSIC                      = 258,
    LFG_RANDOM_BC_NORMAL                    = 259,
    LFG_RANDOM_BC_HEROIC                    = 260,
    LFG_RANDOM_LK_NORMAL                    = 261,
    LFG_RANDOM_LK_HEROIC                    = 262,
};

enum e_LfgUpdateType
{
    LFG_UPDATETYPE_LEADER                   = 1,
    LFG_UPDATETYPE_ROLECHECK_ABORTED        = 4,
    LFG_UPDATETYPE_JOIN_PROPOSAL            = 5,
    LFG_UPDATETYPE_ROLECHECK_FAILED         = 6,
    LFG_UPDATETYPE_REMOVED_FROM_QUEUE       = 7,
    LFG_UPDATETYPE_PROPOSAL_FAILED          = 8,
    LFG_UPDATETYPE_PROPOSAL_DECLINED        = 9,
    LFG_UPDATETYPE_GROUP_FOUND              = 10,
    LFG_UPDATETYPE_ADDED_TO_QUEUE           = 12,
    LFG_UPDATETYPE_PROPOSAL_BEGIN           = 13,
    LFG_UPDATETYPE_CLEAR_LOCK_LIST          = 14,
    LFG_UPDATETYPE_GROUP_MEMBER_OFFLINE     = 15,
    LFG_UPDATETYPE_GROUP_DISBAND            = 16,
};

enum e_LfgActionType
{
    LFG_ACTION_REMOVE_PLAYER_FROM_QUEUE_DELAYED = 1,
    LFG_ACTION_REMOVE_PLAYER_FROM_QUEUE_NOW = 2,
    LFG_ACTION_REMOVE_GROUP_FROM_QUEUE_NOW  = 3,
    LFG_ACTION_ADD_PLAYER_TO_QUEUE          = 4,
    LFG_ACTION_ADD_GROUP_TO_QUEUE           = 5,
    LFG_ACTION_TELEPORT_PLAYER              = 6,
    LFG_ACTION_REVALID_QUEUE                = 7,
    LFG_ACTION_SEND_PROPOSAL                = 8,
    LFG_ACTION_UPDATE_PROPOSAL              = 9,
    LFG_ACTION_PROPOSAL_FAILLED             = 10,
    LFG_ACTION_PROPOSAL_DECLINED            = 11,
    LFG_ACTION_CREATE_GROUP                 = 12,
    LFG_ACTION_SEND_GROUP_ROLES_CHECK       = 13,
    LFG_ACTION_SET_ROLESCHECK_ANSWER        = 14,
    LFG_ACTION_DELAYED_GROUP_UPDATE         = 15,
    LFG_ACTION_INIT_KICK_VOTE               = 16,
    LFG_ACTION_SET_VOTEKICK_ANSWER          = 17,
};

enum LfgProposalState
{
    LFG_PROPOSAL_INITIATING                 = 0,
    LFG_PROPOSAL_FAILED                     = 1,
    LFG_PROPOSAL_SUCCESS                    = 2,
};

enum LfgTeleportError
{
    LFG_TELEPORTERROR_OK                    = 0, // Internal use
    LFG_TELEPORTERROR_PLAYER_DEAD           = 1,
    LFG_TELEPORTERROR_FALLING               = 2,
    //LFG_TELEPORTERROR_UNK2                = 3, // You can't do that right now
    LFG_TELEPORTERROR_FATIGUE               = 4,
    //LFG_TELEPORTERROR_UNK3                = 5, // No reaction
    LFG_TELEPORTERROR_INVALID_LOCATION      = 6,
    //LFG_TELEPORTERROR_UNK4                = 7, // You can't do that right now
    //LFG_TELEPORTERROR_UNK5                = 8, // You can't do that right now
};

enum e_LfgRoleCheckResult
{
    LFG_ROLECHECK_FINISHED                  = 1, // Role check finished
    LFG_ROLECHECK_INITIALITING              = 2, // Role check begins
    LFG_ROLECHECK_MISSING_ROLE              = 3, // Someone didn't selected a role after 2 mins
    LFG_ROLECHECK_WRONG_ROLES               = 4, // Can't form a group with that role selection
    LFG_ROLECHECK_ABORTED                   = 5, // Someone leave the group
    LFG_ROLECHECK_NO_ROLE                   = 6, // Someone selected no role
};

enum e_LfgPlayerStatus
{
    LFG_PLAYER_STATUS_INITIALIZING          = 0,
    LFG_PLAYER_STATUS_IN_QUEUE              = 1,
    LFG_PLAYER_STATUS_IN_ROLECHECK          = 2,
    LFG_PLAYER_STATUS_IN_PROPOSAL           = 3,
    LFG_PLAYER_STATUS_LOGGED_OFF            = 4,
};

//=============================================================================
//=============================================================================
struct s_LfgMemberInfo
{
    ObjectGuid              Guid;
    uint8                   ChosenRole;
    uint8                   Roles;
    uint32                  AverageStuffLvl;

    bool                    LoggedOff;

    s_LfgMemberInfo()
    {
        ChosenRole = LFG_NO_ROLE;
    }
};

typedef std::map  < uint32, s_LfgMemberInfo > tMemberInfoMap;
struct s_GuidInfo
{
    ObjectGuid              Guid;
    Team                    team;
    time_t                  JoinedTime;

    pLfgDungeonSet          Dungeon;
    uint32                  ChosenRole;             // Role decided by system to make viable group
    uint32                  Roles;                  // All possible roles player wanted

    e_LfgPlayerStatus       Status;
    uint32                  StatusId;

    bool                    PreGrouped;
    uint32                  GroupId;
    bool                    IsContinue;
    tMemberInfoMap          MemberInfo;

    uint32                  AverageStuffLvl;

    bool                    LoggedOff;

    s_GuidInfo()
    {
        Dungeon             = NULL;
        Roles               = 0;
        JoinedTime          = 0;
        LoggedOff           = false;
        PreGrouped          = false;
        IsContinue          = false;
        ChosenRole          = LFG_NO_ROLE;
        Status              = LFG_PLAYER_STATUS_INITIALIZING;
    }
};

// Comparaison class for Set of guid
struct LfgGuidInfoSetCompClass
{
  bool operator() (s_GuidInfo const* gi_a, s_GuidInfo const* gi_b)
  {return gi_a->JoinedTime < gi_b->JoinedTime;}
};

typedef std::multiset < s_GuidInfo*, LfgGuidInfoSetCompClass > tGuidInfoSet;
typedef std::map  < uint32, s_GuidInfo* > tGuidInfoMap;

struct s_LfgDungeonStats
{
    uint32              Tanks;
    uint32              Healers;
    uint32              DPS;
    uint32              AverageWait;
    s_LfgDungeonStats()
    {
        Tanks       = 0;
        Healers     = 0;
        DPS         = 0;
        AverageWait = std::numeric_limits< uint32 >::max();
    }
};

struct s_LfgDungeonInfo
{
    tGuidInfoSet            GuidInfoSet;
    tGuidInfoMap            GuidInfoMap;
    s_LfgDungeonStats       DungeonStats;
};

// Pre declaration for typedef
struct s_LfgReward;
//struct s_GuidInfo;

struct s_LfgActionMsg;
struct s_LfgDungeonInfo;
struct s_LfgGroupCandidat;


struct s_LfgRolesInfo;
struct s_LfgGroupMemberInfo;
struct s_LfgDungeonStats;
struct s_LfgProposalResult;
struct s_LfgNewGroup;


// All typedef

typedef std::list < LFGDungeonEntry const* > tLfgDungeonList;

typedef std::multimap < uint32, s_LfgReward const* > tLfgRewardMap;
typedef std::map  < uint32, uint8 > tLfgRolesCheck;
typedef std::shared_ptr < s_LfgGroupCandidat > pLfgGroupCandidat;
typedef std::shared_ptr < tGuidInfoSet > pLfgGuidInfoSet;
typedef tGuidInfoSet::iterator tGuidInfoSetItr;
typedef std::map  < uint32, s_GuidInfo > tGuidInfoQueue;
typedef std::map  < Team, tGuidInfoQueue > tAllGuidInfoQueue;
typedef std::list < s_LfgActionMsg* > tLfgActionMsgList;
typedef std::map  < uint32, s_LfgDungeonInfo > tLfgDungeonsStatsMap;
typedef std::map  < Team, tLfgDungeonsStatsMap > tLfgAllDungeonsStatsMap;
typedef tLfgDungeonsStatsMap::const_iterator tLfgDungeonsStatsItr;
struct LfgDungeonsStatSetCompClass
{
  bool operator() (const tLfgDungeonsStatsItr& gi_a, const tLfgDungeonsStatsItr& gi_b)
  {return ((*gi_a->second.GuidInfoSet.begin())->JoinedTime < (*gi_a->second.GuidInfoSet.begin())->JoinedTime);}
};

// Multiset of queued player to specified dungeons. Data of this multiset is an iterator of m_DungeonsStatsMap.
// This way we can do fast sorting with all needed player info.
// Multiset is necessary at least because one player can be first(oldest queued player) in more than one dungeon queue.
// Todo : try to avoid this mulltiset.
typedef std::multiset < tLfgDungeonsStatsItr, LfgDungeonsStatSetCompClass > tLfgDungeonsStatsSet;
typedef std::map < Team, tLfgDungeonsStatsSet > tLfgAllDungeonsStatsSet;
typedef std::vector < LFGDungeonEntry const* > tLfgDungeonVector;
typedef std::map  < uint32, LFGDungeonEntry const* > tLfgDungeonMap;
typedef std::map  < uint32, s_LfgProposal > tLfgProposalMap;
typedef tGuidInfoQueue::iterator tGuidInfoQueueItr;
typedef tGuidInfoQueue::const_iterator tGuidInfoQueueConstItr;
typedef std::map  < uint32, s_LfgRolesCheck > tRolesCheckMap;
typedef std::list < uint8 > tLfgRolesList;
typedef std::map  < uint32, s_LfgRolesInfo > tLfgRolesMap;
typedef std::map < uint32, s_LfgGroupMemberInfo > tLfgGroupMemberInfo;
typedef std::shared_ptr < s_LfgDungeonStats > pLfgDungeonStats;
typedef std::map < uint32, bool > tLfgResultMap;
typedef std::list < s_GuidInfo* > tGuidInfoList;
typedef std::shared_ptr < s_LfgNewGroup > pLfgNewGroup;
typedef std::list < pLfgNewGroup > tNewGroupList;
typedef std::map < uint32, s_LfgKickInfo > tLfgKicksMap;

// Reward info
struct s_LfgReward
{
    uint32 maxLevel;
    struct
    {
        uint32 questId;
        uint32 variableMoney;
        uint32 variableXP;
    } reward[2];

    s_LfgReward(uint32 _maxLevel, uint32 firstQuest, uint32 firstVarMoney, uint32 firstVarXp, uint32 otherQuest, uint32 otherVarMoney, uint32 otherVarXp)
        : maxLevel(_maxLevel)
    {
        reward[0].questId = firstQuest;
        reward[0].variableMoney = firstVarMoney;
        reward[0].variableXP = firstVarXp;
        reward[1].questId = otherQuest;
        reward[1].variableMoney = otherVarMoney;
        reward[1].variableXP = otherVarXp;
    }
};

struct s_LfgRolesInfo
{
    ObjectGuid  guid;
    uint32      Roles;
};

struct s_LfgGroupNeed
{
    uint32          Tanks;
    uint32          Healers;
    uint32          Dps;
    tLfgRolesMap    mixRolesMap;
    bool            Initialised;
    s_LfgGroupNeed()
    {
        Initialised = false;
        Tanks       = LFG_MIN_TANK;
        Healers     = LFG_MIN_HEALER;
        Dps         = LFG_MIN_DPS;
    }
};

// Dungeon and reason why player can't join
struct s_LfgLockStatus
{
   LFGDungeonEntry const* dungeonInfo;
   e_LfgDungeonLockReason lockType;
};

struct s_LfgNewGroup
{
    tGuidInfoSet            NewGroup;
    uint32                  Homogeneity;
    uint32                  DungeonId;
    LFGDungeonEntry const*  DungeonEntry;
};

struct s_LfgProposal
{
    uint32                  ID;
    tLfgResultMap           GuidResultMap;
    LfgProposalState        State;
    pLfgNewGroup            NewGroupInfo;
    time_t                  ProposalExpire;
    s_LfgProposal()
    {
    }
};

struct s_LfgActionMsg
{
    e_LfgActionType     Action;
    ObjectGuid          Guid;
    ObjectGuid          Guid2;
    uint32              Uint32Value;
    uint32              Uint32Value2;
    pLfgDungeonSet      DungeonSet;
    uint32              ElapsedTime;
    Player*             Plr;
    pLfgNewGroup        newGroupInfo;
    std::string         Reason;


    s_LfgActionMsg(e_LfgActionType action, const ObjectGuid& guid, uint32 uvalue = 0, uint32 uvalue2 = 0)
    {
        Action = action;
        Guid = guid;
        Uint32Value = uvalue;
        Uint32Value2 = uvalue2;
        DungeonSet = NULL;
        ElapsedTime = 0;
    }
    s_LfgActionMsg(e_LfgActionType action, const ObjectGuid& guid, const ObjectGuid& guid2, const char* reason)
    {
        Action = action;
        Guid = guid;
        Guid2 = guid2;
        Reason = reason;
        Uint32Value = 0;
        Uint32Value2 = 0;
        DungeonSet = NULL;
        ElapsedTime = 0;
    }
    s_LfgActionMsg(e_LfgActionType action, Player* plr, pLfgDungeonSet dungeonSet, uint32 uvalue)
    {
        Action = action;
        Plr = plr;
        Uint32Value = uvalue;
        Uint32Value2 = 0;
        DungeonSet = dungeonSet;
        ElapsedTime = 0;
    }
    s_LfgActionMsg(e_LfgActionType action, pLfgNewGroup newgroupinfo)
    {
        Action = action;
        Uint32Value = 0;
        Uint32Value2 = 0;
        DungeonSet = NULL;
        ElapsedTime = 0;
        newGroupInfo = newgroupinfo;
    }
    s_LfgActionMsg(e_LfgActionType action, uint32 uvalue = 0, uint32 uvalue2 = 0)
    {
        Action = action;
        Uint32Value = uvalue;
        Uint32Value2 = uvalue2;
        DungeonSet = NULL;
        ElapsedTime = 0;
    }
};

struct s_LfgGroupCandidat
{
    s_GuidInfo const*       GuidInfo;
    pLfgGuidInfoSet         CandidatInfoSet;
};

struct s_LfgRolesCheck
{
    ObjectGuid              LeaderGuid;
    s_GuidInfo const*       GuidInfo;
    e_LfgRoleCheckResult    result;
    pLfgDungeonSet          dungeons;
    time_t                  expireTime;
    tLfgRolesMap            RolesInfo;
};

struct s_LfgKickInfo
{
    time_t                  ExpireTime;                                // Time left to vote
    bool                    InProgress;                                // Vote in progress
    uint32                  Accepted;                                  // Player accepted the kick
    tLfgResultMap           Votes;                                     // Player votes
    ObjectGuid              VictimGuid;                                // Player guid to be kicked (can't vote)
    std::string             Reason;                                    // kick reason
};

struct s_LfgPartyResult
{
    PartyResult             result;
    uint32                  seconds;
};

class LFGHelper
{
public:
    void                SendLFGUpdate(Player* plr, e_LfgUpdateType updateType);
    void                SendLFGUpdate(ObjectGuid guid, e_LfgUpdateType updateType);
};

class LFGDungeonMgr : public LFGHelper
{
public:
    LFGDungeonMgr();
    ~LFGDungeonMgr();
    void                Initialize();

    tLfgDungeonList*    GetDungeonsForPlayer(Player *plr);
    tLfgDungeonList*    GetOptionalDungeons(Player *plr);
    tLfgLockStatusList* GetDungeonsLock(Player *plr, tLfgDungeonMap* dungeons = NULL);
    tLfgLockStatusMap*  GetPartyLockStatusDungeons(Player* plr);

    tLfgDungeonSet*     GetDungeonListByType(uint32 dungeonType);
    LFGDungeonEntry const* GetRandomDungeon(uint32 dungeonId);
    //tLfgDungeonList*    GetRandomDungeons(Player *plr);

private:

    tLfgDungeonMap      m_AllDungList;
    tLfgDungeonVector   m_DungeonClassicList;
    tLfgDungeonVector   m_DungeonBCList;
    tLfgDungeonVector   m_DungeonBCHeroList;
    tLfgDungeonVector   m_DungeonWOTLKList;
    tLfgDungeonVector   m_DungeonWOTLKHeroList;
    tLfgDungeonVector   m_DungeonWorldEventList;
};

typedef std::list < ACE_thread_t > tLFGThreadId;

class LFGQMgr : public LFGDungeonMgr
{
public:
   LFGQMgr();
   ~LFGQMgr();

    uint32              GetLFGSize(Team team) { return m_AllGuidInfoQueue[team].size(); }
    e_LfgJoinResult     AddPlayerToQueue(Player* plr, pLfgDungeonSet dungeonSet, uint32 roles);
    bool                RemovePlayerFromQueue(ObjectGuid guid);
    void                SetGroupRoles(s_LfgRolesCheck& rolesCheck);
    void                SetGuidStatus(ObjectGuid const& guid, e_LfgPlayerStatus status, uint32 statusId = 0);
    void                SetGuidStatus(s_GuidInfo* guidInfo, e_LfgPlayerStatus status, uint32 statusId = 0);
    void                SetGuidStatus(tGuidInfoSet* newGroup, e_LfgPlayerStatus status, uint32 statusId);
    void                RemoveGuidInfoFromStat(s_GuidInfo* guidInfo);
    void                AddGuidInfoToStat(s_GuidInfo* guidInfo);
    uint32              GetSetSize(tGuidInfoSet const* guidSet);
    pLfgNewGroup        AddGuidInfoToDungeonQueue(s_GuidInfo* guidInfo);

    s_GuidInfo const*   GetGuidInfos(ObjectGuid guid);
    s_GuidInfo const*   GetGuidInfos(ObjectGuid guid, Team team); // Fast way

    bool                IsInQueue(ObjectGuid const& guid);
    bool                IsOnLine(ObjectGuid const& guid);
    void                SetOnLineStatus(ObjectGuid const& guid, bool status);
    void                SetProposal(ObjectGuid const& guid, bool answer);
    void                Initialize();
    void                UpdateDungeonsStatsMap(Team team);
    tGuidInfoQueueConstItr QueueBegin(Team team){ return m_AllGuidInfoQueue[team].begin();}
    tGuidInfoQueueConstItr QueueEnd(Team team){ return m_AllGuidInfoQueue[team].end();}

    pLfgDungeonSet GetDungeonsByGuid(ObjectGuid guid);
    pLfgDungeonStats GetDungeonStats(pLfgDungeonSet dungeonSet, Team team);


    void                ShowQueueInfo(Team team);
    void SaveToDB();
    void LoadFromDB();

private:
    tAllGuidInfoQueue   m_AllGuidInfoQueue;     // All faction queue
    tGuidInfoMap        m_GuidInfoMapIndex;     // For fast access to GuidInfo
    tLfgAllDungeonsStatsMap m_AllDungeonsStatsMap;
    tLfgAllDungeonsStatsSet m_AllDungeonsStatsSet;
    bool                m_StatsUpdateAlliance;
    bool                m_StatsUpdateHorde;

private:
    s_GuidInfo*         GetGuidInfo(ObjectGuid guid);
    tGuidInfoSet*       CheckCompatibility(tGuidInfoSet const* dispoGuid, tGuidInfoSet const* groupedGuid = NULL);
    bool                GetGroup(tGuidInfoSet* dispoGuid, tGuidInfoSet& groupedSet, uint32 neededTank = LFG_MIN_TANK, uint32 neededHealer = LFG_MIN_HEALER, uint32 neededDps = LFG_MIN_DPS);
    bool                GetQueueIndex(ObjectGuid guid, Team& team, tGuidInfoQueueItr& itr);
    Team                GetTeam(ObjectGuid guid);
    void                EvalGroupStuff(pLfgNewGroup groupInfo);
    virtual void        GroupFound(pLfgNewGroup groupInfo) = 0;

};

class ThreadSafeActionList
{
public :
    ThreadSafeActionList()
    {
        m_CurrItr == m_List.end();
        m_NextItr == m_List.end();
    }
    void                Add(s_LfgActionMsg* pMess);
    //void                Add2(e_LfgActionType actionType, ...);
    s_LfgActionMsg*     GetFirstMsg();
    s_LfgActionMsg*     GetMsg();
    void                RemoveCurrent();
    void                Remove(e_LfgActionType action, ObjectGuid guid);

private :
    tLfgActionMsgList           m_List;
    tLfgActionMsgList::iterator m_CurrItr;
    tLfgActionMsgList::iterator m_NextItr;
    ACE_Thread_Mutex            m_Mutex;
};

class LFGMgr : public LFGQMgr
{
public:
    LFGMgr();
    ~LFGMgr();

    void                Initialize();
    void                BuildRewardBlock(WorldPacket &data, uint32 dungeon, Player *plr);
    void                LfgJoin(Player* plr, pLfgDungeonSet DungeonSet, uint8 Roles);
    void                GroupJoin(ObjectGuid guid, pLfgDungeonSet DungeonSet, uint8 Roles);
    void                LfgEvent(e_LfgEventType eventType, ObjectGuid guid = ObjectGuid(),...);

    s_LfgPartyResult    CanUninviteFromGroup(Group* grp, Player* plr) const;

    static void*        UpdateThread(void*);


private:
    tRolesCheckMap      m_RolesCheckMap;
    tLfgProposalMap     m_ProposalMap;
    uint32              m_ProposalId;                           // Used to generate different ID for proposalID
    tLfgKicksMap        m_KicksMap;

    ThreadSafeActionList  m_ActionMsgList;                      // Action need to be processed

private:

    void                DoCreateGroup(s_LfgProposal* prop);
    void                DoCancelRoleCheck(s_LfgRolesCheck* rolesCheck);
    void                DoCancelProposal(uint32 proposalId, ObjectGuid senderGuid, e_LfgUpdateType raison);
    void                DoDeleteExpiredProposal();
    void                DoDeleteExpiredVoteKick();
    void                DoDeleteExpiredRolesCheck();
    void                DoFindGroup(Team team);
    void                DoInitVoteKick(ObjectGuid const& senderGuid, ObjectGuid const& victimGuid, std::string reason);
    void                DoProcessActionMsg(uint32 diff);
    bool                DoRemovePlayerFromQueue(ObjectGuid guid, e_LfgUpdateType raison = LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
    void                DoSendGroupRolesCheck(Player* plr, pLfgDungeonSet dungeonSet, uint32 Roles);
    void                DoSendProposal(pLfgNewGroup newGroupInfo);
    void                DoSetKickVote(ObjectGuid const& guid, bool vote);
    void                DoSetRolesCheckAnswer(ObjectGuid guid, uint32 roles);
    void                DoSetProposalAnswer(ObjectGuid guid, uint32 propId, bool answer);
    void                DoTeleportPlayer(Player* plr, WorldLocation const* location);
    void                DoUpdateQueuedStatus(Team team);

    bool                CheckValidRoles(tLfgRolesList& roleList);
    uint32              GetNewProposalID();

    virtual void        GroupFound(pLfgNewGroup groupInfo);
};

#define sLFGMgr MaNGOS::Singleton<LFGMgr>::Instance()
#endif