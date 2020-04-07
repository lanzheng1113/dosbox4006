/*
 *  Copyright (C) 2002-2015  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Wengier: DBCS support
 */


#include "dos_inc.h"
#include "../ints/int10.h"
#include "inout.h"
#include <string.h>

#define NUMBER_ANSI_DATA 10

class device_CON : public DOS_Device {
public:
	device_CON();
	bool Read(Bit8u * data,Bit16u * size);
	bool Write(Bit8u * data,Bit16u * size);
	bool Seek(Bit32u * pos,Bit32u type);
	bool Close();
	void ClearAnsi(void);
	Bit16u GetInformation(void);
	bool ReadFromControlChannel(PhysPt bufptr,Bit16u size,Bit16u * retcode){return false;}
	bool WriteToControlChannel(PhysPt bufptr,Bit16u size,Bit16u * retcode){return false;}

private:
	Bit8u readcache;
	Bit8u lastwrite;
	struct ansi { /* should create a constructor, which would fill them with the appropriate values */
		bool esc;
		bool sci;
		bool enabled;
		Bit8u attr;
		Bit8u data[NUMBER_ANSI_DATA];
		Bit8u numberofarg;
		Bit8s savecol;
		Bit8s saverow;
		bool warned;
	} ansi;

	static void Real_INT10_SetCursorPos(Bit8u row,Bit8u col,Bit8u page) {
		Bit16u		oldax,oldbx,olddx;

		oldax=reg_ax;
		oldbx=reg_bx;
		olddx=reg_dx;

		reg_ah=0x2;
		reg_dh=row;
		reg_dl=col;
		reg_bh=page;
		CALLBACK_RunRealInt(0x10);

		reg_ax=oldax;
		reg_bx=oldbx;
		reg_dx=olddx;
	}


	static void Real_INT10_TeletypeOutput(Bit8u xChar,Bit8u xAttr) {
		Bit16u		oldax,oldbx;

		oldax=reg_ax;
		oldbx=reg_bx;

		reg_ah=0xE;
		reg_al=xChar;
		reg_bl=xAttr;
		CALLBACK_RunRealInt(0x10);

		reg_ax=oldax;
		reg_bx=oldbx;
	}


	static void Real_WriteChar(Bit8u cur_col,Bit8u cur_row,
					Bit8u page,Bit8u chr,Bit8u attr,Bit8u useattr) {
		//Cursor position
		Real_INT10_SetCursorPos(cur_row,cur_col,page);

		//Write the character
		Bit16u		oldax,oldbx,oldcx;
		oldax=reg_ax;
		oldbx=reg_bx;
		oldcx=reg_cx;

		reg_al=chr;
		reg_bl=attr;
		reg_bh=page;
		reg_cx=1;
		if(useattr)
				reg_ah=0x9;
		else	reg_ah=0x0A;
		CALLBACK_RunRealInt(0x10);

		reg_ax=oldax;
		reg_bx=oldbx;
		reg_cx=oldcx;
	}//static void Real_WriteChar(cur_col,cur_row,page,chr,attr,useattr)

	
	static void AdjustCursorPosition(Bit8u& cur_col,Bit8u& cur_row) {
		BIOS_NCOLS;BIOS_NROWS;
		//Need a new line?
		if(cur_col==ncols) 
		{
			cur_col=0;
			cur_row++;
			Real_INT10_TeletypeOutput('\r',0x7);
		}
		
		//Reached the bottom?
		if(cur_row==nrows) 
		{
			Real_INT10_TeletypeOutput('\n',0x7);	//Scroll up
			cur_row--;
		}
	}


	void Real_INT10_TeletypeOutputAttr(Bit8u chr,Bit8u attr,bool useattr) {
		//TODO Check if this page thing is correct
		Bit8u page=real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
//		BIOS_NCOLS;BIOS_NROWS;
		Bit8u cur_row=CURSOR_POS_ROW(page);
		Bit8u cur_col=CURSOR_POS_COL(page);
		switch (chr) 
		{
		case 7: {
			// set timer (this should not be needed as the timer already is programmed 
			// with those values, but the speaker stays silent without it)
			IO_Write(0x43,0xb6);
			IO_Write(0x42,1320&0xff);
			IO_Write(0x42,1320>>8);
			// enable speaker
			IO_Write(0x61,IO_Read(0x61)|0x3);
			for(Bitu i=0; i < 333; i++) CALLBACK_Idle();
			IO_Write(0x61,IO_Read(0x61)&~0x3);
			break;
		}
		case 8:
			if(cur_col>0)
				cur_col--;
			break;
		case '\r':
			cur_col=0;
			break;
		case '\n':
			cur_col=0;
			cur_row++;
			break;
		case '\t':
			do {
				Real_INT10_TeletypeOutputAttr(' ',attr,useattr);
				cur_row=CURSOR_POS_ROW(page);
				cur_col=CURSOR_POS_COL(page);
			} while(cur_col%8);
			break;
		default:
			//* Draw the actual Character 
			Real_WriteChar(cur_col,cur_row,page,chr,attr,useattr);
			cur_col++;
		}
		
		AdjustCursorPosition(cur_col,cur_row);
		Real_INT10_SetCursorPos(cur_row,cur_col,page);	
	}//void Real_INT10_TeletypeOutputAttr(Bit8u chr,Bit8u attr,bool useattr) 
};



bool device_CON::Read(Bit8u * data,Bit16u * size) {
	Bit16u oldax=reg_ax;
	Bit16u count=0;
	INT10_SetCurMode();
	if ((readcache) && (*size)) {
		data[count++]=readcache;
		if(dos.echo) Real_INT10_TeletypeOutput(readcache,7);
		readcache=0;
	}
	while (*size>count) {
	/*	while (true) {
			reg_ah=0x1; // check for keystroke
			CALLBACK_RunRealInt(0x16);
			if (!GETFLAG(ZF)) break;
			CALLBACK_RunRealInt(0x28);
		}; */ //DOSIdle
		reg_ah=(IS_EGAVGA_ARCH)?0x10:0x0;
		CALLBACK_RunRealInt(0x16);
		switch(reg_al) {
		case 13:
			data[count++]=0x0D;
			if (*size>count) data[count++]=0x0A;    // it's only expanded if there is room for it. (NO cache)
			*size=count;
			reg_ax=oldax;
			if(dos.echo) { 
				Real_INT10_TeletypeOutput(13,7); //maybe don't do this ( no need for it actually ) (but it's compatible)
				Real_INT10_TeletypeOutput(10,7);
			}
			return true;
			break;
		case 8:
			if(*size==1) data[count++]=reg_al;  //one char at the time so give back that BS
			else if(count) {                    //Remove data if it exists (extended keys don't go right)
				data[count--]=0;
				Real_INT10_TeletypeOutput(8,7);
				Real_INT10_TeletypeOutput(' ',7);
			} else {
				continue;                       //no data read yet so restart whileloop.
			}
			break;
		case 0xe0: /* Extended keys in the  int 16 0x10 case */
			if(!reg_ah) { /*extended key if reg_ah isn't 0 */
				data[count++] = reg_al;
			} else {
				data[count++] = 0;
				if (*size>count) data[count++] = reg_ah;
				else readcache = reg_ah;
			}
			break;
		case 0: /* Extended keys in the int 16 0x0 case */
			data[count++]=reg_al;
			if (*size>count) data[count++]=reg_ah;
			else readcache=reg_ah;
			break;
		default:
			data[count++]=reg_al;
			break;
		}
		if(dos.echo) { //what to do if *size==1 and character is BS ?????
			// TODO: If CTRL+C checking is applicable do not echo (reg_al == 3)
			Real_INT10_TeletypeOutput(reg_al,7);
		}
	}
	*size=count;
	reg_ax=oldax;
	return true;
}


bool device_CON::Write(Bit8u * data,Bit16u * size) {
	Bit16u count=0;
	Bitu i;
	Bit8u col,row,page;
	Bit16u ncols,nrows;
	Bit8u tempdata;
	INT10_SetCurMode();
	while (*size>count) {
		if (!ansi.esc){
			if(data[count]=='\033') {
				/*clear the datastructure */
				ClearAnsi();
				/* start the sequence */
				ansi.esc=true;
				count++;
				continue;
			} else if(data[count] == '\t' && !dos.direct_output) {
				/* expand tab if not direct output */
				page = real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
				do {
					Real_INT10_TeletypeOutputAttr(' ',ansi.enabled?ansi.attr:7,true);
					col=CURSOR_POS_COL(page);
				} while(col%8);
				lastwrite = data[count++];
				continue;
			} else { 
				/* Some sort of "hack" now that '\n' doesn't set col to 0 (int10_char.cpp old chessgame) */
				if((data[count] == '\n') && (lastwrite != '\r'))
					Real_INT10_TeletypeOutputAttr('\r',ansi.enabled?ansi.attr:7,true);
 				/* pass attribute only if ansi is enabled */
				Real_INT10_TeletypeOutputAttr(data[count],ansi.enabled?ansi.attr:7,true);
				lastwrite = data[count++];
				continue;
		}
	}

	if(!ansi.sci){
            
		switch(data[count]){
		case '[': 
			ansi.sci=true;
			break;
		case '7': /* save cursor pos + attr */
		case '8': /* restore this  (Wonder if this is actually used) */
		case 'D':/* scrolling DOWN*/
		case 'M':/* scrolling UP*/ 
		default:
			LOG(LOG_IOCTL,LOG_NORMAL)("ANSI: unknown char %c after a esc",data[count]); /*prob () */
			ClearAnsi();
			break;
		}
		count++;
		continue;
	}
	/*ansi.esc and ansi.sci are true */
	page = real_readb(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
	switch(data[count]){
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			ansi.data[ansi.numberofarg]=10*ansi.data[ansi.numberofarg]+(data[count]-'0');
			break;
		case ';': /* till a max of NUMBER_ANSI_DATA */
			ansi.numberofarg++;
			break;
		case 'm':               /* SGR */
			for(i=0;i<=ansi.numberofarg;i++){ 
				ansi.enabled=true;
				switch(ansi.data[i]){
				case 0: /* normal */
					ansi.attr=0x07;//Real ansi does this as well. (should do current defaults)
					ansi.enabled=false;
					break;
				case 1: /* bold mode on*/
					ansi.attr|=0x08;
					break;
				case 4: /* underline */
					LOG(LOG_IOCTL,LOG_NORMAL)("ANSI:no support for underline yet");
					break;
				case 5: /* blinking */
					ansi.attr|=0x80;
					break;
				case 7: /* reverse */
					ansi.attr=0x70;//Just like real ansi. (should do use current colors reversed)
					break;
				case 30: /* fg color black */
					ansi.attr&=0xf8;
					ansi.attr|=0x0;
					break;
				case 31:  /* fg color red */
					ansi.attr&=0xf8;
					ansi.attr|=0x4;
					break;
				case 32:  /* fg color green */
					ansi.attr&=0xf8;
					ansi.attr|=0x2;
					break;
				case 33: /* fg color yellow */
					ansi.attr&=0xf8;
					ansi.attr|=0x6;
					break;
				case 34: /* fg color blue */
					ansi.attr&=0xf8;
					ansi.attr|=0x1;
					break;
				case 35: /* fg color magenta */
					ansi.attr&=0xf8;
					ansi.attr|=0x5;
					break;
				case 36: /* fg color cyan */
					ansi.attr&=0xf8;
					ansi.attr|=0x3;
					break;
				case 37: /* fg color white */
					ansi.attr&=0xf8;
					ansi.attr|=0x7;
					break;
				case 40:
					ansi.attr&=0x8f;
					ansi.attr|=0x0;
					break;
				case 41:
					ansi.attr&=0x8f;
					ansi.attr|=0x40;
					break;
				case 42:
					ansi.attr&=0x8f;
					ansi.attr|=0x20;
					break;
				case 43:
					ansi.attr&=0x8f;
					ansi.attr|=0x60;
					break;
				case 44:
					ansi.attr&=0x8f;
					ansi.attr|=0x10;
					break;
				case 45:
					ansi.attr&=0x8f;
					ansi.attr|=0x50;
					break;
				case 46:
					ansi.attr&=0x8f;
					ansi.attr|=0x30;
					break;	
				case 47:
					ansi.attr&=0x8f;
					ansi.attr|=0x70;
					break;
				default:
					break;
				}
			}
			ClearAnsi();
			break;
		case 'f':
		case 'H':/* Cursor Pos*/
			if(!ansi.warned) { //Inform the debugger that ansi is used.
				ansi.warned = true;
				LOG(LOG_IOCTL,LOG_WARN)("ANSI SEQUENCES USED");
			}
			ncols = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
			nrows = real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS) + 1;
			/* Turn them into positions that are on the screen */
			if(ansi.data[0] == 0) ansi.data[0] = 1;
			if(ansi.data[1] == 0) ansi.data[1] = 1;
			if(ansi.data[0] > nrows) ansi.data[0] = (Bit8u)nrows;
			if(ansi.data[1] > ncols) ansi.data[1] = (Bit8u)ncols;
			Real_INT10_SetCursorPos(--(ansi.data[0]),--(ansi.data[1]),page); /*ansi=1 based, int10 is 0 based */
			ClearAnsi();
			break;
			/* cursor up down and forward and backward only change the row or the col not both */
		case 'A': /* cursor up*/
			col=CURSOR_POS_COL(page) ;
			row=CURSOR_POS_ROW(page) ;
			tempdata = (ansi.data[0]? ansi.data[0] : 1);
			if(tempdata > row) { row=0; } 
			else { row-=tempdata;}
			Real_INT10_SetCursorPos(row,col,page);
			ClearAnsi();
			break;
		case 'B': /*cursor Down */
			col=CURSOR_POS_COL(page) ;
			row=CURSOR_POS_ROW(page) ;
			nrows = real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS) + 1;
			tempdata = (ansi.data[0]? ansi.data[0] : 1);
			if(tempdata + static_cast<Bitu>(row) >= nrows)
				{ row = nrows - 1;}
			else	{ row += tempdata; }
			Real_INT10_SetCursorPos(row,col,page);
			ClearAnsi();
			break;
		case 'C': /*cursor forward */
			col=CURSOR_POS_COL(page);
			row=CURSOR_POS_ROW(page);
			ncols = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
			tempdata=(ansi.data[0]? ansi.data[0] : 1);
			if(tempdata + static_cast<Bitu>(col) >= ncols) 
				{ col = ncols - 1;} 
			else	{ col += tempdata;}
			Real_INT10_SetCursorPos(row,col,page);
			ClearAnsi();
			break;
		case 'D': /*Cursor Backward  */
			col=CURSOR_POS_COL(page);
			row=CURSOR_POS_ROW(page);
			tempdata=(ansi.data[0]? ansi.data[0] : 1);
			if(tempdata > col) {col = 0;}
			else { col -= tempdata;}
			Real_INT10_SetCursorPos(row,col,page);
			ClearAnsi();
			break;
		case 'J': /*erase screen and move cursor home*/
			if(ansi.data[0]==0) ansi.data[0]=2;
			if(ansi.data[0]!=2) {/* every version behaves like type 2 */
				LOG(LOG_IOCTL,LOG_NORMAL)("ANSI: esc[%dJ called : not supported handling as 2",ansi.data[0]);
			}
			INT10_ScrollWindow(0,0,255,255,0,ansi.attr,page);
			ClearAnsi();
			Real_INT10_SetCursorPos(0,0,page);
			break;
		case 'h': /* SET   MODE (if code =7 enable linewrap) */
		case 'I': /* RESET MODE */
			LOG(LOG_IOCTL,LOG_NORMAL)("ANSI: set/reset mode called(not supported)");
			ClearAnsi();
			break;
		case 'u': /* Restore Cursor Pos */
			Real_INT10_SetCursorPos(ansi.saverow,ansi.savecol,page);
			ClearAnsi();
			break;
		case 's': /* SAVE CURSOR POS */
			ansi.savecol=CURSOR_POS_COL(page);
			ansi.saverow=CURSOR_POS_ROW(page);
			ClearAnsi();
			break;
		case 'K': /* erase till end of line (don't touch cursor) */
			col = CURSOR_POS_COL(page);
			row = CURSOR_POS_ROW(page);
			ncols = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
			INT10_WriteChar(' ',ansi.attr,page,ncols-col,true); //Use this one to prevent scrolling when end of screen is reached
			//for(i = col;i<(Bitu) ncols; i++) INT10_TeletypeOutputAttr(' ',ansi.attr,true);
			Real_INT10_SetCursorPos(row,col,page);
			ClearAnsi();
			break;
		case 'M': /* delete line (NANSI) */
			row = CURSOR_POS_ROW(page);
			ncols = real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS);
			nrows = real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS) + 1;
			INT10_ScrollWindow(row,0,nrows-1,ncols-1,ansi.data[0]? -ansi.data[0] : -1,ansi.attr,0xFF);
			ClearAnsi();
			break;
		case 'l':/* (if code =7) disable linewrap */
		case 'p':/* reassign keys (needs strings) */
		case 'i':/* printer stuff */
		default:
			LOG(LOG_IOCTL,LOG_NORMAL)("ANSI: unhandled char %c in esc[",data[count]);
			ClearAnsi();
			break;
		}
		count++;
	}
	*size=count;
	return true;
}

bool device_CON::Seek(Bit32u * pos,Bit32u type) {
	// seek is valid
	*pos = 0;
	return true;
}

bool device_CON::Close() {
	return true;
}

Bit16u device_CON::GetInformation(void) {
	Bit16u head=mem_readw(BIOS_KEYBOARD_BUFFER_HEAD);
	Bit16u tail=mem_readw(BIOS_KEYBOARD_BUFFER_TAIL);

	if ((head==tail) && !readcache) return 0x80D3;	/* No Key Available */
	if (readcache || real_readw(0x40,head)) return 0x8093;		/* Key Available */

	/* remove the zero from keyboard buffer */
	Bit16u start=mem_readw(BIOS_KEYBOARD_BUFFER_START);
	Bit16u end	=mem_readw(BIOS_KEYBOARD_BUFFER_END);
	head+=2;
	if (head>=end) head=start;
	mem_writew(BIOS_KEYBOARD_BUFFER_HEAD,head);
	return 0x80D3; /* No Key Available */
}

device_CON::device_CON() {
	SetName("CON");
	readcache=0;
	lastwrite=0;
	ansi.enabled=false;
	ansi.attr=0x7;
	ansi.saverow=0;
	ansi.savecol=0;
	ansi.warned=false;
	ClearAnsi();
}

void device_CON::ClearAnsi(void){
	for(Bit8u i=0; i<NUMBER_ANSI_DATA;i++) ansi.data[i]=0;
	ansi.esc=false;
	ansi.sci=false;
	ansi.numberofarg=0;
}
