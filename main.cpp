#define _CRT_SECURE_NO_DEPRECATE 1
#define RW 0.3086f
#define GW 0.6094f
#define BW 0.0820f
int order[2160];

#include <stdio.h>
#include <math.h>
#include <windows.h>

#include "ScriptInterpreter.h"
#include "ScriptError.h"
#include "ScriptValue.h"

#include "resource.h"
#include "filter.h"

const static int _key_table[256] = {
	10, 11, 12, 13, 16, 17, 18, 19, 13, 14, 15, 16,  0,  1,  2,  3,
	21, 22, 23, 24, 18, 19, 20, 21, 23, 24, 25, 26, 26, 27, 28, 29,
	19, 20, 21, 22, 11, 12, 13, 14, 28, 29, 30, 31,  4,  5,  6,  7,
	22, 23, 24, 25,  5,  6,  7,  8, 31,  0,  1,  2, 27, 28, 29, 30,
	 3,  4,  5,  6,  8,  9, 10, 11, 14, 15, 16, 17, 25, 26, 27, 28,
	15, 16, 17, 18,  7,  8,  9, 10, 17, 18, 19, 20, 29, 30, 31,  0,
	24, 25, 26, 27, 20, 21, 22, 23,  1,  2,  3,  4,  6,  7,  8,  9,
	12, 13, 14, 15,  9, 10, 11, 12,  2,  3,  4,  5, 30, 31,  0,  1,
	24, 25, 26, 27,  2,  3,  4,  5, 31,  0,  1,  2,  7,  8,  9, 10,
	13, 14, 15, 16, 26, 27, 28, 29, 14, 15, 16, 17, 18, 19, 20, 21,
	22, 23, 24, 25,  5,  6,  7,  8, 19, 20, 21, 22, 12, 13, 14, 15,
	17, 18, 19, 20, 27, 28, 29, 30, 10, 11, 12, 13, 11, 12, 13, 14,
	 6,  7,  8,  9,  1,  2,  3,  4,  0,  1,  2,  3,  4,  5,  6,  7,
	 3,  4,  5,  6,  8,  9, 10, 11, 15, 16, 17, 18, 23, 24, 25, 26,
	29, 30, 31,  0, 25, 26, 27, 28,  9, 10, 11, 12, 21, 22, 23, 24,
	20, 21, 22, 23, 30, 31,  0,  1, 16, 17, 18, 19, 28, 29, 30, 31
};
/* Global definitions */

static FilterDefinition *fd_NagravisionFilter;
void RGB2YUV(int r,int g,int b,int &Y,int &U,int &V);
void YUV2RGB(int Y,int U,int V,int &r,int &g,int &b);
void _update_line_order(int hh);
void _seedit(int seed);
int strToBin(const char * str);

/* Filter data definition */
typedef struct NGFilterData {
    int	 ngSeed;
	bool NagravisionMode;
	bool modePAL;
	bool ngHFSeed;
} NGFilterData;


/* Nagravision Filter Definition */

int RunProcNagravisionFilter(const FilterActivation *fa, const FilterFunctions *ff);
int StartProcNagravisionFilter(FilterActivation *fa, const FilterFunctions *ff);
int EndProcNagravisionFilter(FilterActivation *fa, const FilterFunctions *ff);
int ConfigProcNagravisionFilter(FilterActivation *fa, const FilterFunctions *ff, HWND hwnd);
void StringProcNagravisionFilter(const FilterActivation *fa, const FilterFunctions *ff, char *str);
int InitProcNagravisionFilter(FilterActivation *fa, const FilterFunctions *ff);
char *dsttemp;
char *dstbufframe;
char *tagline;



bool FssProcNagravisionFilter(FilterActivation *fa, const FilterFunctions *ff, char *buf, int buflen) {
	NGFilterData *mfd = (NGFilterData *)fa->filter_data;

	_snprintf(buf, buflen, "Config(%d, %d, %d, %d)",
		mfd->ngSeed,
		mfd->modePAL,
		mfd->NagravisionMode,
		mfd->ngHFSeed);

	return true;
}

void ScriptConfigNagravisionFilter(IScriptInterpreter *isi, void *lpVoid, CScriptValue *argv, int argc) {
	FilterActivation *fa = (FilterActivation *)lpVoid;
	NGFilterData *mfd = (NGFilterData *)fa->filter_data;

	mfd->ngSeed			= argv[0].asInt();
	mfd->modePAL		= !!argv[1].asInt();
	mfd->NagravisionMode	= !!argv[2].asInt();
	mfd->ngHFSeed	= !!argv[3].asInt();
	
}

ScriptFunctionDef func_defs[]={
	{ (ScriptFunctionPtr)ScriptConfigNagravisionFilter, "Config", "0iiii" },
	{ NULL },
};

CScriptObject script_objec={
	NULL, func_defs
};

int RunProcNagravisionFilter(const FilterActivation *fa, const FilterFunctions *ff)
{
	/* Pointer to MFD */
	NGFilterData *mfd = (NGFilterData *)fa->filter_data;
	
	/* Pointers to source and destination frame stores */
	char *src = (char *)fa->src.data;
	char *dst = (char *)fa->dst.data;

	/* Get line length */
	int dstline = fa->dst.w*sizeof(Pixel32)+fa->dst.modulo;
	int srcline = fa->src.w*sizeof(Pixel32)+fa->src.modulo;

	/* Destination frame width, height and half-height*/
	int w = fa->dst.w; 
	const int h = fa->dst.h;
	const int hh = (int)h/2;

	/* Source and target width address */
	int destx = fa->dst.w*sizeof(Pixel32);
	int srcx = fa->src.w*sizeof(Pixel32);

	/* PRNG parameters */
	int frame = fa->pfsi->lCurrentSourceFrame; // frame number

	/* Cut point locations for single cut mode */
	int min = (int) (srcx*.06);
	int max = (int) (srcx*.25);
	
	/* Define RGB/YUV variables */
	int r,g,b,Y,U,V;

	int buff = (int) (hh/9);

	if (mfd->NagravisionMode) {

		/* 
		Tag format 
		==========
		First 7 high bits:	0011001 = Nagravision
		Rest of bits: randomised
		*/

		char seed[1024]="";
		int  _tags[2] = {0xFF,0x00};
		int tag[32];
		int ngtag[7]={0x00,0x00,0xFF,0xFF,0x00,0x00,0xFF};
		sprintf(seed, "%s", "0011001");

		/* Generate seed for TAG LINE based on current frame and mfd->ngSeed value */
		srand((mfd->ngSeed+10)*((frame%256)+10)); if ((frame+2)%2 == 0) {	srand((rand()) + rand()); }	else { srand(rand() * (rand())); }

		for (int i=0; i < 7; i++)
				tag[i] = ngtag[i];

		for (int i=7;i<32;i++)
		{
			// Random tag - black or white
			tag[i] = _tags[rand()%2];
			sprintf(seed, "%s%i", seed,(tag[i] == 0xFF ? 1 : 0));
		}

		memset(tagline,0x00,srcx);
		for (int i=0; i < 32; i++) 
		{
			memset(tagline+((srcx/256)*4)*i,tag[i],(srcx/256)*4);
		}

		if (mfd->modePAL) {
			/* Scramble chroma */

			/* Pointer to source and destination structures */
			Pixel32 *dstc= fa->dst.data;
			Pixel32 *srcc= fa->src.data;

			/* Setup constants for rotation values */
			const double cosa[4] = {cos(45.0)*10000000,cos(135.0)*10000000,cos(225.0)*10000000,cos(315.0)*10000000};
			const double sina[4] = {sin(45.0)*10000000,sin(135.0)*10000000,sin(225.0)*10000000,sin(315.0)*10000000};

			/* Get seed */
			_seedit(strToBin(seed)*mfd->ngSeed);
		
			for (int i=0; i<h; i++)
			{		
				int H = (rand() % 3);

				for (int x=0; x<w; x++)
				{
					r= (srcc[x]>>16)&0xff;
				  	g= (srcc[x]>> 8)&0xff;
				  	b= (srcc[x]    )&0xff;

					/* Convert to YUV */
					RGB2YUV (r,g,b,Y,U,V);
									
					/* Rotate */
					int Ur = (int) ((((U-128)*10000000) * cosa[H] + ((V-128)*10000000) * sina[H])/10000000/10000000 + 128);
					int Vr = (int) ((((V-128)*10000000) * cosa[H] - ((U-128)*10000000) * sina[H])/10000000/10000000 + 128);
	
					Ur=(Ur+256)/3;
					Vr=(Vr+256)/3;

					/* Conver to RGB */
					YUV2RGB(Y,Ur,Vr,r,g,b);

					dstc[x] = (r << 16) | (g << 8) | (b);
				}
				
				/* Move onto next line */
				srcc+= fa->src.pitch>>2;
				dstc+= fa->dst.pitch>>2;
			}
		} 
		else
		{
			/* RGB mode - just copy src >> dst without change */
			memcpy(dst,src,srcx*h);
		}

		_seedit(strToBin(seed)*mfd->ngSeed);
		_update_line_order(hh);

		/* Shuffle */
		// permute even field

 		for(int x=0; x < hh; x++)
		{
			int o = order[x] >= (hh-buff) ? hh-order[x]-1: order[x]+buff ;
			//o = x >= (hh-buff) ? x-(hh-buff): (x > buff ? x+buff : buff*2-x);
			//o = x == hh-1 ? x : o;
			memcpy(dstbufframe+(srcx*o*2),dst,srcx);
			dst +=dstline;
			dst +=dstline;
		}  

		dst -=dstline*h;

		// permute odd field
		if (mfd->ngHFSeed) _update_line_order(hh);
		
 		for(int x=0; x < hh; x++)
		{
			int o = order[x] >= (hh-buff) ? hh-order[x]-1: order[x]+buff ;
			//o = x >= (hh-buff) ? x-(hh-buff): (x > buff ? x+buff : buff*2-x);
			dst +=dstline;
			memcpy(dstbufframe+(srcx*(o*2)+srcline),dst,srcx);
			dst +=dstline;
		}  

		memcpy(dstbufframe,tagline,srcx);

		dst -=dstline*h;

		memcpy(dst,dstbufframe,srcx*h);

	} 
	else // Decode start
	{
		
		int tag[32];
		char seed[1024]="";

		/* Grab a copy of tagline */
		memcpy(tagline,src,srcx);

		tag[0] = (tagline[0] >= 0 ? 0 : 1);
		sprintf(seed, "%i", tag[0]);

		/* Read tag for Nagravision tag - first 7 bits */
		for (int i=1; i < 7;i++) {
			tag[i] = (tagline[((srcx/256)*4)*i] >= 0 ? 0 : 1);
			sprintf(seed, "%s%i", seed,tag[i]);
		}

		/* Check whether frame is tagged for Nagravision (magic number 25) and check-box is on */
		if(strToBin(seed) == 25) {

			/* Read tag for Nagravision tag - remaining 25 bits */
			for (int i=7; i < 32;i++) {
				tag[i] = (tagline[((destx/256)*4)*i] >= 0 ? 0 : 1);
				sprintf(seed, "%s%i", seed,tag[i]);
			}

						/* Unshuffle */
			// permute even field

			_seedit(strToBin(seed)*mfd->ngSeed);
			_update_line_order(hh);

 			for(int x=0; x < hh; x++)
			{
				int o = order[x] >= (hh-buff) ? hh-order[x]-1: order[x]+buff ;
				//o = x >= (hh-buff) ? x-(hh-buff): (x > buff ? x+buff : buff*2-x);
				//o = x == hh-1 ? x : o;
				memcpy(dstbufframe,src+(srcx*(o*2)),srcx);
				dstbufframe +=dstline;
				dstbufframe +=dstline;
			} 

			 dstbufframe -=dstline*h;

			 if (mfd->ngHFSeed) _update_line_order(hh);

			// permute even field
			 for(int x=0; x < hh; x++)
			{
				int o = order[x] >= (hh-buff) ? hh-order[x]-1: order[x]+buff ;
				//o = x >= (hh-buff) ? x-(hh-buff): (x > buff ? x+buff : buff*2-x);
				dstbufframe +=dstline;
				memcpy(dstbufframe,src+(srcx*(o*2)+srcline),srcx);
				dstbufframe +=dstline;
			}

			dstbufframe -=dstline*2;
			memset(dstbufframe,0x00,srcx*2);
			dstbufframe -=dstline*(h-2);
			
			memcpy(dst,dstbufframe,fa->dst.size);
			
			if (mfd->modePAL) {
				/* Unscramble chroma */
				Pixel32 *dstc= (Pixel32*) dst;

				/* Setup constants for rotation values */
				const double cosa[4] = {cos(-45.0)*10000000,cos(-135.0)*10000000,cos(-225.0)*10000000,cos(-315.0)*10000000};
				const double sina[4] = {sin(-45.0)*10000000,sin(-135.0)*10000000,sin(-225.0)*10000000,sin(-315.0)*10000000};

				/* Seed it */
				_seedit(strToBin(seed)*mfd->ngSeed);
			  
				for (int i=0; i<h; i++)
				{
					int H = (rand() % 3);

					for (int x=0; x<w; x++)
					{
						r= (dstc[x]>>16)&0xff;
					  	g= (dstc[x]>> 8)&0xff;
					  	b= (dstc[x]    )&0xff;

						/* Convert to YUV */
						RGB2YUV (r,g,b,Y,U,V);
										
						/* Rotate */
						int Ut = (int) ((((U-128)*10000000) * cosa[H] + ((V-128)*10000000) * sina[H])/10000000/10000000 + 128);
						int Vt = (int) ((((V-128)*10000000) * cosa[H] - ((U-128)*10000000) * sina[H])/10000000/10000000 + 128);

						dstc[x] = (Ut << 16) | (Y << 8) | (Vt);

					}
					
					/* Move onto next line */
					dstc+= fa->dst.pitch>>2;
				}

				dstc-= (fa->dst.pitch>>2)*h;
				for (int i=0; i<h; i++)
				{
					for (int x=0; x<w; x++)
					{
						int Y=  (dstc[x]>> 8) & 0xff;
						int U= (dstc[x]>>16) & 0xff;
						int V= (dstc[x]    ) & 0xff;
						
						if (i < h-2) {
							int Ua= (dstc[x+(fa->dst.pitch>>2)]>>16) & 0xff;
							int Va= (dstc[x+(fa->dst.pitch>>2)]    ) & 0xff;

							U=(U+Ua)/2;
							V=(V+Va)/2;
						}

						YUV2RGB(Y,U,V,r,g,b);

						float s= 3.0f;
						const int SR= (int)(65536.0f*s);
						const int SG= (int)(65536.0f*s);
						const int SB= (int)(65536.0f*s);
						const int TR= (int)(65536.0f*RW*(1.0f-s));
						const int TG= (int)(65536.0f*GW*(1.0f-s));
						const int TB= (int)(65536.0f*BW*(1.0f-s));

						int t= TR*r+TG*g+TB*b+32768;
						r= (t+SR*r)>>16;
						g= (t+SG*g)>>16;
						b= (t+SB*b)>>16;
						if ((unsigned)r>255) r=(~r>>31)&255; 
						if ((unsigned)g>255) g=(~g>>31)&255; 
						if ((unsigned)b>255) b=(~b>>31)&255;

						dstc[x] = (r << 16) | (g << 8) | (b);
					}
					dstc+= fa->dst.pitch>>2;
				}
				
			}
		}
	}
	return 0;
}

int EndProcNagravisionFilter(FilterActivation *fa, const FilterFunctions *ff)
{
	/* I should add some deinitialisation procedures here to make it more memory friendly */
    return 0;
}

BOOL CALLBACK ecConfigDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {

	/* Pointer to MFD */
    NGFilterData *mfd = (NGFilterData *)GetWindowLong(hdlg, DWL_USER);

	/* Temporary storage for strings */
	char tmp[100];

    switch(msg) {
        case WM_INITDIALOG:
			/* Populate window with current values */

            SetWindowLong(hdlg, DWL_USER, lParam);
            mfd = (NGFilterData *)lParam;
            CheckDlgButton(hdlg, IDR_VCEncrypt, mfd->NagravisionMode?BST_CHECKED:BST_UNCHECKED);
            CheckDlgButton(hdlg, IDR_VCDecrypt, mfd->NagravisionMode?BST_UNCHECKED:BST_CHECKED);
			CheckDlgButton(hdlg, IDC_PAL, mfd->modePAL?BST_CHECKED:BST_UNCHECKED);
			CheckDlgButton(hdlg, IDC_LD, mfd->ngHFSeed?BST_CHECKED:BST_UNCHECKED);
			sprintf_s(tmp,"%i",mfd->ngSeed);
			SetDlgItemText(hdlg, IDC_VCSEED, tmp );

            return TRUE;

        case WM_COMMAND:
            switch(LOWORD(wParam)) {
            case IDOK:

				/* Store configured values */
				mfd->modePAL = !!IsDlgButtonChecked(hdlg, IDC_PAL);
				mfd->NagravisionMode = !!IsDlgButtonChecked(hdlg, IDR_VCEncrypt);
				mfd->ngHFSeed=!!IsDlgButtonChecked(hdlg, IDC_LD);
				GetDlgItemText(hdlg, IDC_VCSEED, tmp,10);
				mfd->ngSeed = atoi(tmp);
                EndDialog(hdlg, 0);
                return TRUE;
            case IDCANCEL:
				/* Exit without saving */
                EndDialog(hdlg, 1);
                return FALSE;
            }
            break;
    }

    return FALSE;
}


struct FilterDefinition filterDef_NagravisionFilter =
{
    NULL, NULL, NULL,				// next, prev, module
	"Nagravision Encoder/Decoder",	// name
    "v1.00a - filter to emulate Nagravision encoder and decoder as used on analogue satellite." ,    // desc
	"http://filmnet.plus",			// maker
    NULL,							// private_data
    sizeof(NGFilterData),			// inst_data_size
    InitProcNagravisionFilter,		// initProc
    NULL,							// deinitProc
    RunProcNagravisionFilter,		// runProc
    NULL,							// paramProc
	ConfigProcNagravisionFilter,		// configProc
    StringProcNagravisionFilter,		// stringProc
    StartProcNagravisionFilter,		// startProc
    EndProcNagravisionFilter,		// endProc
    &script_objec,					// script_obj
    FssProcNagravisionFilter,		// fssProc

};

int InitProcNagravisionFilter(FilterActivation *fa, const FilterFunctions *ff) {
	
	/* Pointer to MFD */
	NGFilterData *mfd = (NGFilterData *)fa->filter_data;

	/* Initialise default values */
	mfd->ngSeed = 9876;
	mfd->NagravisionMode = 1;
	mfd->modePAL = 1;
	srand((mfd->ngSeed+10));
	mfd->ngHFSeed = 1;
	return 0;
}

/* Procedure to update text depending on options selected */
void StringProcNagravisionFilter(const FilterActivation *fa, const FilterFunctions *ff, char *str) {

	/* Pointer to MFD */
	NGFilterData *mfd = (NGFilterData *)fa->filter_data;
	
	/* Array of possible modes */
	const char *modes[2]={
		"decoder",
		"encoder",
    };

	const char *PAL[2]={
		"RGB",
		"PAL",
	};

	const char *onff[2]={
		"OFF",
		"ON",
	};

	sprintf(str, " (%s, %s, seed %i, half-frame seed %s)", modes[mfd->NagravisionMode], PAL[mfd->modePAL], mfd->ngSeed, onff[mfd->ngHFSeed]);
}

int StartProcNagravisionFilter(FilterActivation *fa, const FilterFunctions *ff)
{
	/* Pointer to MFD */
	NGFilterData *mfd = (NGFilterData *)fa->filter_data;

	/* Temporary line storage */
	dsttemp = (char*)malloc(fa->dst.w*sizeof(Pixel32));
	dstbufframe = (char*)malloc((fa->src.w*sizeof(Pixel32)+fa->src.modulo)*(fa->src.h));
	tagline = (char*)malloc(fa->dst.w*sizeof(Pixel32));
	
    return 0;
}

int ConfigProcNagravisionFilter(FilterActivation *fa, const FilterFunctions *ff, HWND hwnd)
{
	/* Open configuration box when filter is first loaded */
	return DialogBoxParam(fa->filter->module->hInstModule, MAKEINTRESOURCE(IDD_VCCONFIG), hwnd, ecConfigDlgProc, (LPARAM)fa->filter_data);
}

/* General routines to register filters */

extern "C" int __declspec(dllexport) __cdecl VirtualdubFilterModuleInit2(FilterModule *fm, const FilterFunctions *ff, int& vdfd_ver, int& vdfd_compat);
extern "C" void __declspec(dllexport) __cdecl VirtualdubFilterModuleDeinit(FilterModule *fm, const FilterFunctions *ff);

int __declspec(dllexport) __cdecl VirtualdubFilterModuleInit2(FilterModule *fm, const FilterFunctions *ff, int& vdfd_ver, int& vdfd_compat)
{
	if (!(fd_NagravisionFilter = ff->addFilter(fm, &filterDef_NagravisionFilter, sizeof(FilterDefinition))))
        return 1;

	vdfd_ver = VIRTUALDUB_FILTERDEF_VERSION;
	vdfd_compat = VIRTUALDUB_FILTERDEF_COMPATIBLE;

    return 0;
}

void __declspec(dllexport) __cdecl VirtualdubFilterModuleDeinit(FilterModule *fm, const FilterFunctions *ff)
{
	ff->removeFilter(fd_NagravisionFilter);
}

/* Conversion table courtesy of Emiliano Ferrari */
void RGB2YUV(int r,int g,int b,int &Y,int &U,int &V)
{
	// input:  r,g,b [0..255]
	// output: Y,U,V [0..255]
	const int Yr= (int)( 0.257*65536.0*255.0/219.0);
	const int Yg= (int)( 0.504*65536.0*255.0/219.0);
	const int Yb= (int)( 0.098*65536.0*255.0/219.0);
	const int Ur= (int)( 0.439*65536.0*255.0/223.0);
	const int Ug= (int)(-0.368*65536.0*255.0/223.0);
	const int Ub= (int)(-0.071*65536.0*255.0/223.0);	
	const int Vr= (int)(-0.148*65536.0*255.0/223.0);
	const int Vg= (int)(-0.291*65536.0*255.0/223.0);
	const int Vb= (int)( 0.439*65536.0*255.0/223.0);

	Y= ((int)(Yr * r + Yg * g + Yb * b+32767)>>16);
	U= ((int)(Ur * r + Ug * g + Ub * b+32767)>>16)+128;
	V= ((int)(Vr * r + Vg * g + Vb * b+32767)>>16)+128;
	if ((unsigned)U>255) U=(~U>>31)&0xff;
	if ((unsigned)V>255) V=(~V>>31)&0xff;
}

/* Conversion table courtesy of Emiliano Ferrari */
void YUV2RGB(int Y,int U,int V,int &r,int &g,int &b)
{
	// input:  Y,U,V [0..255]
	// output: r,g,b [0..255]
	const int YK= (int)(1.164*65536.0*219.0/255.0);
	const int k1= (int)(1.596*65536.0*223.0/255.0);
	const int k2= (int)(0.813*65536.0*223.0/255.0);
	const int k3= (int)(2.018*65536.0*223.0/255.0);
	const int k4= (int)(0.391*65536.0*223.0/255.0);

	Y*= YK;
	U-= 128;
	V-= 128;

	r= (Y+k1*U+32768)>>16;
	g= (Y-k2*U-k4*V+32768)>>16;
	b= (Y+k3*V+32768)>>16;

	if ((unsigned)r>255) r=(~r>>31)&0xff;
	if ((unsigned)g>255) g=(~g>>31)&0xff;
	if ((unsigned)b>255) b=(~b>>31)&0xff;
}

void _seedit(int seed) {
	srand(seed);
}

void _update_line_order(int hh) {

		int S = rand() & 0x7F;
		int R = rand() & 0xFF;
		int buffer = 32;
		int B[32];
		hh --;

		for(int i = 0; i < buffer; i++)
		{	
			B[i] = -buffer + i;
		}
		
		for(int i = 0; i < hh; i++)
		{
			int j = i <= (hh-buffer-1) ? _key_table[(R + (2 * S + 1) * i) & 0xFF] : i - (hh-buffer);
			B[j] = order[B[j] + buffer] = i;
		}

		for (int i=hh; i < hh+1; i++) order[i]=i;
}

int strToBin(const char * str)
{
    int val = 0;

    while (*str != '\0')
        val = 2 * val + (*str++ - '0');
    return val;
}