CREATE TABLE `client_version` (
  `account_id` int(11) NOT NULL default '0',
  `version_` tinyint(5) NOT NULL default '0',
  PRIMARY KEY  (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;