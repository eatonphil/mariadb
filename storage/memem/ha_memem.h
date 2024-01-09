/* Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.

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

#ifdef USE_PRAGMA_INTERFACE
#pragma interface /* gcc class implementation */
#endif

#include "thr_lock.h"
#include "handler.h"
#include "table.h"
#include "sql_const.h"

#include <vector>
#include <memory>

typedef std::vector<uchar> MememRow;

struct MememTable
{
  std::vector<std::shared_ptr<MememRow>> rows;
  std::shared_ptr<std::string> name;
};

struct MememDatabase
{
  std::vector<std::shared_ptr<MememTable>> tables;
};

class ha_memem final : public handler
{
  uint current_position= 0;
  std::shared_ptr<MememTable> memem_table= 0;

public:
  ha_memem(handlerton *hton, TABLE_SHARE *table_arg) : handler(hton, table_arg)
  {
  }
  ~ha_memem()= default;
  const char *index_type(uint key_number) { return ""; }
  ulonglong table_flags() const { return 0; }
  ulong index_flags(uint inx, uint part, bool all_parts) const { return 0; }
  /* The following defines can be increased if necessary */
#define MEMEM_MAX_KEY MAX_KEY     /* Max allowed keys */
#define MEMEM_MAX_KEY_SEG 16      /* Max segments for key */
#define MEMEM_MAX_KEY_LENGTH 3500 /* Like in InnoDB */
  uint max_supported_keys() const { return MEMEM_MAX_KEY; }
  uint max_supported_key_length() const { return MEMEM_MAX_KEY_LENGTH; }
  uint max_supported_key_part_length() const { return MEMEM_MAX_KEY_LENGTH; }
  int open(const char *name, int mode, uint test_if_locked) { return 0; }
  int close(void) { return 0; }
  int truncate() { return 0; }
  int rnd_init(bool scan);
  int rnd_next(uchar *buf);
  int rnd_pos(uchar *buf, uchar *pos) { return 0; }
  int index_read_map(uchar *buf, const uchar *key, key_part_map keypart_map,
                     enum ha_rkey_function find_flag)
  {
    return HA_ERR_END_OF_FILE;
  }
  int index_read_idx_map(uchar *buf, uint idx, const uchar *key,
                         key_part_map keypart_map,
                         enum ha_rkey_function find_flag)
  {
    return HA_ERR_END_OF_FILE;
  }
  int index_read_last_map(uchar *buf, const uchar *key,
                          key_part_map keypart_map)
  {
    return HA_ERR_END_OF_FILE;
  }
  int index_next(uchar *buf) { return HA_ERR_END_OF_FILE; }
  int index_prev(uchar *buf) { return HA_ERR_END_OF_FILE; }
  int index_first(uchar *buf) { return HA_ERR_END_OF_FILE; }
  int index_last(uchar *buf) { return HA_ERR_END_OF_FILE; }
  void position(const uchar *record) { return; }
  int info(uint flag) { return 0; }
  int external_lock(THD *thd, int lock_type) { return 0; }
  int create(const char *name, TABLE *table_arg, HA_CREATE_INFO *create_info);
  THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                             enum thr_lock_type lock_type)
  {
    return to;
  }
  int delete_table(const char *name) { return 0; }

private:
  void reset_memem_table();
  virtual int write_row(const uchar *buf);
  int update_row(const uchar *old_data, const uchar *new_data)
  {
    return HA_ERR_WRONG_COMMAND;
  };
  int delete_row(const uchar *buf) { return HA_ERR_WRONG_COMMAND; }
};
