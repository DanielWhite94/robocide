#include <assert.h>
#include <string.h>

#include "countermove.h"

Move counterMove[MoveNB];

Move counterMoveGetResponseMove(Move prevMove) {
	return counterMove[prevMove];
}

void counterMoveCutoff(Move prevMove, Move responseMove) {
	assert(moveIsValid(responseMove));

	counterMove[prevMove]=responseMove;
}

void counterMoveClear(void) {
	STATICASSERT(MoveInvalid==0);
	memset(counterMove, 0, sizeof(counterMove));
}
