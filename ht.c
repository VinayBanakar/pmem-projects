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

    //size_t pool_size = size * sizeof(entry_t) + sizeof(hashtable_t) + 8092; // Arbitrary size.
    size_t pool_size = 10485760; // just put 10 MiB for now, above is too small

    pool = pmemobj_open(POOL, LAYOUT);
    if(!pool){
	printf("pool_size: %lu\n", pool_size);
        pool = pmemobj_create(POOL, LAYOUT, pool_size, 0600);
        if(!pool)
            die("Couldn't open pool: %m\n");
    }
    else {
	    //TODO: Get hashtable pointer from pool and return.


    }

    struct pobj_action actv[2];
    size_t actv_cnt = 0;

    // Now reserve the hash table itself.
    TOID(struct hashtable_s) hashtable = POBJ_RESERVE_NEW(pool, struct hashtable_s, &actv[actv_cnt]);
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

    //Null initialize your hashtable
    for( int i = 0; i < size; i++ ) {
	  ent_p[i] = NULL;
    }
   
    D_RW(hashtable)->size = size;
   
   printf("before publish\n"); 
    //Publish the pool
    pmemobj_publish(pool, actv, actv_cnt);
  
   printf("after publish\n"); 
    return hashtable;
}

//Potential collisions -- no time to check this for now!
/* Hash a string for a particular hash table. */
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

/* Create a key-value pair. */
entry_t *ht_newpair( uint64_t key, char *value ) {
	entry_t *newpair;



	if( ( newpair = malloc( sizeof( entry_t ) ) ) == NULL ) {
		return NULL;
	}
    newpair->key = key;

	if( ( newpair->value = strdup( value ) ) == NULL ) {
		return NULL;
	}
	newpair->next = NULL;

	return newpair;
}

/* Insert a key-value pair into a hash table. */
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

/* Retrieve a key-value pair from a hash table. */
char *ht_get( hashtable_t *hashtable, uint64_t key ) {
	int bin = 0;
	entry_t *pair;

	bin = ht_hash( hashtable, key );

	/* Step through the bin, looking for our value. */
	pair = hashtable->table[ bin ];
	while( pair != NULL && pair->key != 0 && key > pair->key) {
		pair = pair->next;
	}

	/* Did we actually find anything? */
	if( pair == NULL || pair->key == 0 || key != pair->key) {
		//return 1
	       	sprintf(tmp, "%c", 1);
        	return tmp;

	} else {
		return pair->value;
	}
	
}

void perf_test(union hashtable_s_toid hashtable){

}

int main( int argc, char **argv ) {
    int ht_size = 65536;

    TOID(struct hashtable_s) hashtable = ht_create(ht_size);
    //hashtable_t *hashtable = ht_create( ht_size );
	hashtable_t *hashtable_p = D_RW(hashtable);
	ht_set( hashtable_p, 1, "inky" );
	ht_set( hashtable_p, 2, "pinky" );
	ht_set( hashtable_p, 3, "blinky" );
	ht_set( hashtable_p, 4, "floyd" );
	ht_set( hashtable_p, 1, "loyd" );

	printf( "%s\n", ht_get( hashtable_p, 1 ) );
	printf( "%s\n", ht_get( hashtable_p, 2 ) );
	printf( "%s\n", ht_get( hashtable_p, 3 ) );
	printf( "%s\n", ht_get( hashtable_p, 4 ) );
	printf( "%s\n", ht_get( hashtable_p, 1 ) );

    perf_test(hashtable);
	return 0;
}
