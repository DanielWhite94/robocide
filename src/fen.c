#include <stdlib.h>
#include <string.h>

#include "fen.h"

bool fenRead(Fen *data, const char *string) {
	// Fen requires 4-6 fields separated by spaces.
	const char *fields[6];
	fields[0]=string;
	unsigned int fieldCount;
	for(fieldCount=1;fieldCount<6;fieldCount++) {
		char *next=strstr(fields[fieldCount-1], " ");
		if (next==NULL)
			break;
		fields[fieldCount]=next+1;
	}
	if (fieldCount<=3)
		return false; // Not enough fields given.

	// Clear data struct.
	memset(data->array, PieceNone, SqNB*sizeof(Piece));
	data->stm=ColourWhite;
	data->castRights=CastRightsNone;
	data->epSq=SqInvalid;
	data->halfMoveNumber=0;
	data->fullMoveNumber=1;

	// 1. Piece placement.
	const char *c=fields[0];
	unsigned int x=0, y=7;
	while(*c!=' ') // We know a space exists before the null byte.
		switch(*c++) {
			case '1': x+=1; break;
			case '2': x+=2; break;
			case '3': x+=3; break;
			case '4': x+=4; break;
			case '5': x+=5; break;
			case '6': x+=6; break;
			case '7': x+=7; break;
			case '8': x+=8; break;
			case '/':
				if (x!=8)
					return false;
				x=0;
				--y;
			break;
			case 'P': data->array[sqMake(x++,y)]=PieceWPawn; break;
			case 'N': data->array[sqMake(x++,y)]=PieceWKnight; break;
			case 'B': data->array[sqMake(x,y)]=(sqIsLight(sqMake(x,y)) ? PieceWBishopL : PieceWBishopD); ++x; break;
			case 'R': data->array[sqMake(x++,y)]=PieceWRook; break;
			case 'Q': data->array[sqMake(x++,y)]=PieceWQueen; break;
			case 'K': data->array[sqMake(x++,y)]=PieceWKing; break;
			case 'p': data->array[sqMake(x++,y)]=PieceBPawn; break;
			case 'n': data->array[sqMake(x++,y)]=PieceBKnight; break;
			case 'b': data->array[sqMake(x,y)]=(sqIsLight(sqMake(x,y)) ? PieceBBishopL : PieceBBishopD); ++x; break;
			case 'r': data->array[sqMake(x++,y)]=PieceBRook; break;
			case 'q': data->array[sqMake(x++,y)]=PieceBQueen; break;
			case 'k': data->array[sqMake(x++,y)]=PieceBKing; break;
			default: return false; break;
		}
	if (y!=0 || x!=8)
		return false;

	// 2. Active colour.
	switch(fields[1][0]) {
		case 'w': data->stm=ColourWhite; break;
		case 'b': data->stm=ColourBlack; break;
		default: return false; break;
	}

	// 3. Castling availability.
	c=fields[2];
	while(*c!=' ') // We know a space exists before the null byte.
		switch(*c++) {
			case 'K': data->castRights|=CastRightsK; break;
			case 'Q': data->castRights|=CastRightsQ; break;
			case 'k': data->castRights|=CastRightsk; break;
			case 'q': data->castRights|=CastRightsq; break;
			case '-': break;
			default: return false; break;
		}

	// 4. En passent target square.
	if (fields[3][0]!='\0' && fields[3][1]!='\0' && fields[3][0]!='-') {
		File file=fileFromChar(fields[3][0]);
		Rank rank=rankFromChar(fields[3][1]);
		if (fileIsValid(file) && rankIsValid(rank))
			data->epSq=sqMake(file, rank);
	}

	// 5. Halfmove number.
	if (fieldCount>=5)
		data->halfMoveNumber=atoi(fields[4]);

	// 6. Fullmove number.
	if (fieldCount>=6)
		data->fullMoveNumber=atoi(fields[5]);

	return true;
}
