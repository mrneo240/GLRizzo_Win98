#if 1
/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// vid_null.c -- null video driver to aid porting efforts

#include "quakedef.h"
#include <kos.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>
#include <GL/glkos.h>

viddef_t vid; // global video state

unsigned short d_8to16table[256];
unsigned d_8to24table[256];
unsigned char d_15to8table[1 << 15];

/* cvar_t		vid_mode = {"vid_mode","5",false}; */
cvar_t vid_redrawfull = {"vid_redrawfull", "0", false};
cvar_t vid_waitforrefresh = {"vid_waitforrefresh", "0", true};

int texture_mode = GL_LINEAR;

int texture_extension_number = 1;

float gldepthmin, gldepthmax;
float vid_gamma;

cvar_t gl_ztrick = {"gl_ztrick", "0"};

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;
qboolean is8bit = false;

qboolean isPermedia = false;
qboolean gl_mtexable = false;
qboolean scr_skipupdate;
static qboolean fullsbardraw = false;

#define PACK_RGB565(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

int basewidth, baseheight, screenheight, dcvidmode = 5;

int scr_width, scr_height;

void CheckMultiTextureExtensions(void)
{
	gl_mtexable = true;
}

void VID_Shutdown(void)
{
}

/*
===============
GL_Init
===============
*/
void GL_Init(void)
{
	gl_vendor = glGetString(GL_VENDOR);
	Sys_Printf("GL_VENDOR: %s\n", gl_vendor);
	gl_renderer = glGetString(GL_RENDERER);
	Sys_Printf("GL_RENDERER: %s\n", gl_renderer);

	gl_version = glGetString(GL_VERSION);
	Sys_Printf("GL_VERSION: %s\n", gl_version);
	gl_extensions = glGetString(GL_EXTENSIONS);
	Sys_Printf("GL_EXTENSIONS: %s\n", gl_extensions);

	Sys_Printf ("%s %s\n", gl_renderer, gl_version);

	CheckMultiTextureExtensions();

	glClearColor(1, 0, 0, 0);
	glCullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);

	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.666);

	//	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_FLAT);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//@TODO: DONT
	//glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/*
=================
GL_BeginRendering

=================
*/
void GL_BeginRendering(int *x, int *y, int *width, int *height)
{
	extern cvar_t gl_clear;

	*x = *y = 0;
	*width = scr_width;
	*height = scr_height;
}

void GL_EndRendering(void)
{
	if (!scr_skipupdate || block_drawing)
		glKosSwapBuffers();

	if (fullsbardraw)
		Sbar_Changed();
}

void VID_ShiftPalette(unsigned char *p)
{

}

void	VID_SetPalette (unsigned char *palette)
{
		byte	*pal;
	unsigned r,g,b;
	unsigned v;
	int     r1,g1,b1;
	int		j,k,l;
	unsigned short i;
	unsigned	*table;
	//
// 8 8 8 encoding
//
	pal = palette;
	table = d_8to24table;
	for (i=0 ; i<256 ; i++)
	{
		r = pal[0];
		g = pal[1];
		b = pal[2];
		pal += 3;
		
//		v = (255<<24) + (r<<16) + (g<<8) + (b<<0);
//		v = (255<<0) + (r<<8) + (g<<16) + (b<<24);
		v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
		*table++ = v;
	}
	d_8to24table[255] &= 0xffffff;	// 255 is transparent

	// JACK: 3D distance calcs - k is last closest, l is the distance.
	// FIXME: Precalculate this and cache to disk.
	for (i=0; i < (1<<15); i++) {
		/* Maps
			000000000000000
			000000000011111 = Red  = 0x1F
			000001111100000 = Blue = 0x03E0
			111110000000000 = Grn  = 0x7C00
		*/
		r = ((i & 0x1F) << 3)+4;
		g = ((i & 0x03E0) >> 2)+4;
		b = ((i & 0x7C00) >> 7)+4;
		pal = (unsigned char *)d_8to24table;
		for (v=0,k=0,l=10000*10000; v<256; v++,pal+=4) {
			r1 = r-pal[0];
			g1 = g-pal[1];
			b1 = b-pal[2];
			j = (r1*r1)+(g1*g1)+(b1*b1);
			if (j<l) {
				k=v;
				l=j;
			}
		}
		d_15to8table[i]=k;
	}
}

qboolean VID_Is8bit(void)
{
	return is8bit;
}

void VID_Init8bitPalette(void)
{
	// Check for 8bit Extensions and initialize them.
	int i;
	char thePalette[256*3];
	char *oldPalette, *newPalette;

	Sys_Printf("8-bit GL extensions enabled.\n");
    glEnable( GL_SHARED_TEXTURE_PALETTE_EXT );
	oldPalette = (char *) d_8to24table;
	newPalette = thePalette;
	for (i=0;i<256;i++) {
		*newPalette++ = *oldPalette++;
		*newPalette++ = *oldPalette++;
		*newPalette++ = *oldPalette++;
		oldPalette++;
	}
	glColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB8, 256, GL_RGB, GL_UNSIGNED_BYTE, (void *) thePalette);
	is8bit = true;
}

void SelectVidMode()
{
	basewidth = 640;
	baseheight = 480;
	screenheight = 480;
}
// Manoel Kasimier - end

void InitGL()
{
	glKosInit();
	GL_Init();
}

void VID_Init(unsigned char *palette)
{
	InitGL();
	SelectVidMode(); // Manoel Kasimier

	vid.width = vid.conwidth = basewidth;
	vid.height = vid.conheight = baseheight;
	vid.maxwarpwidth = vid.width;   // Manoel Kasimier - hi-res waterwarp & buffered video
	vid.maxwarpheight = vid.height; // Manoel Kasimier - hi-res waterwarp & buffered video
	vid.aspect = 1.0;
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
	vid.rowbytes = vid.conrowbytes = basewidth;
	
	VID_Init8bitPalette();
	// Set up the default palette
	VID_SetPalette(palette);
}

// Adapted from nxDoom
void VID_Update(vrect_t *rects)
{
}

#endif