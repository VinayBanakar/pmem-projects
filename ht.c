#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <libpmemobj.h>
#include <stdint.h>

#define die(...) do {fprintf(stderr, __VA_ARGS__); exit(1);} while(0)


#define TOMBSTONE_MASK (1ULL << 63)
#define POOL "hashtable"
#define LAYOUT "hashtable"


PMEMobjpool *pool;
char tmp[2];
char tm;

extern __inline__ uint64_t rdtsc(void) {
        uint64_t a, d;
        double cput_clock_ticks_per_ns = 2.6; //2.6 Ghz TSC
        uint64_t c;
        __asm__ volatile ("rdtscp" : "=a" (a), "=c" (c), "=d" (d) : : "memory");
        return ((d<<32) | a)/cput_clock_ticks_per_ns;
}


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

TOID_DECLARE(struct hashtable_s, 0);
TOID_DECLARE(struct entry_s, 1);

typedef struct hashtable_s hashtable_t;


/* Create a new hashtable. */
union hashtable_s_toid ht_create( int size ) {

    TOID(struct hashtable_s) hashtable;

    //size_t pool_size = size * sizeof(entry_t) + sizeof(hashtable_t) + 8092; // Arbitrary size.
    size_t pool_size = 10485760; // just put 10 MiB for now, above is too small

    pool = pmemobj_open(POOL, LAYOUT);
    if(!pool){
	//Pool doesn't exist
	printf("==== Initializing pool %s with pool_size- %lu ====\n", POOL, pool_size);
        pool = pmemobj_create(POOL, LAYOUT, pool_size, 0600);
        if(!pool)
            die("Couldn't open pool: %m\n");
    struct pobj_action actv[2];
    size_t actv_cnt = 0;

    // Now reserve the hash table itself.
    hashtable = POBJ_RESERVE_NEW(pool, struct hashtable_s, &actv[actv_cnt]);
    if (TOID_IS_NULL(hashtable)){
        die("Can't reserve hashtable: %m\n");
    }
    actv_cnt++;

    //Now reserve the pointers to the head nodes.
    //NOTE: You don't bellow it won't create a an array of pointers but actual objects of the given type.
    //TOID(struct entry_s) entries = POBJ_XRESERVE_ALLOC(pool, struct entry_s, (size_t)(sizeof(entry_t *) * size), &actv[actv_cnt], POBJ_XALLOC_ZERO);
    //if (TOID_IS_NULL(entries)){
    //    die("Can't reserve entries: %m\n");
    //}
    //actv_cnt++;
    PMEMoid entries = pmemobj_reserve(pool, &actv[actv_cnt], (size_t)(sizeof(entry_t *) * size), 0);
    struct entry_s **ent_p = pmemobj_direct(entries);
    D_RW(hashtable)->table = ent_p;

    actv_cnt++;

    //Null initialize your hashtable
    for( int i = 0; i < size; i++ ) {
	  ent_p[i] = NULL;
    }
   
    D_RW(hashtable)->size = size;
   
    //Publish the pool
    pmemobj_publish(pool, actv, actv_cnt);
     
    }
    else {
	    //Get hashtable pointer from pool and return.
	 // Get the root of the pool
	PMEMoid root = pmemobj_root(pool, sizeof(struct hashtable_s)+sizeof(struct entry_s)*5);
	//PMEMoid root = pmemobj_root(pool, pool_size);
	//PMEMoid root = pmemobj_root(pool, PMEMOBJ_MIN_POOL);
	if(OID_IS_NULL(root))
	       die("Couldn't get root although pool exists: %m\n");
	union hashtable_s_toid *tmp = pmemobj_direct(root);	
    	hashtable = *tmp;
    }

    return hashtable;
}

//Potential collisions -- no time to check this for now!
int ht_hash( hashtable_t *hashtable, uint64_t key ) {

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

static uint64_t
hash(hashtable_t hashmap, uint64_t key)
{
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccd;
	key ^= key >> 33;
	key *= 0xc4ceb9fe1a85ec53;
	key ^= key >> 33;
	//HM_ASSERT(is_power_of_2(hashmap->capacity));
	//key &= hashmap->capacity - 1;

	/* first, 'tombstone' bit is used to indicate deleted item */
	key &= ~TOMBSTONE_MASK;

	/*
	 * Ensure that we never return 0 as a hash, since we use 0 to
	 * indicate that element has never been used at all.
	 */
	return key == 0 ? 1 : key;
}

// Create a key-value pair.
entry_t *ht_newpair( uint64_t key, char *value ) {
       
	struct pobj_action actv[1];
        size_t actv_cnt = 0;

	TOID(struct entry_s) newpair = POBJ_RESERVE_NEW(pool, struct entry_s, &actv[actv_cnt]);
        if (TOID_IS_NULL(newpair)){
             die("Can't reserve newpair: %m\n");
        }	
	actv_cnt++;
    D_RW(newpair)->key = key;

	if( ( D_RW(newpair)->value = strdup( value ) ) == NULL ) {
		return NULL;
	}
	D_RW(newpair)->next = NULL;

	pmemobj_publish(pool, actv, actv_cnt);
	return D_RW(newpair);
}

void ht_set( hashtable_t *hashtable, uint64_t key, char *value ) {
	int bin = 0;
	entry_t *newpair = NULL;
	entry_t *next = NULL;
	entry_t *last = NULL;

	bin = ht_hash( hashtable, key );

	next = hashtable->table[ bin ];
	
	while( next != NULL && key > next->key) {
		last = next;
		next = next->next;
	}
	

	/* There's already a pair.  Let's replace that string. */
	if( next != NULL && key == next->key ) {

		
		free( next->value );
		next->value = strdup( value );

	/* Nope, could't find it.  Time to grow a pair. */
	} else {
		newpair = ht_newpair( key, value );

		/* We're at the start of the linked list in this bin. */
		if( next == hashtable->table[ bin ] ) {
			newpair->next = next;
			hashtable->table[ bin ] = newpair;
	
		/* We're at the end of the linked list in this bin. */
		} else if ( next == NULL ) {
			last->next = newpair;
	
		/* We're in the middle of the list. */
		} else  {
			newpair->next = next;
			last->next = newpair;
		}
	}
}

char *ht_get( hashtable_t *hashtable, uint64_t key ) {
	int bin = 0;
	entry_t *pair;

	bin = ht_hash( hashtable, key );

	/* Step through the bin, looking for our value. */
	pair = hashtable->table[ bin ];
	while( pair != NULL && pair->key != 0 && key > pair->key) {
		pair = pair->next;
	}

	if( pair == NULL || pair->key == 0 || key != pair->key) {
		tm = 1+'0';
		return &tm;

	} else {
		return pair->value;
	}
	
}

void perf_test(hashtable_t *hashtable)
{
	int test_size = 1000;
	printf("== Test 1: Insert %d keys with variable size values\n", test_size);
	char *cpString;
	uint64_t w_begin_time = rdtsc();
	for(uint64_t i=1; i <=test_size; ++i){
		cpString=malloc(i*sizeof(char));
		memset(cpString,'V',i-1);
		cpString[i]=0;
		ht_set(hashtable, i, cpString);
		free(cpString);	
	}
	uint64_t w_end_time = rdtsc();
	printf("== Test 2: Get %d keys with variable size values\n", test_size);
	uint64_t r_begin_time = rdtsc();
	for(uint64_t i=1; i <=1000; ++i){
		printf("%lu -- %s\n", i, ht_get(hashtable, i));	
	}
	uint64_t r_end_time = rdtsc();
	printf(" ==== Total Put time: %lu ns ====\n", w_end_time - w_begin_time);
	printf(" ==== Total Get time: %lu ns ====\n", r_end_time - r_begin_time);
	printf(" === Average Put time: %lu ns ====\n", (w_end_time - w_begin_time)/test_size);
	printf(" === Average Get time: %lu ns ====\n", (r_end_time - r_begin_time)/test_size);
       	
	printf("== Test 3: If key is not present return 1\n");
	printf("%d -- %s\n", 1001, ht_get(hashtable, 1001));

	printf("== Test 4: update key in place \n");
	ht_set(hashtable, 1000, "Updated");
	printf("%d -- %s\n", 1000, ht_get(hashtable, 1000));

		
}

int main( int argc, char **argv ) {
    int ht_size = 65536;

    	TOID(struct hashtable_s) hashtable = ht_create(ht_size);
	//hashtable_t *hashtable = ht_create( ht_size );
	hashtable_t *hashtable_p = D_RW(hashtable);
	ht_set( hashtable_p, 1, "Alpha" );
	ht_set( hashtable_p, 2, "Beta" );
	ht_set( hashtable_p, 3, "Omega" );
	ht_set( hashtable_p, 4, "Epsilon" );
	ht_set( hashtable_p, 1, "===" );

	printf( "%s\n", ht_get( hashtable_p, 1 ) );
	printf( "%s\n", ht_get( hashtable_p, 2 ) );
	printf( "%s\n", ht_get( hashtable_p, 3 ) );
	printf( "%s\n", ht_get( hashtable_p, 4 ) );
	printf( "%s\n", ht_get( hashtable_p, 1 ) );

    perf_test(hashtable_p);
	return 0;
}
