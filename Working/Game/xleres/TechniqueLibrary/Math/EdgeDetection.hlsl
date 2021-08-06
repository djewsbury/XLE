// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(EDGE_DETECTION_H)
#define EDGE_DETECTION_H

static const float ScharrConstant = 60.f;

static const float ScharrHoriz5x5[5][5] =
{
	{ -1.f / ScharrConstant, -1.f / ScharrConstant,  0.f,  1.f / ScharrConstant,  1.f / ScharrConstant },
	{ -2.f / ScharrConstant, -2.f / ScharrConstant,  0.f,  2.f / ScharrConstant,  2.f / ScharrConstant },
	{ -3.f / ScharrConstant, -6.f / ScharrConstant,  0.f,  6.f / ScharrConstant,  3.f / ScharrConstant },
	{ -2.f / ScharrConstant, -2.f / ScharrConstant,  0.f,  2.f / ScharrConstant,  2.f / ScharrConstant },
	{ -1.f / ScharrConstant, -1.f / ScharrConstant,  0.f,  1.f / ScharrConstant,  1.f / ScharrConstant }
};

static const float ScharrVert5x5[5][5] =
{
	{ -1.f / ScharrConstant, -2.f / ScharrConstant, -3.f / ScharrConstant, -2.f / ScharrConstant, -1.f / ScharrConstant },
	{ -1.f / ScharrConstant, -2.f / ScharrConstant, -6.f / ScharrConstant, -2.f / ScharrConstant, -1.f / ScharrConstant },
	{  0.f / ScharrConstant,  0.f / ScharrConstant,  0.f / ScharrConstant,  0.f / ScharrConstant,  0.f / ScharrConstant },
	{  1.f / ScharrConstant,  2.f / ScharrConstant,  6.f / ScharrConstant,  2.f / ScharrConstant,  1.f / ScharrConstant },
	{  1.f / ScharrConstant,  2.f / ScharrConstant,  3.f / ScharrConstant,  2.f / ScharrConstant,  1.f / ScharrConstant }
};

static const float ScharrConstant3x3 = 32.f;

static const float ScharrHoriz3x3[3][3] =
{
	{  -3.f / ScharrConstant3x3, 0.f,  3.f / ScharrConstant3x3 },
	{ -10.f / ScharrConstant3x3, 0.f, 10.f / ScharrConstant3x3 },
	{  -3.f / ScharrConstant3x3, 0.f,  3.f / ScharrConstant3x3 }
};

static const float ScharrVert3x3[3][3] =
{
	{  -3.f / ScharrConstant3x3, -10.f / ScharrConstant3x3,  -3.f / ScharrConstant3x3 },
	{ 0.f, 0.f, 0.f },
	{   3.f / ScharrConstant3x3,  10.f / ScharrConstant3x3,   3.f / ScharrConstant3x3 }
};

static const float Scharr3DConstant3x3 = 1024.f;

static const float Scharr3DX3x3[3][3][3]=
{
	{
		{    9.0 / Scharr3DConstant3x3,   30.0 / Scharr3DConstant3x3,    9.0 / Scharr3DConstant3x3 },
		{   30.0 / Scharr3DConstant3x3,  100.0 / Scharr3DConstant3x3,   30.0 / Scharr3DConstant3x3 },
		{    9.0 / Scharr3DConstant3x3,   30.0 / Scharr3DConstant3x3,    9.0 / Scharr3DConstant3x3 },
	},
	{
		{    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3 },
		{    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3 },
		{    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3 },
	},
	{
		{   -9.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3,   -9.0 / Scharr3DConstant3x3 },
		{  -30.0 / Scharr3DConstant3x3, -100.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3 },
		{   -9.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3,   -9.0 / Scharr3DConstant3x3 },
	},
};
static const float Scharr3DY3x3[3][3][3]=
{
	{
		{    9.0 / Scharr3DConstant3x3,   30.0 / Scharr3DConstant3x3,    9.0 / Scharr3DConstant3x3 },
		{    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3 },
		{   -9.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3,   -9.0 / Scharr3DConstant3x3 },
	},
	{
		{   30.0 / Scharr3DConstant3x3,  100.0 / Scharr3DConstant3x3,   30.0 / Scharr3DConstant3x3 },
		{    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3 },
		{  -30.0 / Scharr3DConstant3x3, -100.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3 },
	},
	{
		{    9.0 / Scharr3DConstant3x3,   30.0 / Scharr3DConstant3x3,    9.0 / Scharr3DConstant3x3 },
		{    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3 },
		{   -9.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3,   -9.0 / Scharr3DConstant3x3 },
	},
};
static const float Scharr3DZ3x3[3][3][3]=
{
	{
		{    9.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,   -9.0 / Scharr3DConstant3x3 },
		{   30.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3 },
		{    9.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,   -9.0 / Scharr3DConstant3x3 },
	},
	{
		{   30.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3 },
		{  100.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3, -100.0 / Scharr3DConstant3x3 },
		{   30.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3 },
	},
	{
		{    9.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,   -9.0 / Scharr3DConstant3x3 },
		{   30.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,  -30.0 / Scharr3DConstant3x3 },
		{    9.0 / Scharr3DConstant3x3,    0.0 / Scharr3DConstant3x3,   -9.0 / Scharr3DConstant3x3 },
	},
};

#endif
