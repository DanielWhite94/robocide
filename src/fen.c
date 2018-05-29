#include <assert.h>
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
	data->castRights=posCastRightsFromStr(c);

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

void fenWrite(Fen *data, char string[static 128]) {
	assert(data!=NULL);

	char tempStr[64];
	string[0]='\0';

	// 1. Piece placement.
	int x, y;
	for(y=7; y>=0; --y) {
		int gap=0;
		for(x=0; x<8; ++x) {
			Sq sq=sqMake(x, y);
			if (data->array[sq]==PieceNone) {
				++gap;
				continue;
			}

			if (gap>0) {
				sprintf(tempStr, "%i", gap);
				strcat(string, tempStr);
				gap=0;
			}

			sprintf(tempStr, "%c", pieceToChar(data->array[sq]));
			strcat(string, tempStr);
		}

		if (gap>0) {
			sprintf(tempStr, "%i", gap);
			strcat(string, tempStr);
		}

		if (y!=0)
			strcat(string, "/");
	}

	// 2. Active colour.
	strcat(string, (data->stm==ColourWhite ? " w" : " b"));

	// 3. Castling availability.
	strcat(string, " ");

	posCastRightsToStr(data->castRights, tempStr);
	strcat(string, tempStr);

	// 4. En passent target square.
	if (data->epSq!=SqInvalid) {
		sprintf(tempStr, " %c%c", fileToChar(sqFile(data->epSq)), rankToChar(sqRank(data->epSq)));
		strcat(string, tempStr);
	} else
		strcat(string, " -");

	// 5 & 6. Halfmove and fullmove numbers.
	sprintf(tempStr, " %i %i", data->halfMoveNumber, data->fullMoveNumber);
	strcat(string, tempStr);
}

void fenFromPos(Fen *data, const Pos *pos) {
	assert(data!=NULL);
	assert(pos!=NULL);

	Sq sq;
	for(sq=0; sq<SqNB; ++sq)
		data->array[sq]=posGetPieceOnSq(pos, sq);
	data->stm=posGetSTM(pos);
	data->castRights=posGetCastRights(pos);
	data->epSq=posGetEPSq(pos);
	data->halfMoveNumber=posGetHalfMoveNumber(pos);
	data->fullMoveNumber=posGetFullMoveNumber(pos);
}
