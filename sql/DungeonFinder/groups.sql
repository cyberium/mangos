/*
Navicat MySQL Data Transfer

Source Server         : My SQL server1
Source Server Version : 50045
Source Host           : momo-srv:3306
Source Database       : characters

Target Server Type    : MYSQL
Target Server Version : 50045
File Encoding         : 65001

Date: 2011-10-18 15:55:16
*/

SET FOREIGN_KEY_CHECKS=0;
-- ----------------------------
-- Table structure for `groups`
-- ----------------------------
DROP TABLE IF EXISTS `groups`;
CREATE TABLE `groups` (
  `groupId` int(11) unsigned NOT NULL,
  `leaderGuid` int(11) unsigned NOT NULL,
  `mainTank` int(11) unsigned NOT NULL,
  `mainAssistant` int(11) unsigned NOT NULL,
  `lootMethod` tinyint(4) unsigned NOT NULL,
  `looterGuid` int(11) unsigned NOT NULL,
  `lootThreshold` tinyint(4) unsigned NOT NULL,
  `icon1` int(11) unsigned NOT NULL,
  `icon2` int(11) unsigned NOT NULL,
  `icon3` int(11) unsigned NOT NULL,
  `icon4` int(11) unsigned NOT NULL,
  `icon5` int(11) unsigned NOT NULL,
  `icon6` int(11) unsigned NOT NULL,
  `icon7` int(11) unsigned NOT NULL,
  `icon8` int(11) unsigned NOT NULL,
  `groupType` tinyint(1) unsigned NOT NULL,
  `difficulty` tinyint(3) unsigned NOT NULL default '0',
  `raiddifficulty` int(11) unsigned NOT NULL,
  `dungeonId` int(11) NOT NULL,
  PRIMARY KEY  (`groupId`),
  UNIQUE KEY `leaderGuid` (`leaderGuid`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 ROW_FORMAT=FIXED COMMENT='Groups';

-- ----------------------------
-- Records of groups
-- ----------------------------
