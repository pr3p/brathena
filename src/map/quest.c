/****************************************************************************!
*                _           _   _   _                                       *    
*               | |__  _ __ / \ | |_| |__   ___ _ __   __ _                  *  
*               | '_ \| '__/ _ \| __| '_ \ / _ \ '_ \ / _` |                 *   
*               | |_) | | / ___ \ |_| | | |  __/ | | | (_| |                 *    
*               |_.__/|_|/_/   \_\__|_| |_|\___|_| |_|\__,_|                 *    
*                                                                            *
*                                                                            *
* \file src/map/quest.c                                                      *
* Descri��o Prim�ria.                                                        *
* Descri��o mais elaborada sobre o arquivo.                                  *
* \author brAthena, Athena, eAthena                                          *
* \date ?                                                                    *
* \todo ?                                                                    *  
*****************************************************************************/

#include "../common/cbasetypes.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/malloc.h"
#include "../common/nullpo.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "map.h"
#include "pc.h"
#include "npc.h"
#include "itemdb.h"
#include "script.h"
#include "intif.h"
#include "battle.h"
#include "mob.h"
#include "party.h"
#include "unit.h"
#include "log.h"
#include "clif.h"
#include "quest.h"
#include "chrif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>


struct quest_interface quest_s;

/**
 * Searches a quest by ID.
 *
 * @param quest_id ID to lookup
 * @return Quest entry (equals to &quest->dummy if the ID is invalid)
 */
struct quest_db *quest_db(int quest_id) {
	if (quest_id < 0 || quest_id > MAX_QUEST_DB || quest->db_data[quest_id] == NULL)
		return &quest->dummy;
	return quest->db_data[quest_id];
}

/**
 * Sends quest info to the player on login.
 *
 * @param sd Player's data
 * @return 0 in case of success, nonzero otherwise (i.e. the player has no quests)
 */
int quest_pc_login(TBL_PC *sd) {
	int i;

	if(sd->avail_quests == 0)
		return 1;

	clif_quest_send_list(sd);
	clif_quest_send_mission(sd);
	for(i = 0; i < sd->avail_quests; i++) {

	clif_quest_update_objective(sd, &sd->quest_log[i]);
	}

	return 0;
}

/**
 * Adds a quest to the player's list.
 *
 * New quest will be added as Q_ACTIVE.
 *
 * @param sd       Player's data
 * @param quest_id ID of the quest to add.
 * @return 0 in case of success, nonzero otherwise
 */
int quest_add(TBL_PC *sd, int quest_id) {
	int n;
	struct quest_db *qi = quest->db(quest_id);

	if(qi == &quest->dummy) {
		ShowError("quest_add: quest %d not found in DB.\n", quest_id);
		return -1;
	}

	if(quest->check(sd, quest_id, HAVEQUEST) >= 0) {
		ShowError("quest_add: Character %d already has quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}

	n = sd->avail_quests; // Insertion point

	sd->num_quests++;
	sd->avail_quests++;
	RECREATE(sd->quest_log, struct quest, sd->num_quests);

	if(sd->avail_quests != sd->num_quests) {
		// The character has some completed quests, make room before them so that they will stay at the end of the array
		memmove(&sd->quest_log[n+1], &sd->quest_log[n], sizeof(struct quest)*(sd->num_quests-sd->avail_quests));
	}

	memset(&sd->quest_log[n], 0, sizeof(struct quest));

	sd->quest_log[n].quest_id = qi->id;
	if(qi->time)
		sd->quest_log[n].time = (unsigned int)(time(NULL) + qi->time);
	sd->quest_log[n].state = Q_ACTIVE;

	sd->save_quest = true;

	clif_quest_add(sd, &sd->quest_log[n]);
	clif_quest_update_objective(sd, &sd->quest_log[n]);

	if(save_settings&64)
		chrif_save(sd,0);

	return 0;
}

/**
 * Replaces a quest in a player's list with another one.
 *
 * @param sd   Player's data
 * @param qid1 Current quest to replace
 * @param qid2 New quest to add
 * @return 0 in case of success, nonzero otherwise
 */
int quest_change(TBL_PC *sd, int qid1, int qid2) {
	int i;
	struct quest_db *qi = quest->db(qid2);

	if(qi == &quest->dummy) {
		ShowError("quest_change: quest %d not found in DB.\n", qid2);
		return -1;
	}

	if(quest->check(sd, qid2, HAVEQUEST) >= 0) {
		ShowError("quest_change: Character %d already has quest %d.\n", sd->status.char_id, qid2);
		return -1;
	}

	if(quest->check(sd, qid1, HAVEQUEST) < 0) {
		ShowError("quest_change: Character %d doesn't have quest %d.\n", sd->status.char_id, qid1);
		return -1;
	}

	ARR_FIND(0, sd->avail_quests, i, sd->quest_log[i].quest_id == qid1);
	if(i == sd->avail_quests) {
		ShowError("quest_change: Character %d has completed quest %d.\n", sd->status.char_id, qid1);
		return -1;
	}

	memset(&sd->quest_log[i], 0, sizeof(struct quest));
	sd->quest_log[i].quest_id = qi->id;
	if(qi->time)
		sd->quest_log[i].time = (unsigned int)(time(NULL) + qi->time);
	sd->quest_log[i].state = Q_ACTIVE;

	sd->save_quest = true;

	clif_quest_delete(sd, qid1);
	clif_quest_add(sd, &sd->quest_log[i]);
	clif_quest_update_objective(sd, &sd->quest_log[i]);

	if(save_settings&64)
		chrif_save(sd,0);

	return 0;
}

/**
 * Removes a quest from a player's list
 *
 * @param sd       Player's data
 * @param quest_id ID of the quest to remove
 * @return 0 in case of success, nonzero otherwise
 */
int quest_delete(TBL_PC *sd, int quest_id) {
	int i;

	//Search for quest
	ARR_FIND(0, sd->num_quests, i, sd->quest_log[i].quest_id == quest_id);
	if(i == sd->num_quests) {
		ShowError("quest_delete: Character %d doesn't have quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}

	if(sd->quest_log[i].state != Q_COMPLETE)
		sd->avail_quests--;

	if(i < --sd->num_quests) {
		// Compact the array
		memmove(&sd->quest_log[i], &sd->quest_log[i+1], sizeof(struct quest)*(sd->num_quests-i));
	}
	if(sd->num_quests == 0) {
		aFree(sd->quest_log);
		sd->quest_log = NULL;
	} else {
		RECREATE(sd->quest_log, struct quest, sd->num_quests);
	}
	sd->save_quest = true;

	clif_quest_delete(sd, quest_id);

	if(save_settings&64)
		chrif_save(sd,0);

	return 0;
}

/**
 * Map iterator subroutine to update quest objectives for a party after killing a monster.
 *
 * @see map_foreachinrange
 * @param ap Argument list, expecting:
 *           int Party ID
 *           int Mob ID
 */
int quest_update_objective_sub(struct block_list *bl, va_list ap) {
	struct map_session_data *sd;
	int mob_id, party_id;

	nullpo_ret(bl);
	nullpo_ret(sd = (struct map_session_data *)bl);

	party_id = va_arg(ap,int);
	mob_id = va_arg(ap,int);

	if(!sd->avail_quests)
		return 0;
	if(sd->status.party_id != party_id)
		return 0;

	quest->update_objective(sd, mob_id);

	return 1;
}


/**
 * Updates the quest objectives for a character after killing a monster.
 *
 * @param sd     Character's data
 * @param mob_id Monster ID
 */
void quest_update_objective(TBL_PC *sd, int mob_id) {
	int i,j;

	for(i = 0; i < sd->avail_quests; i++) {
		struct quest_db *qi = NULL;

		if(sd->quest_log[i].state != Q_ACTIVE) // Skip inactive quests
			continue;

		qi = quest->db(sd->quest_log[i].quest_id);

		for(j = 0; j < qi->num_objectives; j++) {
			if(qi->mob[j] == mob_id && sd->quest_log[i].count[j] < qi->count[j])  {
				sd->quest_log[i].count[j]++;
				sd->save_quest = true;
				clif_quest_update_objective(sd,&sd->quest_log[i]);
			}
		}
	}
}

/**
 * Updates a quest's state.
 *
 * Only status of active and inactive quests can be updated. Completed quests can't (for now). [Inkfish]
 *
 * @param sd       Character's data
 * @param quest_id Quest ID to update
 * @param qs       New quest state
 * @return 0 in case of success, nonzero otherwise
 */
int quest_update_status(TBL_PC *sd, int quest_id, enum quest_state qs) {
	int i;

	ARR_FIND(0, sd->avail_quests, i, sd->quest_log[i].quest_id == quest_id);
	if(i == sd->avail_quests) {
		ShowError("quest_update_status: Character %d doesn't have quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}

	sd->quest_log[i].state = qs;
	sd->save_quest = true;

	if(qs < Q_COMPLETE) {
		clif_quest_update_status(sd, quest_id, qs == Q_ACTIVE ? true : false);
		return 0;
	}

	// The quest is complete, so it needs to be moved to the completed quests block at the end of the array.

	if(i < (--sd->avail_quests)) {
		struct quest tmp_quest;
		memcpy(&tmp_quest, &sd->quest_log[i],sizeof(struct quest));
		memcpy(&sd->quest_log[i], &sd->quest_log[sd->avail_quests],sizeof(struct quest));
		memcpy(&sd->quest_log[sd->avail_quests], &tmp_quest,sizeof(struct quest));
	}

	clif_quest_delete(sd, quest_id);

	if(save_settings&64)
		chrif_save(sd,0);

	return 0;
}

/**
 * Queries quest information for a character.
 *
 * @param sd       Character's data
 * @param quest_id Quest ID
 * @param type     Check type
 * @return -1 if the quest was not found, otherwise it depends on the type:
 *         HAVEQUEST: The quest's state
 *         PLAYTIME:  2 if the quest's timeout has expired
 *                    1 if the quest was completed
 *                    0 otherwise
 *         HUNTING:   2 if the quest has not been marked as completed yet, and its objectives have been fulfilled
 *                    1 if the quest's timeout has expired
 *                    0 otherwise
 */
int quest_check(TBL_PC *sd, int quest_id, enum quest_check_type type) {
	int i;

	ARR_FIND(0, sd->num_quests, i, sd->quest_log[i].quest_id == quest_id);
	if(i == sd->num_quests)
		return -1;

	switch(type) {
		case HAVEQUEST:
			return sd->quest_log[i].state;
		case PLAYTIME:
			return (sd->quest_log[i].time < (unsigned int)time(NULL) ? 2 : sd->quest_log[i].state == Q_COMPLETE ? 1 : 0);
		case HUNTING:
			if(sd->quest_log[i].state == Q_INACTIVE || sd->quest_log[i].state == Q_ACTIVE) {
				int j;
				struct quest_db *qi = quest->db(sd->quest_log[i].quest_id);
				ARR_FIND(0, MAX_QUEST_OBJECTIVES, j, sd->quest_log[i].count[j] < qi->count[j]);
				if(j == MAX_QUEST_OBJECTIVES)
					return 2;
				if(sd->quest_log[i].time < (unsigned int)time(NULL))
					return 1;
			}
			return 0;
		default:
			ShowError("quest_check_quest: Unknown parameter %d",type);
			break;
	}

	return -1;
}

/**
 * Loads quests from the quest db.
 *
 * @return Number of loaded quests, or -1 if the file couldn't be read.
 */
int quest_read_db(void) {
	int QuestLoop, QueryLoop, MaxQuestLoop = 0;
	struct quest_db entry;

	if(SQL_ERROR == SQL->Query(dbmysql_handle, "SELECT * FROM `%s`", get_database_name(50))) {
		Sql_ShowDebug(dbmysql_handle);
		return -1;
	}

	while(SQL_SUCCESS == SQL->NextRow(dbmysql_handle) && MaxQuestLoop < MAX_QUEST_DB) {
		char *row[9];

		for(QueryLoop = 0; QueryLoop < 9; ++QueryLoop)
			SQL->GetData(dbmysql_handle, QueryLoop, &row[QueryLoop], NULL);

		if (row[0] == NULL)
			continue;

		memset(&entry, 0, sizeof(entry));

		entry.id = atoi(row[0]);

		if (entry.id < 0 || entry.id >= MAX_QUEST_DB) {
			ShowError("quest_read_db: Invalid quest ID '%d' (min: 0, max: %d.)\n", entry.id, MAX_QUEST_DB);
			continue;
		}

		entry.time = atoi(row[1]);

		for (QuestLoop = 0; QuestLoop < MAX_QUEST_OBJECTIVES; QuestLoop++) {
			entry.mob[QuestLoop] = atoi(row[2*QuestLoop+2]);
			entry.count[QuestLoop] = atoi(row[2*QuestLoop+3]);

			if (!entry.mob[QuestLoop] || !entry.count[QuestLoop])
				break;
		}

		entry.num_objectives = QuestLoop;

		if (quest->db_data[entry.id] == NULL)
			quest->db_data[entry.id] = aMalloc(sizeof(struct quest_db));

		memcpy(quest->db_data[entry.id], &entry, sizeof(struct quest_db));
		MaxQuestLoop++;
	}

	ShowSQL("Leitura de '"CL_WHITE"%lu"CL_RESET"' entradas na tabela '"CL_WHITE"%s"CL_RESET"'.\n", MaxQuestLoop, get_database_name(50));
	SQL->FreeResult(dbmysql_handle);
	return 0;
}

/**
 * Map iterator to ensures a player has no invalid quest log entries.
 *
 * Any entries that are no longer in the db are removed.
 *
 * @see map_foreachpc
 * @param ap Ignored
 */
int quest_reload_check_sub(struct map_session_data *sd, va_list ap) {
	int i, j;

	nullpo_ret(sd);

	j = 0;
	for (i = 0; i < sd->num_quests; i++) {
		struct quest_db *qi = quest->db(sd->quest_log[i].quest_id);
		if (qi == &quest->dummy) { // Remove no longer existing entries
			if (sd->quest_log[i].state != Q_COMPLETE) // And inform the client if necessary
				clif_quest_delete(sd, sd->quest_log[i].quest_id);
			continue;
		}
		if (i != j) {
			// Move entries if there's a gap to fill
			memcpy(&sd->quest_log[j], &sd->quest_log[i], sizeof(struct quest));
		}
		j++;
	}
	sd->num_quests = j;
	ARR_FIND(0, sd->num_quests, i, sd->quest_log[i].state == Q_COMPLETE);
	sd->avail_quests = i;

	return 1;
}

/**
 * Clears the quest database for shutdown or reload.
 */
void quest_clear_db(void) {
	int i;

	for (i = 0; i < MAX_QUEST_DB; i++) {
		if (quest->db_data[i]) {
			aFree(quest->db_data[i]);
			quest->db_data[i] = NULL;
		}
	}
}
/**
 * Initializes the quest interface.
 *
 * @param minimal Run in minimal mode (skips most of the loading)
 */
void do_init_quest(void)
{
	quest->read_db();
}

/**
 * Finalizes the quest interface before shutdown.
 */
void do_final_quest(void) {
	quest->clear();
}

/**
 * Reloads the quest database.
 */
void do_reload_quest(void) {
	quest->clear();

	quest->read_db();

	// Update quest data for players, to ensure no entries about removed quests are left over.
	map_foreachpc(&quest_reload_check_sub);
}

/**
 * Initializes default values for the quest interface.
 */
void quest_defaults(void) {
	quest = &quest_s;

	memset(&quest->db, 0, sizeof(quest->db));
	memset(&quest->dummy, 0, sizeof(quest->dummy));
	/* */
	quest->init = do_init_quest;
	quest->final = do_final_quest;
	quest->reload = do_reload_quest;
	/* */
	quest->db = quest_db;
	quest->pc_login = quest_pc_login;
	quest->add = quest_add;
	quest->change = quest_change;
	quest->delete = quest_delete;
	quest->update_objective_sub = quest_update_objective_sub;
	quest->update_objective = quest_update_objective;
	quest->update_status = quest_update_status;
	quest->check = quest_check;
	quest->clear = quest_clear_db;
	quest->read_db = quest_read_db;
}