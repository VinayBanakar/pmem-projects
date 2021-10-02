Name: Vinay Banakar \
email: vin@cs.wisc.edu 


I liked using TX macros more than RP, although this was challenging. The attached ht_tx.c supports \
multiple hashtables in a single pool, they are tracked by a list called ht_list. Each table can be \
expanded with any size the user provides. Any table can be migrated with any other table as long \
the new table is equal to or greater in size. The program is crash tolerant during both expansion \
and migration tasks.

```bash
$ #Run the following to make all three ht versions
$ ./make
$ ./ht_tx hash # Takes the pool as param.
````

```bash
$ # To run other variants.
$ ./ht_rp
$ ./ht_vanilla
```
  

# Peformance numbers for 1000 operations:
Keys are integers and values are of variable size, incrementing in size with key.
## hashtable with transaction APIs
```
==== Total Put time: 2769105579 ns  ====
==== Total Get time: 1074856 ns     ====
=== Average Put time: 2769105 ns    ====
=== Average Get time: 1074 ns       ====
```
## hashtable with reserve and publish APIs
```
==== Total Put time: 378730309 ns   ====
==== Total Get time: 32679397 ns    ====
==== Average Put time: 378730 ns    ====
==== Average Get time: 32679 ns     ====
```
## hashtable with malloc - volatile
```
==== Total Put time: 893398 ns      ====
==== Total Get time: 43060192 ns    ====
=== Average Put time: 893 ns        ====
=== Average Get time: 43060 ns      ====
```
Frankly, I have not done extensive performance analysis to make strong \
conclusions about efficiency of one API over the other. Will look at this \
again soon.