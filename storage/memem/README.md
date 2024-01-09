# memem: in-memory storage engine for MySQL/MariaDB

This is a minimal storage engine for my own education. It stores data
in memory. It is completely not thread-safe, since I didn't have time
to figure out MySQL/MariaDB's lock primitives during the [hack
week](https://eatonphil.com/2024-01-wehack-mysql.html) when I built
this.

It's pretty succinct! 235LoC. Check out
[storage/memem/ha_memem.cc](storage/memem/ha_memem.cc) and
[storage/memem/ha_memem.h](storage/memem/ha_memem.h).

## Setup

Download and build. (MySQL/MariaDB initial builds take forever, be patient.)

```console
$ git clone https://github.com/eatonphil/mariadb
$ cd mariadb
$ mkdir build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
$ make -j8
```

Create a database.

```console
$ ./build/scripts/mariadb-install-db --srcdir=$(pwd) --datadir=$(pwd)/db
```

Create my.cnf, the MySQL configuration file.

```console
$ echo "[client]
socket=$(pwd)/mariadb.sock

[mariadb]
socket=$(pwd)/mariadb.sock

basedir=$(pwd)
datadir=$(pwd)/db
pid-file=$(pwd)/db.pid" > my.cnf
```

## Run

In one terminal, start the server.

```console
$ ./build/sql/mariadbd --defaults-extra-file=$(pwd)/my.cnf --debug:d:o,$(pwd)/db.debug
./build/sql/mariadbd: Can't create file '/var/log/mariadb/mariadb.log' (errno: 13 "Permission denied")
2024-01-03 17:10:15 0 [Note] Starting MariaDB 11.4.0-MariaDB-debug source revision 3fad2b115569864d8c1b7ea90ce92aa895cfef08 as process 185550
2024-01-03 17:10:15 0 [Note] InnoDB: !!!!!!!! UNIV_DEBUG switched on !!!!!!!!!
2024-01-03 17:10:15 0 [Note] InnoDB: Compressed tables use zlib 1.2.13
2024-01-03 17:10:15 0 [Note] InnoDB: Number of transaction pools: 1
2024-01-03 17:10:15 0 [Note] InnoDB: Using crc32 + pclmulqdq instructions
2024-01-03 17:10:15 0 [Note] InnoDB: Initializing buffer pool, total size = 128.000MiB, chunk size = 2.000MiB
2024-01-03 17:10:15 0 [Note] InnoDB: Completed initialization of buffer pool
2024-01-03 17:10:15 0 [Note] InnoDB: Buffered log writes (block size=512 bytes)
2024-01-03 17:10:15 0 [Note] InnoDB: End of log at LSN=57155
2024-01-03 17:10:15 0 [Note] InnoDB: Opened 3 undo tablespaces
2024-01-03 17:10:15 0 [Note] InnoDB: 128 rollback segments in 3 undo tablespaces are active.
2024-01-03 17:10:15 0 [Note] InnoDB: Setting file './ibtmp1' size to 12.000MiB. Physically writing the file full; Please wait ...
2024-01-03 17:10:15 0 [Note] InnoDB: File './ibtmp1' size is now 12.000MiB.
2024-01-03 17:10:15 0 [Note] InnoDB: log sequence number 57155; transaction id 16
2024-01-03 17:10:15 0 [Note] InnoDB: Loading buffer pool(s) from ./db/ib_buffer_pool
2024-01-03 17:10:15 0 [Note] Plugin 'FEEDBACK' is disabled.
2024-01-03 17:10:15 0 [Note] Plugin 'wsrep-provider' is disabled.
2024-01-03 17:10:15 0 [Note] InnoDB: Buffer pool(s) load completed at 240103 17:10:15
2024-01-03 17:10:15 0 [Note] Server socket created on IP: '0.0.0.0'.
2024-01-03 17:10:15 0 [Note] Server socket created on IP: '::'.
2024-01-03 17:10:15 0 [Note] mariadbd: Event Scheduler: Loaded 0 events
2024-01-03 17:10:15 0 [Note] ./build/sql/mariadbd: ready for connections.
Version: '11.4.0-MariaDB-debug'  socket: './mariadb.sock'  port: 3306  Source distribution
```

(Note: `tail -f db.debug` to see more detailed debug logs!)

In another terminal, run the provided test script
(`storage/memem/test.sql`):

```console
$ ./build/client/mariadb --defaults-extra-file=$(pwd)/my.cnf --database=test --table --verbose < storage/memem/test.sql
--------------
drop table if exists y
--------------

--------------
drop table if exists z
--------------

--------------
create table y(i int, j int) engine = MEMEM
--------------

--------------
insert into y values (2, 1029)
--------------

--------------
insert into y values (92, 8)
--------------

--------------
select * from y where i + 8 = 10
--------------

+------+------+
| i    | j    |
+------+------+
|    2 | 1029 |
+------+------+
--------------
create table z(a int) engine = MEMEM
--------------

--------------
insert into z values (322)
--------------

--------------
insert into z values (8)
--------------

--------------
select * from z where a > 20
--------------

+------+
| a    |
+------+
|  322 |
+------+
```
