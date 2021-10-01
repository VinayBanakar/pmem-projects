#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

#define TOMBSTONE_MASK (1ULL << 63)

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
hashtable_t *ht_create( int size ) {

	hashtable_t *hashtable = NULL;
	int i;

	if( size < 1 ) return NULL;

	/* Allocate the table itself. */
	if( ( hashtable = malloc( sizeof( hashtable_t ) ) ) == NULL ) {
		return NULL;
	}

	/* Allocate pointers to the head nodes. */
	if( ( hashtable->table = malloc( sizeof( entry_t * ) * size ) ) == NULL ) {
		return NULL;
	}
	for( i = 0; i < size; i++ ) {
		hashtable->table[i] = NULL;
	}

	hashtable->size = size;

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

	// printf("Inserting %llu and %s\n", key, value);

	bin = ht_hash( hashtable, key );

	next = hashtable->table[ bin ];

	while( next != NULL && next->key != 0 && key > next->key) {
		last = next;
		next = next->next;
	}

	/* There's already a pair.  Let's replace that string. */
	if( next != NULL && next->key != 0 && key == next->key ) {

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

hashtable_t *ht_expand(hashtable_t *hashtable, int new_size){

	if( new_size < 1 ) return NULL;
	if(new_size == hashtable-> size) return NULL;
	if(hashtable -> size > new_size) return NULL; // preserving existing data.

	hashtable_t *new_hashtable = NULL;

	/* Allocate the table itself. */
	if( ( new_hashtable = malloc( sizeof( hashtable_t ) ) ) == NULL ) {
		return NULL;
	}
	if( ( new_hashtable->table = malloc( sizeof( entry_t * ) * new_size ) ) == NULL ) {
		return NULL;
	}

	int i;
    // Null initialize the new table.
	for( i = 0; i < new_size; i++ ) {
		new_hashtable->table[i] = NULL;
	}
	new_hashtable->size = new_size;
	
	entry_t *pair = NULL;
    // Now move old keys to new bins and free old entries
	for(i = 0; i < hashtable->size; i++){
		pair = hashtable->table[i];
		if(pair != NULL && pair->key != 0){
			ht_set(new_hashtable, pair->key, pair->value);
			free(pair);
		}
	}
	//free old hash table
	free(hashtable);
    return new_hashtable; 
}

// Should atomically remove element from one table and add it to another.
// Move data from ht1 to ht2
bool ht_move(hashtable_t *ht1, hashtable_t *ht2){
	// atomically move data
	if(ht1->size != ht2->size) return false;
	for(i=0; i< ht1->size; ++i){
		
	}
}

int main( int argc, char **argv ) {

	//PMEMobjpool *pool = pmemobj_open(POOL, LAYOUT);
    hashtable_t *hashtable = ht_create( 6 );

	ht_set( hashtable, 1, "inky" );
	ht_set( hashtable, 2, "pinky" );
	ht_set( hashtable, 3, "blinky" );
	ht_set( hashtable, 4, "floyd" );
	ht_set( hashtable, 1, "loyd" );

	hashtable_t * ht2 = ht_expand(hashtable, 8);
	ht_set(ht2, 10, "exp");
	ht_set(ht2, 11, "exp2");

	printf( "%s\n", ht_get( ht2, 10) );
	printf( "%s\n", ht_get( ht2, 1 ) );
	printf( "%s\n", ht_get( ht2, 2 ) );
	printf( "%s\n", ht_get( ht2, 3 ) );
	printf( "%s\n", ht_get( ht2, 4 ) );
	printf( "%s\n", ht_get( ht2, 1 ) );

	return 0;
}