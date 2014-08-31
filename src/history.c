#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "history.h"

const History HistoryMax=(((History)1)<<HistoryBit);

History history[PieceNB][SqNB];

void historyInc(Piece fromPiece, Sq toSq, unsigned int depth)
{
  assert(pieceIsValid(fromPiece));
  assert(sqIsValid(toSq));
  
  // Increment count in table
  History *counter=&history[fromPiece][toSq];
  *counter+=(((History)1)<<depth);
  
  // Overflow? (not a literal overflow, but beyond desired range)
  if (*counter>=HistoryMax)
    historyAge();
}

History historyGet(Piece fromPiece, Sq toSq)
{
  assert(pieceIsValid(fromPiece));
  assert(sqIsValid(toSq));
  return history[fromPiece][toSq];
}

void historyAge(void)
{
  unsigned int i, j;
  for(i=0;i<PieceNB;++i)
    for(j=0;j<SqNB;++j)
      history[i][j]/=2;
}

void historyClear(void)
{
  memset(history, 0, sizeof(history));
}
