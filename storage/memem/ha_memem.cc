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
#include "sql_class.h"

#include "ha_memem.h"

// WARNING! All accesses of `database` in this code are thread
// unsafe. Since this was written during a hack week, I didn't have
// time to figure out MySQL/MariaDB's runtime well enough to do the
// thread-safe version of this.
static MememDatabase *database;

static int memem_table_index(const char *name)
{
  int i;
  assert(database->tables.size() < INT_MAX);
  for (i= 0; i < (int) database->tables.size(); i++)
  {
    if (strcmp(database->tables[i]->name->c_str(), name) == 0)
    {
      return i;
    }
  }

  return -1;
}

static handler *memem_create_handler(handlerton *hton, TABLE_SHARE *table,
                                     MEM_ROOT *mem_root)
{
  return new (mem_root) ha_memem(hton, table);
}

static int memem_init(void *p)
{
  handlerton *memem_hton;

  memem_hton= (handlerton *) p;
  memem_hton->db_type= DB_TYPE_AUTOASSIGN;
  memem_hton->create= memem_create_handler;
  memem_hton->drop_table= [](handlerton *, const char *name) {
    int index= memem_table_index(name);
    if (index == -1)
    {
      return HA_ERR_NO_SUCH_TABLE;
    }

    database->tables.erase(database->tables.begin() + index);
    DBUG_PRINT("info", ("[MEMEM] Deleted table '%s'.", name));

    return 0;
  };
  memem_hton->flags= HTON_CAN_RECREATE;

  database= new MememDatabase;

  return 0;
}

static int memem_fini(void *p)
{
  delete database;
  return 0;
}

struct st_mysql_storage_engine memem_storage_engine= {
    MYSQL_HANDLERTON_INTERFACE_VERSION};

maria_declare_plugin(memem){
    MYSQL_STORAGE_ENGINE_PLUGIN,
    &memem_storage_engine,
    "MEMEM",
    "MySQL AB",
    "/dev/null storage engine (anything you write to it disappears)",
    PLUGIN_LICENSE_GPL,
    memem_init, /* Plugin Init */
    memem_fini, /* Plugin Deinit */
    0x0100 /* 1.0 */,
    NULL,                          /* status variables                */
    NULL,                          /* system variables                */
    "1.0",                         /* string version */
    MariaDB_PLUGIN_MATURITY_STABLE /* maturity */
} maria_declare_plugin_end;

int ha_memem::create(const char *name, TABLE *table_arg,
                     HA_CREATE_INFO *create_info)
{
  assert(memem_table_index(name) == -1);

  // We only support INTEGER fields for now.
  uint i = 0;
  while (table_arg->field[i]) {
    if (table_arg->field[i]->type() != MYSQL_TYPE_LONG)
      {
	DBUG_PRINT("info", ("Unsupported field type."));
	return 1;
      }

    i++;
  }

  auto t= std::make_shared<MememTable>();
  t->name= std::make_shared<std::string>(name);
  database->tables.push_back(t);
  DBUG_PRINT("info", ("[MEMEM] Created table '%s'.", name));

  return 0;
}

void ha_memem::reset_memem_table()
{
  // Reset table cursor.
  current_position= 0;

  std::string full_name= "./" + std::string(table->s->db.str) + "/" +
                         std::string(table->s->table_name.str);
  DBUG_PRINT("info", ("[MEMEM] Resetting to '%s'.", full_name.c_str()));
  assert(database->tables.size() > 0);
  int index= memem_table_index(full_name.c_str());
  assert(index >= 0);
  assert(index < (int) database->tables.size());

  memem_table= database->tables[index];
}

int ha_memem::write_row(const uchar *buf)
{
  if (memem_table == NULL)
  {
    reset_memem_table();
  }

  // Assume there are no NULLs.
  buf++;

  uint field_count = 0;
  while (table->field[field_count]) field_count++;

  // Store the row in the same format MariaDB gives us.
  auto row= std::make_shared<std::vector<uchar>>(
      buf, buf + sizeof(int) * field_count);
  memem_table->rows.push_back(row);

  return 0;
}

int ha_memem::rnd_init(bool scan)
{
  reset_memem_table();
  return 0;
}

int ha_memem::rnd_next(uchar *buf)
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
  std::shared_ptr<std::vector<uchar>> row= memem_table->rows[current_position];
  std::copy(row->begin(), row->end(), ptr);

  current_position++;
  return 0;
}
