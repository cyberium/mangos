/*
Navicat MySQL Data Transfer

Source Server         : My SQL server1
Source Server Version : 50045
Source Host           : momo-srv:3306
Source Database       : characters

Target Server Type    : MYSQL
Target Server Version : 50045
File Encoding         : 65001

Date: 2011-10-18 15:55:03
*/

SET FOREIGN_KEY_CHECKS=0;
-- ----------------------------
-- Table structure for `group_member`
-- ----------------------------
DROP TABLE IF EXISTS `group_member`;
CREATE TABLE `group_member` (
  `groupId` int(11) unsigned NOT NULL,
  `memberGuid` int(11) unsigned NOT NULL,
  `assistant` tinyint(1) unsigned NOT NULL,
  `subgroup` smallint(6) unsigned NOT NULL,
  `roles` smallint(6) default NULL,
  `orix` float(11,0) default NULL,
  `oriy` float(11,0) default NULL,
  `oriz` float(11,0) default NULL,
  `mapid` int(11) default NULL,
  `orient` float(11,0) default NULL,
  PRIMARY KEY  (`groupId`,`memberGuid`),
  KEY `Idx_memberGuid` (`memberGuid`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 ROW_FORMAT=FIXED COMMENT='Groups';

-- ----------------------------
-- Records of group_member
-- ----------------------------
