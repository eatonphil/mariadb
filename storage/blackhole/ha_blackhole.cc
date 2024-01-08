/* Copyright (c) 2005, 2012, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation // gcc: Class implementation
#endif

#define MYSQL_SERVER 1
#include <my_global.h>
#include "sql_priv.h"
#include "unireg.h"
#include "ha_blackhole.h"
#include "sql_class.h" // THD, SYSTEM_THREAD_SLAVE_SQL

/* Static declarations for handlerton */

static handler *blackhole_create_handler(handlerton *hton, TABLE_SHARE *table,
                                         MEM_ROOT *mem_root)
{
  return new (mem_root) ha_blackhole(hton, table);
}

/* Static declarations for shared structures */

static mysql_mutex_t blackhole_mutex;
static HASH blackhole_open_tables;

static st_blackhole_share *get_share(const char *table_name);
static void free_share(st_blackhole_share *share);

static MememDatabase *database;

// TODO: this is not thread safe.
static int memem_table_index(const char *name)
{
  int i;
  assert(database->tables.size() < INT_MAX);
  for (i= 0; i < (int) database->tables.size(); i++)
  {
    if (strcmp(database->tables[i]->name, name) == 0)
    {
      return i;
    }
  }

  return -1;
}

/*****************************************************************************
** BLACKHOLE tables
*****************************************************************************/

int ha_blackhole::open(const char *name, int mode, uint test_if_locked)
{
  if (!(share= get_share(name)))
  {
    return HA_ERR_OUT_OF_MEM;
  }

  thr_lock_data_init(&share->lock, &lock, NULL);
  return 0;
}

int ha_blackhole::close(void)
{
  free_share(share);
  return 0;
}

int ha_blackhole::create(const char *name, TABLE *table_arg,
                         HA_CREATE_INFO *create_info)
{
  if (memem_table_index(name) != -1)
  {
    // For some reason even with `DROP TABLE IF EXISTS x`,
    // delete_table() is not called. So we get into this position
    // where sometimes the storage engine tries to create a table that
    // already exists.
    delete_table(name);
  }

  // TODO: this is not thread safe.
  MememTable *t= new MememTable;
  t->name= strdup(name);
  database->tables.push_back(t);
  DBUG_PRINT("info", ("[MEMEM] Created table '%s'.", name));

  return 0;
}

int ha_blackhole::delete_table(const char *name)
{
  int index= memem_table_index(name);
  if (index == -1)
  {
    DBUG_PRINT("info", ("[MEMEM] Table '%s' already deleted.", name));
    // Already deleted.
    return 0;
  }

  // TODO: this is not thread safe.
  MememTable *t= database->tables[index];
  for (auto &row : t->rows)
  {
    delete row;
  }
  free(t->name);
  delete t;

  database->tables.erase(database->tables.begin() + index);
  DBUG_PRINT("info", ("[MEMEM] Deleted table '%s'.", name));

  return 0;
};

void ha_blackhole::reset_memem_table()
{
  // Reset table cursor.
  current_position= 0;

  std::string full_name= "./" + std::string(table->s->db.str) + "/" +
                         std::string(table->s->table_name.str);
  DBUG_PRINT("info", ("[MEMEM] Resetting to '%s'.", full_name.c_str()));
  int index= memem_table_index(full_name.c_str());
  assert(index >= 0);
  assert(index < (int) database->tables.size());

  // TODO: not thread safe.
  memem_table= database->tables[index];
}

int ha_blackhole::write_row(const uchar *buf)
{
  if (memem_table == NULL)
  {
    reset_memem_table();
  }

  // Assume there are no NULLs.
  buf++;

  std::vector<uchar> *row= new std::vector<uchar>;
  uint i= 0;
  while (table->field[i])
  {
    if (table->field[i]->type() != MYSQL_TYPE_LONG)
    {
      DBUG_PRINT("info", ("Unsupported field type."));
      return 1;
    }

    row->insert(std::end(*row), buf, buf + sizeof(int));
    buf+= sizeof(int);
    i++;
  }

  memem_table->rows.push_back(row);

  return 0;
}

int ha_blackhole::rnd_init(bool scan)
{
  reset_memem_table();
  return 0;
}

int ha_blackhole::rnd_next(uchar *buf)
{
  if (current_position == memem_table->rows.size())
  {
    // Reset the in-memory table to make logic errors more obvious.
    memem_table= NULL;
    return HA_ERR_END_OF_FILE;
  }
  assert(current_position < memem_table->rows.size());

  uchar *ptr= buf;
  *ptr= 0;
  ptr++;

  // Rows internally are stored in the same format that MariaDB
  // wants. So we can just copy them over.
  std::vector<uchar> *row= memem_table->rows[current_position];
  std::copy(row->begin(), row->end(), ptr);

  current_position++;
  return 0;
}

THR_LOCK_DATA **ha_blackhole::store_lock(THD *thd, THR_LOCK_DATA **to,
                                         enum thr_lock_type lock_type)
{
  DBUG_ENTER("ha_blackhole::store_lock");
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
  {
    /*
      Here is where we get into the guts of a row level lock.
      If TL_UNLOCK is set
      If we are not doing a LOCK TABLE or DISCARD/IMPORT
      TABLESPACE, then allow multiple writers
    */

    if ((lock_type >= TL_WRITE_CONCURRENT_INSERT && lock_type <= TL_WRITE) &&
        !thd_in_lock_tables(thd) && !thd_tablespace_op(thd))
      lock_type= TL_WRITE_ALLOW_WRITE;

    /*
      In queries of type INSERT INTO t1 SELECT ... FROM t2 ...
      MySQL would use the lock TL_READ_NO_INSERT on t2, and that
      would conflict with TL_WRITE_ALLOW_WRITE, blocking all inserts
      to t2. Convert the lock to a normal read lock to allow
      concurrent inserts to t2.
    */

    if (lock_type == TL_READ_NO_INSERT && !thd_in_lock_tables(thd))
      lock_type= TL_READ;

    lock.type= lock_type;
  }
  *to++= &lock;
  DBUG_RETURN(to);
}

static st_blackhole_share *get_share(const char *table_name)
{
  st_blackhole_share *share;
  uint length;

  length= (uint) strlen(table_name);
  mysql_mutex_lock(&blackhole_mutex);

  if (!(share= (st_blackhole_share *) my_hash_search(
            &blackhole_open_tables, (uchar *) table_name, length)))
  {
    if (!(share= (st_blackhole_share *) my_malloc(
              PSI_INSTRUMENT_ME, sizeof(st_blackhole_share) + length,
              MYF(MY_WME | MY_ZEROFILL))))
      goto error;

    share->table_name_length= length;
    strmov(share->table_name, table_name);

    if (my_hash_insert(&blackhole_open_tables, (uchar *) share))
    {
      my_free(share);
      share= NULL;
      goto error;
    }

    thr_lock_init(&share->lock);
  }
  share->use_count++;

error:
  mysql_mutex_unlock(&blackhole_mutex);
  return share;
}

static void free_share(st_blackhole_share *share)
{
  mysql_mutex_lock(&blackhole_mutex);
  mysql_mutex_unlock(&blackhole_mutex);
}

static void blackhole_free_key(st_blackhole_share *share)
{
  thr_lock_delete(&share->lock);
  my_free(share);
}

static uchar *blackhole_get_key(st_blackhole_share *share, size_t *length,
                                my_bool not_used __attribute__((unused)))
{
  *length= share->table_name_length;
  return (uchar *) share->table_name;
}

static PSI_mutex_key bh_key_mutex_blackhole;

static PSI_mutex_info all_blackhole_mutexes[]= {
    {&bh_key_mutex_blackhole, "blackhole", PSI_FLAG_GLOBAL}};

void init_blackhole_psi_keys()
{
  const char *category= "blackhole";
  int count;

  if (PSI_server == NULL)
    return;

  count= array_elements(all_blackhole_mutexes);
  PSI_server->register_mutex(category, all_blackhole_mutexes, count);
}

static int blackhole_init(void *p)
{
  handlerton *blackhole_hton;

  init_blackhole_psi_keys();

  blackhole_hton= (handlerton *) p;
  blackhole_hton->db_type= DB_TYPE_BLACKHOLE_DB;
  blackhole_hton->create= blackhole_create_handler;
  blackhole_hton->drop_table= [](handlerton *, const char *) { return -1; };
  blackhole_hton->flags= HTON_CAN_RECREATE;

  mysql_mutex_init(bh_key_mutex_blackhole, &blackhole_mutex,
                   MY_MUTEX_INIT_FAST);
  (void) my_hash_init(PSI_INSTRUMENT_ME, &blackhole_open_tables,
                      system_charset_info, 32, 0, 0,
                      (my_hash_get_key) blackhole_get_key,
                      (my_hash_free_key) blackhole_free_key, 0);

  database= new MememDatabase;

  return 0;
}

static int blackhole_fini(void *p)
{
  my_hash_free(&blackhole_open_tables);
  mysql_mutex_destroy(&blackhole_mutex);
  delete database;
  return 0;
}

struct st_mysql_storage_engine blackhole_storage_engine= {
    MYSQL_HANDLERTON_INTERFACE_VERSION};

maria_declare_plugin(blackhole){
    MYSQL_STORAGE_ENGINE_PLUGIN,
    &blackhole_storage_engine,
    "BLACKHOLE",
    "MySQL AB",
    "/dev/null storage engine (anything you write to it disappears)",
    PLUGIN_LICENSE_GPL,
    blackhole_init, /* Plugin Init */
    blackhole_fini, /* Plugin Deinit */
    0x0100 /* 1.0 */,
    NULL,                          /* status variables                */
    NULL,                          /* system variables                */
    "1.0",                         /* string version */
    MariaDB_PLUGIN_MATURITY_STABLE /* maturity */
} maria_declare_plugin_end;
