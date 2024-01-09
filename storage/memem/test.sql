drop table if exists y;
drop table if exists z;

create table y(i int, j int) engine = MEMEM;
insert into y values (2, 1029);
insert into y values (92, 8);
select * from y where i + 8 = 10;

create table z(a int) engine = MEMEM;
insert into z values (322);
insert into z values (8);
select * from z where a > 20;
