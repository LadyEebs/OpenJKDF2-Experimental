#ifndef _RDSHADER_H
#define _RDSHADER_H

#include "types.h"
#include "globals.h"

#ifdef RENDER_DROID2

typedef enum
{
	RD_SHADER_U8,
	RD_SHADER_S8,
	RD_SHADER_F16,
	RD_SHADER_F32,

	RD_SHADER_ENC_COUNT
} rdShader_RegEncoding;
static_assert(RD_SHADER_ENC_COUNT <= 4, "RD_SHADER_ENC_COUNT must not exceed 4");

typedef enum
{
	// 6 general purpose registers
	RD_SHADER_R0,
	RD_SHADER_R1,
	RD_SHADER_R2,
	RD_SHADER_R3,
	RD_SHADER_R4,
	RD_SHADER_R5,

	RD_SHADER_REG_COUNT
} rdShader_TempRegisters;
static_assert(RD_SHADER_REG_COUNT <= 8, "RD_SHADER_REG_COUNT must not exceed 8.");

typedef enum
{
	// 2 vertex color registers
	RD_SHADER_V0,
	RD_SHADER_V1,

	RD_SHADER_REG_V_COUNT
} rdShader_VertexRegisters;
static_assert(RD_SHADER_REG_V_COUNT <= 2, "RD_SHADER_REG_V_COUNT must not exceed 2.");

typedef enum
{
	RD_SHADER_T0,
	RD_SHADER_T1,
	RD_SHADER_T2,
	RD_SHADER_T3,
	RD_SHADER_T4,

	RD_SHADER_REG_T_COUNT
} rdShader_TextureRegisters;
static_assert(RD_SHADER_REG_T_COUNT <= 5, "RD_SHADER_REG_T_COUNT must not exceed 5.");

typedef enum
{
	RD_SHADER_C0,
	RD_SHADER_C1,
	RD_SHADER_C2,
	RD_SHADER_C3,
	RD_SHADER_C4,
	RD_SHADER_C5,
	RD_SHADER_C6,
	RD_SHADER_C7,

	RD_SHADER_REG_CONST_COUNT
} rdShader_ConstantRegisters;
static_assert(RD_SHADER_REG_CONST_COUNT <= 8, "RD_SHADER_REG_CONST_COUNT must not exceed 8.");

typedef enum
{
	RD_SHADER_TIME = 0,
	RD_SHADER_XY,
	RD_SHADER_Z,
	RD_SHADER_POS,
	RD_SHADER_VDIR,
	RD_SHADER_NORM,
	RD_SHADER_WPOS,
	RD_SHADER_WNORM,
	RD_SHADER_UV,
	RD_SHADER_AR,
	RD_SHADER_IDENTITY,

	// material registers
	RD_SHADER_MAT_FILL = 32,
	RD_SHADER_MAT_ALBEDO,
	RD_SHADER_MAT_EMISSIVE,
	RD_SHADER_MAT_SPECULAR,
	RD_SHADER_MAT_ROUGHNESS,
	RD_SHADER_MAT_DISPLACEMENT,

	RD_SHADER_SYS_MAX
} rdShader_SystemValues;
static_assert(RD_SHADER_SYS_MAX < 256, "SHADER_SYS_MAX must not exceed 255");

typedef enum
{
	RD_SHADER_GPR,		// register
	RD_SHADER_CLR,		// color
	RD_SHADER_CON,		// constant
	RD_SHADER_TEX,		// texcoord
	RD_SHADER_IMM4x8,   // 32 bit snorm8 value
	RD_SHADER_IMM8,		// 8 bit immediate value
	RD_SHADER_IMM16,	// 16 bit immediate value
	RD_SHADER_IMM32,	// 32 bit immediate value
	RD_SHADER_REG_TYPE_COUNT
} rdShader_RegisterTypes;
static_assert(RD_SHADER_REG_TYPE_COUNT <= 8, "RD_SHADER_REG_TYPE_COUNT must not exceed 8.");

typedef enum
{
	RD_SHADER_NEGATE = 1,	// negate: -x
	RD_SHADER_INVERT,		// invert: 1 - x
	RD_SHADER_RCP,			// rcp: 1 / x

	RD_SHADER_UNARY_COUNT
} rdShader_UnaryModifiers;
static_assert(RD_SHADER_UNARY_COUNT <= 4, "RD_SHADER_UNARY_COUNT must not exceed 4.");

typedef enum
{
	RD_SHADER_X2 = 1,	// x * 2
	RD_SHADER_X4,		// x * 4
	RD_SHADER_D2,		// x / 2
	RD_SHADER_D4,		// x / 4

	RD_SHADER_BIAS,		// x - 0.5
	RD_SHADER_EXPAND,   // x * 2 - 1

	RD_SHADER_MUL_COUNT
} rdShader_Multipliers;
static_assert(RD_SHADER_MUL_COUNT <= 8, "RD_SHADER_MUL_COUNT must not exceed 8.");

typedef enum
{
	RD_SHADER_LUM = 1,	// dot(xyz, lum)
	RD_SHADER_SUM,		// x+y+z
	RD_SHADER_AVG,		// (x+y+z)/3
	RD_SHADER_MIN,		// min(x,y,z)
	RD_SHADER_MAX,		// max(x,y,z)
	RD_SHADER_MAG,		// length(xyz)

	RD_SHADER_SRC_RED_COUNT
} rdShader_SrcReductions;
static_assert(RD_SHADER_SRC_RED_COUNT <= 8, "RD_SHADER_SRC_RED_COUNT must not exceed 8.");

typedef enum
{
	RD_WRITE_RGBA	= 0b1111, // 0xF
	RD_WRITE_RGB	= 0b0111, // 0x7
	RD_WRITE_RED	= 0b0001, // 0x1
	RD_WRITE_GREEN	= 0b0010, // 0x2
	RD_WRITE_BLUE	= 0b0100, // 0x4
	RD_WRITE_ALPHA	= 0b1000, // 0x8
} rdShader_WriteMask;

typedef enum
{
	// only enum common swizzles since every combo is a lot
	RD_SWIZZLE_XYZW = 0xe4
} rdShader_Swizzles;

typedef enum
{
	RD_SHADER_0_OPERANDS = (0 << 5),
	RD_SHADER_1_OPERANDS = (1 << 5),
	RD_SHADER_2_OPERANDS = (2 << 5),
	RD_SHADER_3_OPERANDS = (3 << 5),
} rdShader_OperandMasks;

// operand count is baked into the op code
// storage in the bytecode is (opcode & 0x1F), the operands are used for parsing
// note: if ids change, update the default shader in the backend
#define RD_SHADER_OPCODE(name, value) \
    RD_SHADER_OP_##name = value,

typedef enum {
#include "rdShader_OpCodes.h"
	RD_SHADER_MAX_OPS
} rdShader_OpCodes;
static_assert((RD_SHADER_MAX_OPS & 0x1F) <= 32, "RD_SHADER_MAX_OPS must not exceed 32 op codes.");

#undef RD_SHADER_OPCODE

rdShader* rdShader_New(char *fpath);
int rdShader_NewEntry(rdShader* shader, char* path);
void rdShader_Free(rdShader* shader);
void rdShader_FreeEntry(rdShader* shader);
int rdShader_LoadEntry(char* fpath, rdShader* shader);

uint32_t rdShader_AssembleSrc(uint8_t idx, uint8_t type, uint8_t fmt, uint8_t addr,
	uint8_t abs, uint8_t swizzle, uint8_t negate_or_invert, uint8_t scale_bias, uint8_t reduction);

uint32_t rdShader_AssembleOpAndDst(uint8_t opcode, uint8_t fmt, uint8_t addr, uint8_t swizzle,
	uint8_t multiplier, uint8_t write_mask, uint8_t precise, uint8_t abs, uint8_t neg, uint8_t clamp);

#endif

#endif // _RDSHADER_H
