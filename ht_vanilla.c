#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOMBSTONE_MASK (1ULL << 63)

extern __inline__ uint64_t rdtsc(void) {
  uint64_t a, d;
  double cput_clock_ticks_per_ns = 2.6; // 2.6 Ghz TSC
  uint64_t c;
  __asm__ volatile("rdtscp" : "=a"(a), "=c"(c), "=d"(d) : : "memory");
  return ((d << 32) | a) / cput_clock_ticks_per_ns;
}

char tmp[2];
struct entry_s {
  uint64_t key;
  char *value;
  struct entry_s *next;
};

typedef struct entry_s entry_t;

struct hashtable_s {
  int size;
  struct entry_s **table;
};

typedef struct hashtable_s hashtable_t;

/* Create a new hashtable. */
hashtable_t *ht_create(int size) {

  hashtable_t *hashtable = NULL;
  int i;

  if (size < 1)
    return NULL;

  /* Allocate the table itself. */
  if ((hashtable = malloc(sizeof(hashtable_t))) == NULL) {
    return NULL;
  }

  /* Allocate pointers to the head nodes. */
  if ((hashtable->table = malloc(sizeof(entry_t *) * size)) == NULL) {
    return NULL;
  }
  for (i = 0; i < size; i++) {
    hashtable->table[i] = NULL;
  }

  hashtable->size = size;

  return hashtable;
}

// Potential collisions -- no time to check this for now!
/* Hash a string for a particular hash table. */
int ht_hash(hashtable_t *hashtable, uint64_t key) {

  unsigned long int hashval;
  int i = 0;

  /* Convert our string to an integer */
  // while( hashval < ULONG_MAX && i < strlen( key ) ) {
  // 	hashval = hashval << 8;
  // 	hashval += key[ i ];
  // 	i++;
  // }

  return key % hashtable->size;
}

static uint64_t hash(hashtable_t hashmap, uint64_t key) {
  key ^= key >> 33;
  key *= 0xff51afd7ed558ccd;
  key ^= key >> 33;
  key *= 0xc4ceb9fe1a85ec53;
  key ^= key >> 33;
  // HM_ASSERT(is_power_of_2(hashmap->capacity));
  // key &= hashmap->capacity - 1;

  /* first, 'tombstone' bit is used to indicate deleted item */
  key &= ~TOMBSTONE_MASK;

  /*
   * Ensure that we never return 0 as a hash, since we use 0 to
   * indicate that element has never been used at all.
   */
  return key == 0 ? 1 : key;
}

/* Create a key-value pair. */
entry_t *ht_newpair(uint64_t key, char *value) {
  entry_t *newpair;

  if ((newpair = malloc(sizeof(entry_t))) == NULL) {
    return NULL;
  }
  newpair->key = key;

  if ((newpair->value = strdup(value)) == NULL) {
    return NULL;
  }
  newpair->next = NULL;

  return newpair;
}

/* Insert a key-value pair into a hash table. */
void ht_set(hashtable_t *hashtable, uint64_t key, char *value) {
  int bin = 0;
  entry_t *newpair = NULL;
  entry_t *next = NULL;
  entry_t *last = NULL;

  // printf("Inserting %llu and %s\n", key, value);

  bin = ht_hash(hashtable, key);

  next = hashtable->table[bin];

  while (next != NULL && next->key != 0 && key > next->key) {
    last = next;
    next = next->next;
  }

  /* There's already a pair.  Let's replace that string. */
  if (next != NULL && next->key != 0 && key == next->key) {

    free(next->value);
    next->value = strdup(value);

    /* Nope, could't find it.  Time to grow a pair. */
  } else {
    newpair = ht_newpair(key, value);

    /* We're at the start of the linked list in this bin. */
    if (next == hashtable->table[bin]) {
      newpair->next = next;
      hashtable->table[bin] = newpair;

      /* We're at the end of the linked list in this bin. */
    } else if (next == NULL) {
      last->next = newpair;

      /* We're in the middle of the list. */
    } else {
      newpair->next = next;
      last->next = newpair;
    }
  }
}

/* Retrieve a key-value pair from a hash table. */
char *ht_get(hashtable_t *hashtable, uint64_t key) {
  int bin = 0;
  entry_t *pair;

  bin = ht_hash(hashtable, key);

  /* Step through the bin, looking for our value. */
  pair = hashtable->table[bin];
  while (pair != NULL && pair->key != 0 && key > pair->key) {
    pair = pair->next;
  }

  /* Did we actually find anything? */
  if (pair == NULL || pair->key == 0 || key != pair->key) {
    // return 1
    sprintf(tmp, "%c", 1);
    return tmp;

  } else {
    return pair->value;
  }
}

hashtable_t *ht_expand(hashtable_t *hashtable, int new_size) {

  if (new_size < 1)
    return NULL;
  if (new_size == hashtable->size)
    return NULL;
  if (hashtable->size > new_size)
    return NULL; // preserving existing data.

  hashtable_t *new_hashtable = NULL;

  /* Allocate the table itself. */
  if ((new_hashtable = malloc(sizeof(hashtable_t))) == NULL) {
    return NULL;
  }
  if ((new_hashtable->table = malloc(sizeof(entry_t *) * new_size)) == NULL) {
    return NULL;
  }

  int i;
  // Null initialize the new table.
  for (i = 0; i < new_size; i++) {
    new_hashtable->table[i] = NULL;
  }
  new_hashtable->size = new_size;

  entry_t *pair = NULL;
  // Now move old keys to new bins and free old entries
  for (i = 0; i < hashtable->size; i++) {
    pair = hashtable->table[i];
    if (pair != NULL && pair->key != 0) {
      ht_set(new_hashtable, pair->key, pair->value);
      free(pair);
    }
  }
  // free old hash table
  free(hashtable);
  return new_hashtable;
}

// Implement the ability to have more than one hash table,
// and have a method for moving data between hash tables by
// atomically removing the entry from one table and adding it to the second.
// Move data from ht1 to ht2 which are of same size.
bool ht_move(hashtable_t *ht1, hashtable_t *ht2) {
  // atomically move data
  if (ht1->size != ht2->size)
    return false;

  entry_t *pair = NULL;
  for (int i = 0; i < ht1->size; ++i) {
    pair = ht1->table[i];
    if (pair != NULL && pair->key != 0) {
      ht_set(ht2, pair->key, pair->value);
      free(pair);
    }
  }
  free(ht1);
  return true;
}

void perf_test(hashtable_t *hashtable) {
  int test_size = 1000;
  printf("== Test 1: Insert %d keys with variable size values\n", test_size);
  char *cpString;
  uint64_t w_begin_time = rdtsc();
  for (uint64_t i = 1; i <= test_size; ++i) {
    cpString = calloc(i, sizeof(char));
    memset(cpString, 'V', i - 1);
    cpString[i] = 0;
    ht_set(hashtable, i, cpString);
    free(cpString);
  }
  uint64_t w_end_time = rdtsc();
  printf("== Test 2: Get %d keys with variable size values\n", test_size);
  uint64_t r_begin_time = rdtsc();
  for (uint64_t i = 1; i <= 1000; ++i) {
    printf("%lu -- %s\n", i, ht_get(hashtable, i));
  }
  uint64_t r_end_time = rdtsc();
  printf(" ==== Total Put time: %llu ns ====\n", w_end_time - w_begin_time);
  printf(" ==== Total Get time: %llu ns ====\n", r_end_time - r_begin_time);
  printf(" === Average Put time: %llu ns ====\n",
         (w_end_time - w_begin_time) / test_size);
  printf(" === Average Get time: %llu ns ====\n",
         (r_end_time - r_begin_time) / test_size);
}

int main(int argc, char **argv) {

  // PMEMobjpool *pool = pmemobj_open(POOL, LAYOUT);
  hashtable_t *hashtable = ht_create(6);

  ht_set(hashtable, 1, "Alpha");
  ht_set(hashtable, 2, "Beta");
  ht_set(hashtable, 3, "Omega");
  ht_set(hashtable, 4, "Epsilon");
  ht_set(hashtable, 1, "kapa");

  // Resizing hash table
  hashtable_t *ht2 = ht_expand(hashtable, 8);
  ht_set(ht2, 10, "test1");
  ht_set(ht2, 11, "test2");
  ht_set(ht2, 2, "test3");

  printf("%s\n", ht_get(ht2, 10));
  printf("%s\n", ht_get(ht2, 1));
  printf("%s\n", ht_get(ht2, 2));
  printf("%s\n", ht_get(ht2, 3));
  printf("%s\n", ht_get(ht2, 4));

  // Moving data to another hash table
  hashtable_t *ht3 = ht_create(8);
  if (!ht_move(ht2, ht3))
    printf("Moving data between ht failed!!");

  printf("%s\n", ht_get(ht3, 3));

  perf_test(ht3);
  return 0;
}