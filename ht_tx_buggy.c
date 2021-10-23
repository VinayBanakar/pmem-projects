#include <errno.h>
#include <libpmemobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

POBJ_LAYOUT_BEGIN(httx);
POBJ_LAYOUT_ROOT(httx, struct root);
// POBJ_LAYOUT_ROOT(httx, uint64_t); // To indicate incomplete migration caused
// by crash.
POBJ_LAYOUT_END(httx)

#define HASHTABLE_TX_TYPE_OFFSET 1004
#define HASH_FUNC_COEFF_P                                                      \
  32212254719ULL // large prime number for hash coefficient
#define POOL_SIZE                                                              \
  (1024 * 1024 *                                                               \
   1024) // Allocate one big pool; not dynamicaly resizing pool yet.
//#define POOL_SIZE PMEMOBJ_MIN_POOL

#define die(...)                                                               \
  do {                                                                         \
    fprintf(stderr, __VA_ARGS__);                                              \
    exit(1);                                                                   \
  } while (0)

extern __inline__ uint64_t rdtsc(void) {
  uint64_t a, d;
  double cput_clock_ticks_per_ns = 2.6; // 2.6 Ghz TSC
  uint64_t c;
  __asm__ volatile("rdtscp" : "=a"(a), "=c"(c), "=d"(d) : : "memory");
  return ((d << 32) | a) / cput_clock_ticks_per_ns;
}

struct hashtable_s;
TOID_DECLARE(struct hashtable_s, HASHTABLE_TX_TYPE_OFFSET + 0);
struct entry;
struct buckets;
TOID_DECLARE(struct buckets, HASHTABLE_TX_TYPE_OFFSET + 1);
TOID_DECLARE(struct entry, HASHTABLE_TX_TYPE_OFFSET + 2);

// prototypes
void ht_alloc(PMEMobjpool *, TOID(struct hashtable_s) *, uint32_t, size_t,
              uint64_t);
void ht_expand(PMEMobjpool *, TOID(struct hashtable_s), size_t);
void perf_test(char *);

struct entry {
  uint64_t key;
  PMEMoid value;
  TOID(struct entry) next;
};

struct buckets {
  size_t nbuckets;             // number of buckets
  TOID(struct entry) bucket[]; // array of lists
};

struct hashtable_s {
  uint32_t seed; // Random number generator

  // hash function coefficients
  uint32_t hash_fun_a;
  uint32_t hash_fun_b;
  uint64_t hash_fun_p;

  uint64_t size;
  uint64_t uuid; // A unique id to identify this HT.
  TOID(struct buckets) buckets;
};

struct root {
  // int not_empty; // 0 if empty 1 if not.
  TOID(struct hashtable_s)
  ht_list[6]; // 6 HTs per pool for now, this can any number.
};

PMEMobjpool *pop;

// Initialize the pool and hashtable
// If the bucket size passed for a ht is more than previous then it'll auto
// expand the table
TOID(struct hashtable_s) *
    init_pool_ht(const char *path, uint64_t ht_id, size_t buck_sz) {

  // TOID(struct hashtable_s)* hashtable;

  if (access(path, F_OK) != 0) {
    pop = pmemobj_create(path, POBJ_LAYOUT_NAME(httx), POOL_SIZE, 0666);
    if (pop == NULL) {
      fprintf(stderr, "failed to create pool: %s\n", pmemobj_errormsg());
      // return 1;
      die("Exit");
    }
  } else {
    pop = pmemobj_open(path, POBJ_LAYOUT_NAME(httx));
    if (pop == NULL) {
      fprintf(stderr, "failed to open pool: %s\n", pmemobj_errormsg());
      // return 1;
      die("Exit");
    }
  }

  TOID(struct root) root = POBJ_ROOT(pop, struct root);

  /*
  int htl_sz = 6;
  //First create a list of tables if the list is NULL
  // For now let's test with 6 tables in the pool.
  if(!D_RO(root)->not_empty){
      TX_BEGIN(pop) {
          TX_ADD(root);
          //TX_ADD_DIRECT(&D_RW(root)->ht_list);
          TX_NEW(struct ht_list);
          D_RW(root)->ht_list = TX_ZALLOC(struct hashtable_s, htl_sz);
      } TX_ONABORT {
                  fprintf(stderr, "%s: transaction aborted: %s\n", __func__,
                      pmemobj_errormsg());
                  abort();
          } TX_ONCOMMIT {
          D_RW(root)->not_empty = 1;
      } TX_END
  }
  //REVIEW: Nested flexible arrays is causing TX_ALLOC to fail -- alas, no time
  to debug
  // so let's statically allocate this for now.
  */

  if (TOID_IS_NULL(D_RO(root)->ht_list[ht_id])) {
    // create new it table doesn't exist.
    ht_alloc(pop, &D_RW(root)->ht_list[ht_id], 0, buck_sz, ht_id);
  }

  // Expand the table
  if (D_RW(D_RW(D_RW(root)->ht_list[ht_id])->buckets)->nbuckets < buck_sz)
    ht_expand(pop, D_RW(root)->ht_list[ht_id], buck_sz);
  return &D_RW(root)->ht_list[ht_id];
}

void ht_alloc(PMEMobjpool *pop, TOID(struct hashtable_s) * hashtable,
              uint32_t seed, size_t bucket_sz, uint64_t ht_id) {
  size_t len = bucket_sz;
  size_t sz = sizeof(struct buckets) + len * sizeof(TOID(struct entry));

  TX_BEGIN(pop) {
    *hashtable = TX_NEW(struct hashtable_s);
    TX_ADD(*hashtable);
    D_RW(*hashtable)->uuid = ht_id;
    D_RW(*hashtable)->seed = seed;
    do {
      D_RW(*hashtable)->hash_fun_a = (uint32_t)rand();
    } while (D_RW(*hashtable)->hash_fun_a == 0);
    D_RW(*hashtable)->hash_fun_b = (uint32_t)rand();
    D_RW(*hashtable)->hash_fun_p = HASH_FUNC_COEFF_P;

    D_RW(*hashtable)->buckets = TX_ZALLOC(struct buckets, sz);
    D_RW(D_RW(*hashtable)->buckets)->nbuckets = len;
  }
  TX_ONABORT {
    fprintf(stderr, "%s: transaction aborted: %s\n", __func__,
            pmemobj_errormsg());
    abort();
  }
  TX_END
}

/**
 * hash -- A simple hashing function,
 * see https://en.wikipedia.org/wiki/Universal_hashing#Hashing_integers
 */
uint64_t hash(const TOID(struct hashtable_s) * hashtable,
              TOID(struct buckets) * buckets, uint64_t value) {
  uint32_t a = D_RO(*hashtable)->hash_fun_a;
  uint32_t b = D_RO(*hashtable)->hash_fun_b;
  uint64_t p = D_RO(*hashtable)->hash_fun_p;
  size_t len = D_RO(*buckets)->nbuckets;

  return ((a * value + b) % p) % len;
}

/**
 * Returns 0/1 if set, -1 if something failed. Updated value in place.
 */
int ht_set(PMEMobjpool *pop, TOID(struct hashtable_s) hashtable, uint64_t key,
           PMEMoid value) {
  TOID(struct buckets) buckets = D_RO(hashtable)->buckets;
  TOID(struct entry) buck;
  // TOID(char) str;

  uint64_t h = hash(&hashtable, &buckets, key);
  int num = 0;
  int ret = 0;

  for (buck = D_RO(buckets)->bucket[h]; !TOID_IS_NULL(buck);
       buck = D_RO(buck)->next) {
    if (D_RO(buck)->key == key) {
      // Update the value.
      TX_BEGIN(pop) {
        TX_ADD_DIRECT(&D_RW(buck)->value);
        // TOID_ASSIGN(str, D_RW(buck)->value);
        // TX_FREE(str);
        // REVIEW: Super weird that it can't be freed from persistent pointer!
        D_RW(buck)->value = value;
        ret = 1;
      }
      TX_ONABORT {
        fprintf(stderr, "transaction aborted: %s\n", pmemobj_errormsg());
        ret = -1;
      }
      TX_END
    }
    num++;
  }

  if (ret)
    return ret;

  TX_BEGIN(pop) {
    TX_ADD_FIELD(D_RO(hashtable)->buckets, bucket[h]);
    TX_ADD_FIELD(hashtable, size);

    TOID(struct entry) e = TX_NEW(struct entry);
    D_RW(e)->key = key;
    D_RW(e)->value = value;
    D_RW(e)->next = D_RO(buckets)->bucket[h];
    D_RW(buckets)->bucket[h] = e;

    D_RW(hashtable)->size++;
    num++;
    ret = 0;
  }
  TX_ONABORT {
    fprintf(stderr, "transaction aborted: %s\n", pmemobj_errormsg());
    ret = -1;
  }
  TX_END

  if (ret)
    return ret;

  // if (D_RO(hashtable)->size > 2 * D_RO(buckets)->nbuckets))
  // 	ht_expand(pop, hashtable, D_RO(buckets)->nbuckets * 2);

  return 0;
}

void ht_expand(PMEMobjpool *pop, TOID(struct hashtable_s) hashtable,
               size_t new_len) {
  TOID(struct buckets) buckets_old = D_RO(hashtable)->buckets;

  if (new_len == 0)
    new_len = D_RO(buckets_old)->nbuckets;

  size_t sz_old = sizeof(struct buckets) +
                  D_RO(buckets_old)->nbuckets * sizeof(TOID(struct entry));
  size_t sz_new = sizeof(struct buckets) + new_len * sizeof(TOID(struct entry));

  TX_BEGIN(pop) {
    TX_ADD_FIELD(hashtable, buckets);
    TOID(struct buckets) buckets_new = TX_ZALLOC(struct buckets, sz_new);
    D_RW(buckets_new)->nbuckets = new_len;
    // log the entire bucket range as on ABORT any changes to it will be
    // reverted.
    pmemobj_tx_add_range(buckets_old.oid, 0, sz_old);

    // Move all the old bucket entries to new bucket
    for (size_t i = 0; i < D_RO(buckets_old)->nbuckets; ++i) {
      while (!TOID_IS_NULL(D_RO(buckets_old)->bucket[i])) {
        TOID(struct entry) en = D_RO(buckets_old)->bucket[i];
        uint64_t h = hash(&hashtable, &buckets_new, D_RO(en)->key);
        D_RW(buckets_old)->bucket[i] = D_RO(en)->next;
        TX_ADD_FIELD(en, next);
        D_RW(en)->next = D_RO(buckets_new)->bucket[h];
        D_RW(buckets_new)->bucket[h] = en;
      }
    }

    D_RW(hashtable)->buckets = buckets_new;
    // Safe to free the bucket now.
    TX_FREE(buckets_old);
  }
  TX_ONABORT {
    fprintf(stderr, "%s: transaction aborted: %s\n", __func__,
            pmemobj_errormsg());
    // If transaction fails nothing to do as old buckets remain intact.
  }
  TX_END
}

PMEMoid ht_get(PMEMobjpool *pop, TOID(struct hashtable_s) hashtable_s,
               uint64_t key) {
  TOID(struct buckets) buckets = D_RO(hashtable_s)->buckets;
  TOID(struct entry) buck;

  uint64_t h = hash(&hashtable_s, &buckets, key);

  for (buck = D_RO(buckets)->bucket[h]; !TOID_IS_NULL(buck);
       buck = D_RO(buck)->next)
    if (D_RO(buck)->key == key)
      return D_RO(buck)->value;
  return OID_NULL;
}

// Migrating data atomically from ht1 to ht2 and all ht2 will be erased.
// During migration if system crashes the new changes will not be commited.
int ht_migrate(TOID(struct hashtable_s) ht1, TOID(struct hashtable_s) ht2) {

  int finished = 0;
  TOID(struct buckets) buckets_ht1 = D_RO(ht1)->buckets;
  size_t sz = sizeof(struct buckets) +
              D_RO(buckets_ht1)->nbuckets * sizeof(TOID(struct entry));
  TX_BEGIN(pop) {
    TX_ADD_FIELD(ht1, buckets);
    TOID(struct buckets) buckets_ht2 = TX_ZALLOC(struct buckets, sz);
    D_RW(buckets_ht2)->nbuckets = D_RO(buckets_ht1)->nbuckets;
    pmemobj_tx_add_range(buckets_ht1.oid, 0,
                         sz); // ht1 undo logged for contingency.

    for (size_t i = 0; i < D_RO(buckets_ht1)->nbuckets; ++i) {
      while (!TOID_IS_NULL(D_RO(buckets_ht1)->bucket[i])) {
        TOID(struct entry) en = D_RO(buckets_ht1)->bucket[i];
        uint64_t h = hash(&ht2, &buckets_ht2, D_RO(en)->key);
        D_RW(buckets_ht1)->bucket[i] = D_RO(en)->next;
        TX_ADD_FIELD(en, next);
        D_RW(en)->next = D_RO(buckets_ht2)->bucket[h];
        D_RW(buckets_ht2)->bucket[h] = en;
      }
    }
    TX_ADD(ht2);
    D_RW(ht2)->buckets = buckets_ht2;

    // Erase ht1
    TX_FREE(buckets_ht1);
    TX_FREE(ht1);
  }
  TX_ONCOMMIT { finished = 1; }
  TX_ONABORT {
    fprintf(stderr, "%s: transaction aborted: %s\n", __func__,
            pmemobj_errormsg());
  }
  TX_END

  return finished;
}

TOID_DECLARE(char, 0);
int main(int argc, char *argv[]) {

  const char *path = argv[1];

  // Simple test
  TOID(struct hashtable_s) *ht = init_pool_ht(path, 0, 10);
  char *test =
      "What is the ultimate answer for life, the universe, and everything?\0";
  PMEMoid TESToid;

  TX_BEGIN(pop) { TESToid = TX_STRDUP(test, 0); }
  TX_ONABORT {
    fprintf(stderr, "transaction aborted: %s\n", pmemobj_errormsg());
  }
  TX_END

  if (!ht_set(pop, *ht, 42, TESToid)) {
    char *val = pmemobj_direct(ht_get(pop, *ht, 42));
    printf("%s\n", val);
  }

  pmemobj_close(pop);
  // FIXEME: Have to close the pool to open new HT,
  // simply rearanging the code should fix this dependency.
  //
  perf_test(path);
}

void perf_test(char *path) {

  TOID(struct hashtable_s) *ht1 = init_pool_ht(path, 1, 10);

  int test_size = 1;
  printf("==== Test 1: Insert %d keys with variable size values ====\n",
         test_size);
  char *test;
  char *val;
  PMEMoid TESToid;
  uint64_t w_begin_time = rdtsc();
  for (int i = 1; i < 1000; i++) {
    test = calloc(i, sizeof(char));
    memset(test, 'V', i - 1);
    TX_BEGIN(pop) { TESToid = TX_STRDUP(test, 0); }
    TX_ONABORT {
      fprintf(stderr, "transaction aborted: %s\n", pmemobj_errormsg());
    }
    TX_END
    if (ht_set(pop, *ht1, i, TESToid) == -1) {
      die("Failed!");
      break;
    }
  }
  uint64_t w_end_time = rdtsc();

  printf("== Test 2: Get %d keys with variable size values\n", test_size);
  uint64_t r_begin_time = rdtsc();
  for (int i = 1; i < test_size; i++) {
    PMEMoid valp = ht_get(pop, *ht1, i);
    if (!OID_IS_NULL(valp)) {
      val = pmemobj_direct(valp);
      // printf("%s\n", val);
    } else {
      printf("== Key %d not found in hash table %d ==\n", i, 1);
    }
  }
  uint64_t r_end_time = rdtsc();
  printf(" ==== Total Put time: %lu ns ====\n", w_end_time - w_begin_time);
  printf(" ==== Total Get time: %lu ns ====\n", r_end_time - r_begin_time);
  printf(" === Average Put time: %lu ns ====\n",
         (w_end_time - w_begin_time) / test_size);
  printf(" === Average Get time: %lu ns ====\n",
         (r_end_time - r_begin_time) / test_size);

  printf("==== Test 3: update key in place ====\n");
  test = " Don’t Panic.\0";
  TX_BEGIN(pop) { TESToid = TX_STRDUP(test, 0); }
  TX_ONABORT {
    fprintf(stderr, "transaction aborted: %s\n", pmemobj_errormsg());
  }
  TX_END
  if (ht_set(pop, *ht1, 42, TESToid)) {
    val = pmemobj_direct(ht_get(pop, *ht1, 42));
    printf("Updated value of key %d is %s\n ", 42, val);
  }
  pmemobj_close(pop);

  printf("==== Test 4: Create multiple variable size hash tables in single "
         "pool ====\n");
  printf("== Hash table with the following UUID and size are created:\n");
  TOID(struct hashtable_s) *ht2 = init_pool_ht(path, 2, 10);
  printf("\t ht_%lu -- %lu \n", D_RO(*ht2)->uuid,
         D_RO(D_RO(*ht2)->buckets)->nbuckets); // ht2->buckets->nbuckets?
  pmemobj_close(pop);
  TOID(struct hashtable_s) *ht3 = init_pool_ht(path, 3, 5);
  printf("\t ht_%lu -- %lu \n", D_RO(*ht3)->uuid,
         D_RO(D_RO(*ht3)->buckets)->nbuckets); // ht2->buckets->nbuckets?
  pmemobj_close(pop);
  TOID(struct hashtable_s) *ht4 = init_pool_ht(path, 4, 20);
  printf("\t ht_%lu -- %lu \n", D_RO(*ht4)->uuid,
         D_RO(D_RO(*ht4)->buckets)->nbuckets); // ht2->buckets->nbuckets?
  pmemobj_close(pop);

  printf("==== Test 5: Expanding the hashtable ====\n");
  ht1 = init_pool_ht(path, 1, 20); // Notice this was 10 before.
  printf("*** Now the the hashtable size of %lu is %lu ***\n", D_RO(*ht1)->uuid,
         D_RO(D_RO(*ht1)->buckets)->nbuckets);

  for (int i = 1; i < test_size; i++) {
    PMEMoid valp = ht_get(pop, *ht1, i);
    if (!OID_IS_NULL(valp)) {
      val = pmemobj_direct(valp);
      // printf("%s\n", val);
    } else {
      printf("== Key %d not found in hash table %d ==\n", i, 1);
    }
  }

  printf("==== Test 6: Migrating the hashtable ====\n");
  for (int i = 1; i < 10000; i++) {
    PMEMoid valp = ht_get(pop, *ht4, i);
    if (!OID_IS_NULL(valp)) {
      val = pmemobj_direct(valp);
      printf("%s\n", val);
    } else {
      // printf("== Key %d not found in hash table %d ==\n", i, 1);
    }
  }
  printf("\t*** No GETs were successful on ht4\n");
  if (ht_migrate(*ht1, *ht4)) {
    printf("\t*** Migration from ht1 and ht4 is complete\n");
  } else {
    die("\t*** Migration from ht1 and ht4 failed \n");
  }

  printf("\t***Now reading data from ht4:***\n");
  for (int i = 1; i < 1000; i++) {
    PMEMoid valp = ht_get(pop, *ht4, i);
    if (!OID_IS_NULL(valp)) {
      val = pmemobj_direct(valp);
      // printf("%s\n", val);
    } else {
      printf("== Key %d not found in hash table %d ==\n", i, 4);
    }
  }
  printf("\t***All reads completed successfully***\n");

  // close the pool before next call.
  pmemobj_close(pop);
}
