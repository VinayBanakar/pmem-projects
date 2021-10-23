#include <fcntl.h>
#include <libpmemobj.h>
#include <stdio.h>
#include <stdlib.h>
struct my_root {
	int value;
	int is_odd;
};

struct tmp {
	int val;	
	char* str;
};

POBJ_LAYOUT_BEGIN(example);
POBJ_LAYOUT_ROOT(example, struct my_root);
POBJ_LAYOUT_END(example);

int main(int argc, char** argv)
{
	/* create a pool within an existing file */
	PMEMobjpool *pop = pmemobj_create("/users/vinay_b/pmem-projects/tx_samp",
				POBJ_LAYOUT_NAME(example),
				8388608, 0666);
	if (pop == NULL) {

		fprintf(stderr, "failed to create pool: %s\n", pmemobj_errormsg());
		return 0;
	}

	struct tmp* ab;
	
	TX_BEGIN(pop) {
		TX_ADD_DIRECT(&ab);		
		ab = TX_NEW(struct tmp);
		TOID(struct my_root) root = POBJ_ROOT(pop, struct my_root);
		/* track the value field */
		TX_ADD_FIELD(root, value);
		//TX_ADD_FIELD(root, is_odd);
		D_RW(root)->value = 4;
		/* modify an untracked value */
		D_RW(root)->is_odd = D_RO(root)->value % 2;
		pmemobj_tx_abort(-1);
		TX_BEGIN(pop){
			D_RW(root)->value=5;
		} TX_END
		printf("%d\n", D_RW(root)->value);
	}TX_END
}
