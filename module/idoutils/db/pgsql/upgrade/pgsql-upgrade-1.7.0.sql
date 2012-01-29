-- -----------------------------------------
-- upgrade path for Icinga IDOUtils 1.7.0
--
-- -----------------------------------------
-- Copyright (c) 2011 Icinga Development Team (http://www.icinga.org)
--
-- Please check http://docs.icinga.org for upgrading information!
-- -----------------------------------------

-- -----------------------------------------
--#2274
-- -----------------------------------------
create index statehist_state_idx on icinga_statehistory(objectid,state);

-- -----------------------------------------
-- update dbversion
-- -----------------------------------------

INSERT INTO icinga_dbversion (name, version) VALUES ('idoutils', '1.6.0') ON DUPLICATE KEY UPDATE version='1.6.0';

