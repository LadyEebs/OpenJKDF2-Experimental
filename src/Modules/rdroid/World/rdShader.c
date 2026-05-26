#include "rdShader.h"

#ifdef RENDER_DROID2

#include "Win95/std.h"
#include "General/stdMath.h"
#include "General/stdString.h"
#include "General/stdFileUtil.h"
#include "General/stdFnames.h"
#include "General/stdHashTable.h"
#include "Engine/rdroid.h"

#include "Modules/std/std3D.h"

#define RD_SHADER_OPCODE(name, value) \
    { #name, value },

typedef struct rdShader_opcodeNames
{
	const char* name;
	uint8_t     token;
} rdShader_OpCode;

static const rdShader_OpCode rdShader_opcodeNames[] =
{
#include "rdShader_OpCodes.h"
};

#undef RD_SHADER_OPCODE

typedef struct
{
	uint8_t   idx;
	uint8_t   type;
	uint8_t   fmt;
	uint8_t   address;
	uint8_t   swizzle;
	uint8_t   mask;
	uint8_t   abs;
	uint8_t   unary;
	rdVector4 immediate;
} rdShader_Register;

typedef struct
{
	uint8_t           mult;
	uint8_t           reduction;
	rdShader_Register reg;
} rdShader_SrcOperand;

typedef struct
{
	uint8_t           multiplier;
	rdShader_Register reg;
} rdShader_DestOperand;

typedef struct
{
	uint8_t              opcode;
	uint8_t              precise;
	uint8_t              clamp;
	uint8_t              srcCount;
	rdShader_DestOperand dest;
	rdShader_SrcOperand  src[3];
} rdShader_Instruction;

// statically defined aliases (internal values)
typedef struct rdShader_StaticAlias
{
	const char* name;
	const char* reg;
} rdShader_StaticAlias;

// dynamically defined alises using the alias op
typedef struct rdShader_Alias
{
	char                   name[16];
	char                   reg[16];
	struct rdShader_Alias* next;
} rdShader_Alias;

typedef int (*rdShader_MacroFn)(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut);
typedef struct rdShader_Macro
{
	const char*      name;
	uint8_t          srcCount;
	rdShader_MacroFn expand;
} rdShader_Macro;

// label defined in the shader source (name:)
#define RD_SHADER_MAX_LABELS 16
#define RD_SHADER_MAX_FIXUPS 16

typedef struct
{
	char    name[32];
	uint8_t pc;
} rdShader_Label;

// forward-reference fixup: patch the call target once all labels are resolved
typedef struct
{
	char     name[32];
	uint16_t instrIdx;
} rdShader_Fixup;

typedef struct
{
	stdHashTable*   aliasHash;
	rdShader_Alias* firstAlias;
	rdShader*       shader;
	rdShader_Label  labels[RD_SHADER_MAX_LABELS];
	int             labelCount;
	rdShader_Fixup  fixups[RD_SHADER_MAX_FIXUPS];
	int             fixupCount;
	uint8_t         currentCallDepth;
	uint8_t         maxCallDepth;
} rdShader_Assembler;

rdShader_Assembler* rdShader_pCurrentAssembler = NULL;

// sub dst, a, b  ->  add dst, a, -b
static int rdShader_MacroExpandSUB(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 1)
		return 0;

	inst->opcode = RD_SHADER_OP_ADD;
	inst->src[1].reg.unary = RD_SHADER_NEGATE;
	return rdShader_AssembleInstruction(&out[0], inst) ? 1 : 0;
}

// neg dst, a  ->  mov dst, -a
static int rdShader_MacroExpandNEG(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 1)
		return 0;

	inst->opcode = RD_SHADER_OP_MOV;
	inst->src[0].reg.unary = RD_SHADER_NEGATE;
	return rdShader_AssembleInstruction(&out[0], inst) ? 1 : 0;
}

// sat dst, a  ->  mov dst, a  [clamp]
static int rdShader_MacroExpandSAT(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 1)
		return 0;

	inst->opcode = RD_SHADER_OP_MOV;
	inst->clamp = 1;
	return rdShader_AssembleInstruction(&out[0], inst) ? 1 : 0;
}

// abs dst, a  ->  mov dst, abs(a)
static int rdShader_MacroExpandABS(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 1)
		return 0;
	inst->opcode = RD_SHADER_OP_MOV;
	inst->src[0].reg.abs = 1;
	return rdShader_AssembleInstruction(&out[0], inst) ? 1 : 0;
}

// lerp dst, a, b, t -> sub tmp, b, a   (add tmp, b, -a)
//                      mad dst, tmp, t, a
static int rdShader_MacroExpandLERP(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 2)
		return 0;

	// find a free temp register for the intermediate result
	uint8_t tmpReg = rdShader_pCurrentAssembler->shader->regcount + 1;

	// instruction 0: add tmp, src[1], -src[0]  (b - a)
	rdShader_Instruction sub;
	memset(&sub, 0, sizeof(rdShader_Instruction));
	sub.opcode = RD_SHADER_OP_ADD;
	sub.srcCount = 2;
	sub.dest.reg.type = RD_SHADER_GPR;
	sub.dest.reg.address = tmpReg;
	sub.dest.reg.swizzle = RD_SWIZZLE_XYZW;
	sub.dest.reg.mask = RD_WRITE_RGBA;
	sub.src[0] = inst->src[1];            // b
	sub.src[1] = inst->src[0];            // a
	sub.src[1].reg.unary = RD_SHADER_NEGATE; // -a

	if (!rdShader_AssembleInstruction(&out[0], &sub))
		return 0;

	// instruction 1: mad dst, tmp, t, a
	rdShader_Instruction mad;
	memset(&mad, 0, sizeof(rdShader_Instruction));
	mad.opcode = RD_SHADER_OP_MAD;
	mad.srcCount = 3;
	mad.dest = inst->dest;
	mad.src[0].reg.type = RD_SHADER_GPR;
	mad.src[0].reg.address = tmpReg;
	mad.src[0].reg.swizzle = RD_SWIZZLE_XYZW;
	mad.src[0].reg.mask = RD_WRITE_RGBA;
	mad.src[1] = inst->src[2]; // t
	mad.src[2] = inst->src[0]; // a

	if (!rdShader_AssembleInstruction(&out[1], &mad))
		return 0;

	// consume the temp register
	rdShader_pCurrentAssembler->shader->regcount = tmpReg;
	return 2;
}

// sqrt dst, a  ->  rsqrt tmp, a
//                  rcp   dst, tmp
static int rdShader_MacroExpandSQRT(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 2)
		return 0;

	uint8_t tmpReg = rdShader_pCurrentAssembler->shader->regcount + 1;

	rdShader_Instruction rsqrt;
	memset(&rsqrt, 0, sizeof(rdShader_Instruction));
	rsqrt.opcode = RD_SHADER_OP_RSQRT;
	rsqrt.srcCount = 1;
	rsqrt.dest.reg.type = RD_SHADER_GPR;
	rsqrt.dest.reg.address = tmpReg;
	rsqrt.dest.reg.swizzle = RD_SWIZZLE_XYZW;
	rsqrt.dest.reg.mask = RD_WRITE_RGBA;
	rsqrt.src[0] = inst->src[0];

	if (!rdShader_AssembleInstruction(&out[0], &rsqrt))
		return 0;

	rdShader_Instruction rcp;
	memset(&rcp, 0, sizeof(rdShader_Instruction));
	rcp.opcode = RD_SHADER_OP_RCP;
	rcp.srcCount = 1;
	rcp.dest = inst->dest;
	rcp.src[0].reg.type = RD_SHADER_GPR;
	rcp.src[0].reg.address = tmpReg;
	rcp.src[0].reg.swizzle = RD_SWIZZLE_XYZW;
	rcp.src[0].reg.mask = RD_WRITE_RGBA;

	if (!rdShader_AssembleInstruction(&out[1], &rcp))
		return 0;

	rdShader_pCurrentAssembler->shader->regcount = tmpReg;
	return 2;
}

// clamp dst, a, lo, hi  ->  max tmp, a,   lo
//                           min dst, tmp, hi
static int rdShader_MacroExpandCLAMP(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 2)
		return 0;

	uint8_t tmpReg = rdShader_pCurrentAssembler->shader->regcount + 1;

	rdShader_Instruction maxInst;
	memset(&maxInst, 0, sizeof(rdShader_Instruction));
	maxInst.opcode = RD_SHADER_OP_MAX;
	maxInst.srcCount = 2;
	maxInst.dest.reg.type = RD_SHADER_GPR;
	maxInst.dest.reg.address = tmpReg;
	maxInst.dest.reg.swizzle = RD_SWIZZLE_XYZW;
	maxInst.dest.reg.mask = RD_WRITE_RGBA;
	maxInst.src[0] = inst->src[0]; // a
	maxInst.src[1] = inst->src[1]; // lo

	if (!rdShader_AssembleInstruction(&out[0], &maxInst))
		return 0;

	rdShader_Instruction minInst;
	memset(&minInst, 0, sizeof(rdShader_Instruction));
	minInst.opcode = RD_SHADER_OP_MIN;
	minInst.srcCount = 2;
	minInst.dest = inst->dest;
	minInst.src[0].reg.type = RD_SHADER_GPR;
	minInst.src[0].reg.address = tmpReg;
	minInst.src[0].reg.swizzle = RD_SWIZZLE_XYZW;
	minInst.src[0].reg.mask = RD_WRITE_RGBA;
	minInst.src[1] = inst->src[2]; // hi

	if (!rdShader_AssembleInstruction(&out[1], &minInst))
		return 0;

	rdShader_pCurrentAssembler->shader->regcount = tmpReg;
	return 2;
}

// pow dst, a, b  ->  log2 tmp, a
//                    mul  tmp, tmp, b
//                    exp2 dst, tmp
static int rdShader_MacroExpandPOW(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 3)
		return 0;

	uint8_t tmpReg = rdShader_pCurrentAssembler->shader->regcount + 1;

	// instruction 0: log2 tmp, a
	rdShader_Instruction log2inst;
	memset(&log2inst, 0, sizeof(rdShader_Instruction));
	log2inst.opcode = RD_SHADER_OP_LOG2;
	log2inst.srcCount = 1;
	log2inst.dest.reg.type = RD_SHADER_GPR;
	log2inst.dest.reg.address = tmpReg;
	log2inst.dest.reg.swizzle = RD_SWIZZLE_XYZW;
	log2inst.dest.reg.mask = RD_WRITE_RGBA;
	log2inst.src[0] = inst->src[0]; // a

	if (!rdShader_AssembleInstruction(&out[0], &log2inst))
		return 0;

	// instruction 1: MUL tmp, tmp, b
	rdShader_Instruction mul;
	memset(&mul, 0, sizeof(rdShader_Instruction));
	mul.opcode = RD_SHADER_OP_MUL;
	mul.srcCount = 2;
	mul.dest.reg.type = RD_SHADER_GPR;
	mul.dest.reg.address = tmpReg;
	mul.dest.reg.swizzle = RD_SWIZZLE_XYZW;
	mul.dest.reg.mask = RD_WRITE_RGBA;
	mul.src[0].reg.type = RD_SHADER_GPR;
	mul.src[0].reg.address = tmpReg;
	mul.src[0].reg.swizzle = RD_SWIZZLE_XYZW;
	mul.src[0].reg.mask = RD_WRITE_RGBA;
	mul.src[1] = inst->src[1]; // b

	if (!rdShader_AssembleInstruction(&out[1], &mul))
		return 0;

	// instruction 2: exp2 dst, tmp
	rdShader_Instruction exp2inst;
	memset(&exp2inst, 0, sizeof(rdShader_Instruction));
	exp2inst.opcode = RD_SHADER_OP_EXP2;
	exp2inst.srcCount = 1;
	exp2inst.dest = inst->dest;
	exp2inst.src[0].reg.type = RD_SHADER_GPR;
	exp2inst.src[0].reg.address = tmpReg;
	exp2inst.src[0].reg.swizzle = RD_SWIZZLE_XYZW;
	exp2inst.src[0].reg.mask = RD_WRITE_RGBA;

	if (!rdShader_AssembleInstruction(&out[2], &exp2inst))
		return 0;

	rdShader_pCurrentAssembler->shader->regcount = tmpReg;
	return 3;
}

// crs dst, a, b  ->  mul tmp, a.yzxw, b.zxyw
//                    mad dst, a.zxyw, -b.yzxw, tmp
// 
// expands to:  dst.x = a.y*b.z - a.z*b.y
//              dst.y = a.z*b.x - a.x*b.z
//              dst.z = a.x*b.y - a.y*b.x
static int rdShader_MacroExpandCROSS(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 2)
		return 0;

	// yzxw: (y = 1) | (z = 2) << 2 | (x = 0) << 4 | (w = 3) << 6
	const uint8_t swizzle_yzxw = (1) | (2 << 2) | (0 << 4) | (3 << 6);
	// zxyw: (z = 2) | (x = 0) << 2 | (y = 1) << 4 | (w = 3) << 6
	const uint8_t swizzle_zxyw = (2) | (0 << 2) | (1 << 4) | (3 << 6);

	uint8_t tmpReg = rdShader_pCurrentAssembler->shader->regcount + 1;

	// instruction 0: mul tmp, a.yzxw, b.zxyw
	rdShader_Instruction mul;
	memset(&mul, 0, sizeof(rdShader_Instruction));
	mul.opcode = RD_SHADER_OP_MUL;
	mul.srcCount = 2;
	mul.dest.reg.type = RD_SHADER_GPR;
	mul.dest.reg.address = tmpReg;
	mul.dest.reg.swizzle = RD_SWIZZLE_XYZW;
	mul.dest.reg.mask = RD_WRITE_RGBA;
	mul.src[0] = inst->src[0];
	mul.src[0].reg.swizzle = swizzle_yzxw; // a.yzxw
	mul.src[1] = inst->src[1];
	mul.src[1].reg.swizzle = swizzle_zxyw; // b.zxyw

	if (!rdShader_AssembleInstruction(&out[0], &mul))
		return 0;

	// instruction 1: mad dst, a.zxyw, -b.yzxw, tmp
	rdShader_Instruction mad;
	memset(&mad, 0, sizeof(rdShader_Instruction));
	mad.opcode = RD_SHADER_OP_MAD;
	mad.srcCount = 3;
	mad.dest = inst->dest;
	mad.src[0] = inst->src[0];
	mad.src[0].reg.swizzle = swizzle_zxyw;   // a.zxyw
	mad.src[1] = inst->src[1];
	mad.src[1].reg.swizzle = swizzle_yzxw;   // b.yzxw
	mad.src[1].reg.unary = RD_SHADER_NEGATE; // -b.yzxw
	mad.src[2].reg.type = RD_SHADER_GPR;
	mad.src[2].reg.address = tmpReg;
	mad.src[2].reg.swizzle = RD_SWIZZLE_XYZW;
	mad.src[2].reg.mask = RD_WRITE_RGBA;

	if (!rdShader_AssembleInstruction(&out[1], &mad))
		return 0;

	rdShader_pCurrentAssembler->shader->regcount = tmpReg;
	return 2;
}

// nrm dst, a  ->  dp3   tmp, a, a       (|a|^2)
//                 rsqrt tmp, tmp         (1/|a|)
//                 mul   dst, a, tmp      (a / |a|)
static int rdShader_MacroExpandNRM(rdShader_Instruction* inst, rdShaderInstr* out, int maxOut)
{
	if (maxOut < 3)
		return 0;

	uint8_t tmpReg = rdShader_pCurrentAssembler->shader->regcount + 1;

	// instruction 0: dp3 tmp, a, a
	rdShader_Instruction dp3;
	memset(&dp3, 0, sizeof(rdShader_Instruction));
	dp3.opcode = RD_SHADER_OP_DP3;
	dp3.srcCount = 2;
	dp3.dest.reg.type = RD_SHADER_GPR;
	dp3.dest.reg.address = tmpReg;
	dp3.dest.reg.swizzle = RD_SWIZZLE_XYZW;
	dp3.dest.reg.mask = RD_WRITE_RGBA;
	dp3.src[0] = inst->src[0]; // a
	dp3.src[1] = inst->src[0]; // a

	if (!rdShader_AssembleInstruction(&out[0], &dp3))
		return 0;

	// instruction 1: rsqrt tmp, tmp
	rdShader_Instruction rsqrt;
	memset(&rsqrt, 0, sizeof(rdShader_Instruction));
	rsqrt.opcode = RD_SHADER_OP_RSQRT;
	rsqrt.srcCount = 1;
	rsqrt.dest.reg.type = RD_SHADER_GPR;
	rsqrt.dest.reg.address = tmpReg;
	rsqrt.dest.reg.swizzle = RD_SWIZZLE_XYZW;
	rsqrt.dest.reg.mask = RD_WRITE_RGBA;
	rsqrt.src[0].reg.type = RD_SHADER_GPR;
	rsqrt.src[0].reg.address = tmpReg;
	rsqrt.src[0].reg.swizzle = RD_SWIZZLE_XYZW;
	rsqrt.src[0].reg.mask = RD_WRITE_RGBA;

	if (!rdShader_AssembleInstruction(&out[1], &rsqrt))
		return 0;

	// instruction 2: mul dst, a, tmp
	rdShader_Instruction mul;
	memset(&mul, 0, sizeof(rdShader_Instruction));
	mul.opcode = RD_SHADER_OP_MUL;
	mul.srcCount = 2;
	mul.dest = inst->dest;
	mul.src[0] = inst->src[0]; // a
	mul.src[1].reg.type = RD_SHADER_GPR;
	mul.src[1].reg.address = tmpReg;
	mul.src[1].reg.swizzle = RD_SWIZZLE_XYZW;
	mul.src[1].reg.mask = RD_WRITE_RGBA;

	if (!rdShader_AssembleInstruction(&out[2], &mul))
		return 0;

	rdShader_pCurrentAssembler->shader->regcount = tmpReg;
	return 3;
}

static const rdShader_Macro rdShader_macros[] =
{
	{ "sub",   2, rdShader_MacroExpandSUB   },
	{ "neg",   1, rdShader_MacroExpandNEG   },
	{ "sat",   1, rdShader_MacroExpandSAT   },
	{ "abs",   1, rdShader_MacroExpandABS   },
	{ "lrp",   3, rdShader_MacroExpandLERP  },
	{ "sqrt",  1, rdShader_MacroExpandSQRT  },
	{ "clamp", 3, rdShader_MacroExpandCLAMP },
	{ "pow",   2, rdShader_MacroExpandPOW   },
	{ "crs",   2, rdShader_MacroExpandCROSS },
	{ "nrm",   1, rdShader_MacroExpandNRM   },
};

static const rdShader_Macro* rdShader_FindMacro(const char* name)
{
	for (int i = 0; i < ARRAY_SIZE(rdShader_macros); ++i)
	{
		if (stricmp(name, rdShader_macros[i].name) == 0)
			return &rdShader_macros[i];
	}
	return NULL;
}

static uint8_t rdShader_ParseOpCode(const char* name)
{
	char* swizzle = _strchr(name, '.');
	if (swizzle)
		*swizzle = '\0';

	for (uint8_t i = 0; i < (RD_SHADER_MAX_OPS & 0x1F); ++i)
	{
		if (stricmp(name, rdShader_opcodeNames[i].name) == 0)
		{
			if (swizzle)
				*swizzle = '.';
			return rdShader_opcodeNames[i].token;
		}
	}

	if (swizzle)
		*swizzle = '.';

	return RD_SHADER_OP_NOP;
}

static uint8_t rdShader_OperandCount(uint8_t opcode)
{
	return (opcode >> 5) & 0x3;
}

static uint8_t rdShader_ParseSrcRegisterType(char c)
{
	switch (c)
	{
	case 'r': return RD_SHADER_GPR;
	case 't': return RD_SHADER_TEX;
	case 'v': return RD_SHADER_CLR;
	case 'c': return RD_SHADER_CON;
	default:  return 0;
	}
}

static uint8_t rdShader_ParseMultiplier(const char* token)
{
	if (strnicmp(token, "mul:2", 5) == 0) return RD_SHADER_X2;
	if (strnicmp(token, "mul:4", 5) == 0) return RD_SHADER_X4;
	if (strnicmp(token, "div:2", 5) == 0) return RD_SHADER_D2;
	if (strnicmp(token, "div:4", 5) == 0) return RD_SHADER_D4;

	if (strncmp(token, "bias", 4) == 0) return RD_SHADER_BIAS;
	if (strncmp(token, "expand", 6) == 0) return RD_SHADER_EXPAND;

	return 0;
}

static uint8_t rdShader_ParseFormat(const char* token)
{
	if (strnicmp(token, "fmt:ubyte4", 10) == 0) return RD_SHADER_U8;
	if (strnicmp(token, "fmt:sbyte4", 10) == 0) return RD_SHADER_S8;
	if (strnicmp(token, "fmt:half2", 9) == 0) return RD_SHADER_F16;
	if (strnicmp(token, "fmt:float", 9) == 0) return RD_SHADER_F32;
	return RD_SHADER_U8;
}

static uint8_t rdShader_ParseReduction(const char* name)
{
	static const char* keywords[RD_SHADER_SRC_RED_COUNT] =
	{
		"", "lum", "sum", "avg", "min", "max", "mag",
	};

	for (uint8_t i = 0; i < RD_SHADER_SRC_RED_COUNT; ++i)
	{
		if (strnicmp(name, keywords[i], 3) == 0)
			return i;
	}

	return 0;
}

static uint8_t rdShader_ParseDstRegisterType(char c)
{
	if (c == 'r')
		return RD_SHADER_GPR;
	return 0; // todo: error
}

static uint8_t rdShader_ParseSwizzle(const char* swizzle)
{
	const int length = strlen(swizzle);
	if (length == 0 || length > 4) return 0xE4; // Default "xyzw"

	uint8_t mask = 0;
	char last = 'x'; // Default to 'x' if missing

	for (int i = 0; i < 4; i++)
	{
		char c = (i < length) ? swizzle[i] : last; // Extend last valid character
		switch (c)
		{
		case 'x': case 'r': last = 'x'; mask |= (0 << (i * 2)); break;
		case 'y': case 'g': last = 'y'; mask |= (1 << (i * 2)); break;
		case 'z': case 'b': last = 'z'; mask |= (2 << (i * 2)); break;
		case 'w': case 'a': last = 'w'; mask |= (3 << (i * 2)); break;
		default: return 0xE4; // Invalid input, return default
		}
	}
	return mask;
}

static int rdShader_ExtractSwizzle(const char* expression, char* swizzle_out)
{
	int mask = 0;

	// Lookup table for swizzle characters to write masks
	const int swizzle_map[] = { RD_WRITE_RED, RD_WRITE_GREEN, RD_WRITE_BLUE, RD_WRITE_ALPHA };
	const char swizzle_chars[] = { 'x', 'y', 'z', 'w', 'r', 'g', 'b', 'a', '\0' };

	int i;
	for (i = 1; i < 5; i++)
	{
		// max 4 swizzle chars (xyzw or rgba)
		char c = expression[i];
		if (c != 'x' && c != 'y' && c != 'z' && c != 'w'
			&& c != 'r' && c != 'g' && c != 'b' && c != 'a')
		{
			break; // stop at first non-swizzle character
		}
		swizzle_out[i - 1] = c;

		int idx = _strchr(swizzle_chars, c) - swizzle_chars;
		mask |= swizzle_map[idx & 3]; // set the appropriate mask
	}

	swizzle_out[i - 1] = '\0'; // null-terminate

	return mask;
}

static char* rdShader_ParseRegister(char* token, char* swizzle, rdShader_Register* reg)
{
	// extract register index or immediate value
	if (reg->type >= RD_SHADER_IMM8)
	{
		reg->immediate.x = fabs(atof(token));
		reg->swizzle = RD_SWIZZLE_XYZW;
		reg->mask = RD_WRITE_RGBA;
		reg->address = stdMath_FloatToMini8(reg->immediate.x);
	}
	else
	{
		if (reg->address == 0xFF)
			reg->address = atoi(token);

		// handle the swizzle mask
		if (swizzle)
		{
			char mask[5] = { 'x', 'y', 'z', 'w', '\0' };
			reg->mask = rdShader_ExtractSwizzle(swizzle, mask);
			reg->swizzle = rdShader_ParseSwizzle(mask);
		}
		else
		{
			reg->swizzle = RD_SWIZZLE_XYZW;
			reg->mask = RD_WRITE_RGBA;
		}
	}

	if (reg->type == RD_SHADER_GPR && reg->address != 0xFF)
	{
		if (rdShader_pCurrentAssembler->shader->regcount < reg->address)
			rdShader_pCurrentAssembler->shader->regcount = reg->address;
	}

	return token;
}

static void rdShader_ParseDestinationOperand(char* token, rdShader_DestOperand* op)
{
	op->reg.address = 0xFF;

	// find the swizzle
	char* swizzle = _strchr(token, '.');

	// if this isn't an immediate value, clear the swizzle
	if (isalpha(token[0]) && swizzle)
		*swizzle = '\0';
		
	char* alias = (char*)stdHashTable_GetKeyVal(rdShader_pCurrentAssembler->aliasHash, token);
	if (alias)
	{
		op->reg.type = rdShader_ParseDstRegisterType(alias[0]);
		op->reg.address = atoi(alias + 1);
		token += strlen(token);
	}
	else
	{
		char typeChar = token[0];
		if ((typeChar != 'r') && (typeChar != 'R'))
			return; // todo: error
		++token;
	}

	if (swizzle)
		*swizzle = '.';

	rdShader_ParseRegister(token, swizzle, &op->reg);
}

static char* rdShader_ParseSourceRegister(char* token, rdShader_Register* reg)
{
	// find the swizzle
	char* swizzle = _strchr(token, '.');

	if (isdigit(token[0])) // immediate value
	{
		if (reg->idx >= 2)
			reg->type = RD_SHADER_IMM8;
		else if (reg->idx >= 1)
			reg->type = RD_SHADER_IMM16;
		else
			reg->type = RD_SHADER_IMM32;
	}
	else
	{
		if (swizzle)
			*swizzle = '\0'; // set to null so we can parse the name

		// kill any trailing spaces that fuck with the alias key
		char* trailingSpace = strchr(token, ' ');
		if (trailingSpace)
			*trailingSpace = '\0';

		// check for an alias
		char* alias = (char*)stdHashTable_GetKeyVal(rdShader_pCurrentAssembler->aliasHash, token);
		if (alias)
		{
			reg->type = rdShader_ParseSrcRegisterType(alias[0]);
			reg->address = atoi(alias + 1);
			if (trailingSpace) // skip ahead
				token = trailingSpace;
		}
		else
		{
			reg->type = rdShader_ParseSrcRegisterType(token[0]);
			++token;
		}

		// restore
		if (trailingSpace)
			*trailingSpace = ' ';

		if (swizzle)
			*swizzle = '.';
	}

	token = rdShader_ParseRegister(token, swizzle, reg);

	return token;
}

static void rdShader_ParseSourceOperandExpression(char* token, rdShader_SrcOperand* op)
{
	token = rdShader_ParseSourceRegister(token, &op->reg);
	while (isspace(*token)) // skip whitespace
		token++;

	char* modifiers = _strchr(token, '[');
	if (modifiers)
	{
		char tmp[128];
		char* arg = stdString_GetEnclosedStringContents(modifiers, tmp, 128, '[', ']');
		if (!arg)
			return;

		char* modifier = tmp;
		while (*modifier)
		{
			char* next_comma = _strchr(modifier, ' ');
			if (next_comma)
				*next_comma = '\0';

			// trim whitespace from the current modifier
			while (isspace(*modifier))
				modifier++;

			// try to parse multipliers first
			uint8_t mult = rdShader_ParseMultiplier(modifier);
			if (mult)
				op->mult = mult;
			else if (strnicmp(modifier, "negate", 6) == 0)
				op->reg.unary = RD_SHADER_NEGATE;
			else if (strnicmp(modifier, "invert", 6) == 0)
				op->reg.unary = RD_SHADER_INVERT;
			else if (strnicmp(modifier, "rcp", 3) == 0)
				op->reg.unary = RD_SHADER_RCP;
			else
				op->reg.fmt = rdShader_ParseFormat(modifier);

			if (next_comma)
				modifier = next_comma + 1;
			else
				break;
		}
	}
}

// source operand layout
//  |31         30|29          27|26       25|24       16|15    8|7   6|5   4|  3  |2    0|
//  |  reduction  |  scale/bias  |  neg/inv  |  swizzle  |  addr | idx | fmt | abs | type |
uint32_t rdShader_AssembleSrc(
	uint8_t idx,
	uint8_t type,
	uint8_t fmt,
	uint8_t addr,
	uint8_t abs,
	uint8_t swizzle,
	uint8_t unary,
	uint8_t scale_bias,
	uint8_t reduction
)
{
	uint32_t result;
	result = type & 0x7;
	result |= (abs & 0x1) << 3;
	result |= (fmt & 0x3) << 4;
	result |= (idx & 0x3) << 6;
	result |= (addr & 0xFF) << 8;
	result |= (swizzle & 0xFF) << 16;
	result |= (unary & 0x3) << 24;
	result |= (scale_bias & 0x7) << 26;
	result |= (reduction & 0x7) << 29;

	return result;
}

// operation + destination layout
// |31         29|  28 |27          25|24       17|16     10|    9    |8      7|    6    |5       0|
// | write mask  | abs |  multiplier  |  swizzle  |  index  |  negate | format | precise | op code |
uint32_t rdShader_AssembleOpAndDst(
	uint8_t opcode,
	uint8_t fmt,
	uint8_t addr,
	uint8_t swizzle,
	uint8_t multiplier,
	uint8_t write_mask,
	uint8_t precise,
	uint8_t abs,
	uint8_t neg,
	uint8_t clamp
)
{
	uint32_t result;
	result  = (opcode & 0x1F);
	result |= (precise & 0x1) << 5;
	result |= (fmt & 0x3) << 6;
	result |= (neg & 0x1) << 8;
	result |= (clamp & 0x1) << 9;
	result |= (addr & 0x3F) << 10;
	result |= (swizzle & 0xFF) << 16;
	result |= (multiplier & 0x7) << 24;
	result |= (abs & 0x1) << 27;
	result |= (write_mask & 0xF) << 28;
	return result;
}

static int rdShader_AssembleInstruction(rdShaderInstr* result, rdShader_Instruction* inst)
{
	result->op_dst = rdShader_AssembleOpAndDst(inst->opcode,
											   inst->dest.reg.fmt,
											   inst->dest.reg.address,
											   inst->dest.reg.swizzle,
											   inst->dest.multiplier,
											   inst->dest.reg.mask,
											   inst->precise,
											   inst->dest.reg.abs,
											   inst->dest.reg.unary,
											   inst->clamp);

	// if we have 3 operands, then immediate values must be 8 bit
	if (inst->srcCount >= 3)
	{
		if (inst->src[0].reg.type >= RD_SHADER_IMM8) inst->src[0].reg.type = RD_SHADER_IMM8;
		if (inst->src[1].reg.type >= RD_SHADER_IMM8) inst->src[1].reg.type = RD_SHADER_IMM8;
	}
	else if (inst->srcCount >= 2) // same for 2 operands, must not exceed 16 bit
	{
		if (inst->src[0].reg.type >= RD_SHADER_IMM16) inst->src[0].reg.type = RD_SHADER_IMM16;
		if (inst->src[1].reg.type >= RD_SHADER_IMM16) inst->src[1].reg.type = RD_SHADER_IMM16;
	}

	// store source operand 0
	result->src0 = rdShader_AssembleSrc(0,
										inst->src[0].reg.type,
										inst->src[0].reg.fmt,
										inst->src[0].reg.address,
										inst->src[0].reg.abs,
										inst->src[0].reg.swizzle,
										inst->src[0].reg.unary,
										inst->src[0].mult,
										inst->src[0].reduction);

	// store source operand 1 if we need it
	if (inst->srcCount > 1)
	{
		result->src1 = rdShader_AssembleSrc(1,
											inst->src[1].reg.type,
											inst->src[1].reg.fmt,
											inst->src[1].reg.address,
											inst->src[1].reg.abs,
											inst->src[1].reg.swizzle,
											inst->src[1].reg.unary,
											inst->src[1].mult,
											inst->src[1].reduction);
	}

	// store source operand 2 if we need it
	// if we don't, use it to store immediate values
	if (inst->srcCount > 2)
	{
		result->src2 = rdShader_AssembleSrc(2,
											inst->src[2].reg.type,
											inst->src[2].reg.fmt,
											inst->src[2].reg.address,
											inst->src[2].reg.abs,
											inst->src[2].reg.swizzle,
											inst->src[2].reg.unary,
											inst->src[2].mult,
											inst->src[2].reduction);
	}
	else if (inst->srcCount > 1)
	{
		// first operand takes up the entire immediate space
		if (inst->src[0].reg.type == RD_SHADER_IMM4x8)
		{
			result->src2 = stdMath_PackSnorm4x8(&inst->src[0].reg.immediate);
		}
		// second operand takes up the entire immediate space
		else if (inst->src[1].reg.type == RD_SHADER_IMM4x8)
		{
			result->src2 = stdMath_PackSnorm4x8(&inst->src[1].reg.immediate);
		}
		// otherwise they share the space, operand 0 in low bits, 1 in high bits
		else
		{
			float imm0 = inst->src[0].reg.immediate.x;
			float imm1 = inst->src[1].reg.immediate.x;
			result->src2 = stdMath_PackHalf2x16(imm0, imm1);
		}
	}
	else
	{
		if (inst->src[0].reg.type == RD_SHADER_IMM4x8)
		{
			result->src2 = stdMath_PackSnorm4x8(&inst->src[0].reg.immediate);
		}
		else
		{
			float imm0 = inst->src[0].reg.immediate.x;
			result->src2 = stdMath_FloatBitsToUint(imm0);
		}
	}

	return 1;
}


static void rdShader_AddAlias(const char* name, const char* reg)
{
	rdShader_Alias* alias = (rdShader_Alias*)malloc(sizeof(rdShader_Alias));
	if (!alias)
		return;

	stdString_SafeStrCopy(alias->name, name, 16);
	stdString_SafeStrCopy(alias->reg, reg, 16);

	alias->next = rdShader_pCurrentAssembler->firstAlias;
	rdShader_pCurrentAssembler->firstAlias = alias;
	alias = stdHashTable_SetKeyVal(rdShader_pCurrentAssembler->aliasHash, alias->name, alias->reg);
}

static void rdShader_ParseAlias(char* name)
{
	char* comma = _strchr(name, ',');
	if (comma)
	{
		*comma = '\0';
		while (isspace(*name))
			name++;

		char* alias = (char*)stdHashTable_GetKeyVal(rdShader_pCurrentAssembler->aliasHash, name);
		if (!alias)
		{
			char* reg = comma + 1;
			while (isspace(*reg))
				reg++;
			rdShader_AddAlias(name, reg);
		}
	}
}

static void rdShader_ParseSourceOperandWithModifiers(char* token, rdShader_SrcOperand* op)
{
	if (token[0] == '-') // check for negate
	{
		op->reg.unary = RD_SHADER_NEGATE;
		token++;
	}
	else if (strncmp(token, "1 - ", 4) == 0) // check for invert, todo: fix spaces
	{
		op->reg.unary = RD_SHADER_INVERT;
		token += 4;
	}
	else if (strncmp(token, "1 / ", 4) == 0)
	{
		op->reg.unary = RD_SHADER_RCP;
		token += 4;
	}

	if (strncmp(token, "abs", 3) == 0) // check for abs
	{
		op->reg.abs = 1;
		token += 3;

		char temp[128];
		char* arg = stdString_GetEnclosedStringContents(token, temp, 128, '(', ')');
		if (arg)
		{
			rdShader_ParseSourceOperandExpression(temp, op);
		}
		else
		{
			// todo: error   
		}
	}
	if (token[0] == '(') // check for expression
	{
		char temp[128];
		char* arg = stdString_GetEnclosedStringContents(token, temp, 128, '(', ')');
		if (arg)
		{
			rdShader_ParseSourceOperandExpression(temp, op);
		}
		else
		{
			// todo: error   
		}
	}
	else
	{
		rdShader_ParseSourceOperandExpression(token, op);
	}
}

void rdShader_ParseSourceOperand(char* token, rdShader_SrcOperand* op)
{
	op->reg.address = 0xFF;

	// check for a reduction
	uint8_t reduction = rdShader_ParseReduction(token);
	if (reduction)
	{
		op->reduction = reduction;
		token += 3;

		// fetch the content within the ()
		char temp[128];
		char* arg = stdString_GetEnclosedStringContents(token, temp, 128, '(', ')');
		if (arg)
		{
			rdShader_ParseSourceOperandWithModifiers(temp, op);
		}
		else
		{
			// todo: error
		}
	}
	else
	{
		rdShader_ParseSourceOperandWithModifiers(token, op);
	}
}

void rdShader_ParseInstructionModifiers(const char* modifierStart, rdShader_Instruction* inst)
{
	char buffer[128];
	strncpy(buffer, modifierStart, 128 - 1);
	buffer[128 - 1] = '\0';

	// split the modifiers by commas
	char* token = strtok(buffer, " ");
	if (!token)
		return;

	while (isspace(token[0]))
		token++;

	while (token)
	{
		uint8_t multiplier = rdShader_ParseMultiplier(token);
		if (multiplier)
			inst->dest.multiplier = multiplier;
		else if (strnicmp(token, "precise", 7) == 0)
			inst->precise = 1;
		else if (strnicmp(token, "clamp", 5) == 0)
			inst->clamp = 1;
		else if (strnicmp(token, "abs", 3) == 0)
			inst->dest.reg.abs = 1;
		else if (strnicmp(token, "negate", 6) == 0)
			inst->dest.reg.unary = RD_SHADER_NEGATE;
		else
			inst->dest.reg.fmt = rdShader_ParseFormat(token);

		token = strtok(NULL, " ");
	}
}

int rdShader_ParseInstruction(char* line, rdShaderInstr* result, int maxOut)
{
	rdShader_Instruction inst;
	memset(&inst, 0, sizeof(rdShader_Instruction));

	// copy the line to a buffer for manipulation
	char buffer[512];
	strncpy(buffer, line, sizeof(buffer));
	buffer[sizeof(buffer) - 1] = '\0';

	// split the line into tokens
	char* token = strtok(buffer, " \t");
	if (!token)
		return 0;

	// try real opcode first
	inst.opcode = rdShader_ParseOpCode(token);

	// fallback to macro lookup
	const rdShader_Macro* macro = NULL;
	if (inst.opcode == RD_SHADER_OP_NOP)
	{
		macro = rdShader_FindMacro(token);
		if (!macro)
			return 0;
	}

	if (inst.src[0].reg.type == RD_SHADER_TEX && inst.src[0].reg.address == 4)
		rdShader_pCurrentAssembler->shader->hasReadback = 1;

	// parse the destination operand
	token = strtok(NULL, ",");
	if (!token)
		return 0;

	rdShader_ParseDestinationOperand(token, &inst.dest);

	// parse the source operands
	inst.srcCount = macro ? macro->srcCount : rdShader_OperandCount(inst.opcode);
	for (int i = 0; i < inst.srcCount; ++i)
	{
		token = strtok(NULL, ",");
		if (!token)
			break; // todo: error

		while (isspace(token[0]))
			++token;

		inst.src[i].reg.idx = i;

		if (token[0] == '(') // vector declaration
		{
			if (i == 2)
			{
				// todo: error, 3 operand instructions don't support vector immediates
				continue;
			}
			else if (i == 1)
			{
				if (inst.src[0].reg.type >= RD_SHADER_IMM8)
				{
					// todo: error, only 1 immediate value can be present in an instruction with a vector declaration
					continue;
				}
			}
			_sscanf(token, "(%f/%f/%f/%f)",
					&inst.src[i].reg.immediate.x,
					&inst.src[i].reg.immediate.y,
					&inst.src[i].reg.immediate.z,
					&inst.src[i].reg.immediate.w);
			inst.src[i].reg.type = RD_SHADER_IMM4x8;
			inst.src[i].reg.fmt = RD_SHADER_S8;
			inst.src[i].reg.address = 0;
			inst.src[i].reg.swizzle = RD_SWIZZLE_XYZW;
			inst.src[i].reg.mask = RD_WRITE_RGBA;
			inst.src[i].reg.abs = 0;
			inst.src[i].reg.unary = 0;
		}
		else
		{
			rdShader_ParseSourceOperand(token, &inst.src[i]);
		}
	}

	// anything else is a modifier for the instruction
	if (token && token[0] != '\0')
	{
		char* modifiers = strpbrk(token, " \t");
		if (modifiers)
			rdShader_ParseInstructionModifiers(modifiers + 1, &inst);
	}

	// expand macro or assemble single instruction
	if (macro)
		return macro->expand(&inst, result, maxOut);

	return rdShader_AssembleInstruction(result, &inst);
}

// returns 1 if the line is a label definition ("label:")
static int rdShader_IsLabelDefinition(const char* ln)
{
	const char* p = ln;
	while (*p && (isalnum((unsigned char)*p) || *p == '_'))
		++p;
	return (*p == ':' && p > ln);
}

static void rdShader_DefineLabel(const char* ln, uint8_t pc)
{
	const char* colon = _strchr(ln, ':');
	if (!colon || colon == ln)
		return;

	rdShader_Assembler* asmblr = rdShader_pCurrentAssembler;
	if (asmblr->labelCount >= RD_SHADER_MAX_LABELS)
		return; // todo: error

	rdShader_Label* label = &asmblr->labels[asmblr->labelCount++];
	int len = (int)(colon - ln);
	if (len >= (int)sizeof(label->name))
		len = (int)sizeof(label->name) - 1;

	memcpy(label->name, ln, len);
	label->name[len] = '\0';
	label->pc = pc;
}

static int rdShader_LookupLabel(const char* name)
{
	rdShader_Assembler* asmblr = rdShader_pCurrentAssembler;
	for (int i = 0; i < asmblr->labelCount; ++i)
	{
		if (stricmp(asmblr->labels[i].name, name) == 0)
			return (int)asmblr->labels[i].pc;
	}
	return -1;
}

// emit a call instruction
// registers a fixup if the label isn't defined yet
static void rdShader_EmitCall(const char* labelName, rdShaderInstr* out, int instrIdx)
{
	memset(out, 0, sizeof(rdShaderInstr));

	int targetPc = rdShader_LookupLabel(labelName);
	if (targetPc < 0)
	{
		// forward reference: record a fixup and emit with a placeholder address
		rdShader_Assembler* asmblr = rdShader_pCurrentAssembler;
		if (asmblr->fixupCount < RD_SHADER_MAX_FIXUPS)
		{
			rdShader_Fixup* fixup = &asmblr->fixups[asmblr->fixupCount++];
			stdString_SafeStrCopy(fixup->name, labelName, sizeof(fixup->name));
			fixup->instrIdx = (uint16_t)instrIdx;
		}
		targetPc = 0; // placeholder, patched by rdShader_ResolveFixups
	}

	out->op_dst = rdShader_AssembleOpAndDst(RD_SHADER_OP_CALL, 0, (uint8_t)targetPc,
											RD_SWIZZLE_XYZW, 0, RD_WRITE_RGBA, 0, 0, 0, 0);
}

// patch any forward-referenced call targets after all labels are known
static void rdShader_ResolveFixups(rdShader* shader)
{
	rdShader_Assembler* asmblr = rdShader_pCurrentAssembler;
	for (int i = 0; i < asmblr->fixupCount; ++i)
	{
		rdShader_Fixup* fixup = &asmblr->fixups[i];
		int pc = rdShader_LookupLabel(fixup->name);
		if (pc < 0)
			continue; // unresolved label, leave as placeholder

		// patch the addr field (bits 10-15) in op_dst
		rdShaderInstr* instr = &shader->byteCode.instructions[fixup->instrIdx];
		instr->op_dst = (instr->op_dst & ~(0x3Fu << 10)) | (((uint32_t)pc & 0x3Fu) << 10);
	}
}

static void rdShader_InitAliasHash(rdShader_Assembler* assembler)
{
	assembler->aliasHash = stdHashTable_New(64);

	// add default system aliases
	static const rdShader_StaticAlias systemAliases[] =
	{
		{"sv:sec",      "c8"},
		{"sv:xy",       "c9"},
		{"sv:z",        "c10"},
		{"sv:pos",      "c11"},
		{"sv:vdir",     "c12"},
		{"sv:norm",     "c13"},
		{"sv:wpos",     "c14"},
		{"sv:wvdir",    "c15"},
		{"sv:wnorm",    "c16"},
		{"sv:uv",       "c17"},
		{"sv:aspect",   "c18"},
		{"sv:identity", "c19"}
	};
	for (uint8_t i = 0; i < ARRAY_SIZE(systemAliases); ++i)
		stdHashTable_SetKeyVal(assembler->aliasHash, systemAliases[i].name, systemAliases[i].reg);

	// add default material aliases
	static const rdShader_StaticAlias materialAliases[] =
	{
		{"mat:fill",         "c32"},
		{"mat:albedo",       "c33"},
		{"mat:glow",         "c34"},
		{"mat:f0",           "c35"},
		{"mat:roughness",    "c36"},
		{"mat:displacement", "c37"}
	};
	for (uint8_t i = 0; i < ARRAY_SIZE(materialAliases); ++i)
		stdHashTable_SetKeyVal(assembler->aliasHash, materialAliases[i].name, materialAliases[i].reg);

	// special texture register ailiases
	static const rdShader_StaticAlias texAliases[] =
	{
		{"tex0", "t0"},
		{"tex1", "t1"},
		{"tex2", "t2"},
		{"tex3", "t3"},
		{"fbo",  "t4"}
	};

	for (uint8_t i = 0; i < ARRAY_SIZE(texAliases); ++i)
		stdHashTable_SetKeyVal(assembler->aliasHash, texAliases[i].name, texAliases[i].reg);
}

rdShader* rdShader_New(char* fpath)
{
	rdShader* shader;
	shader = (rdShader*)rdroid_pHS->alloc(sizeof(rdShader));
    if ( shader )
    {
		rdShader_NewEntry(shader, fpath);
    }
    
    return shader;
}

int rdShader_NewEntry(rdShader* shader, char* path)
{
    if (path)
        stdString_SafeStrCopy(shader->name, path, 0x20);
	shader->id = -1;
	shader->shaderid = std3D_GenShader();
	return 0;
}

void rdShader_Free(rdShader* shader)
{
    if (shader)
    {
		rdShader_FreeEntry(shader);
        rdroid_pHS->free(shader);
    }
}

void rdShader_FreeEntry(rdShader* shader)
{
	std3D_DeleteShader(shader->shaderid);
}

int rdShader_LoadEntry(char* fpath, rdShader* shader)
{
	stdString_SafeStrCopy(shader->name, stdFileFromPath(fpath), 0x20);

	if (!stdConffile_OpenRead(fpath))
		return 0;

	if (!stdConffile_ReadLine())
	{
		stdConffile_Close();
		return 0;
	}

	if (!sscanf_s(stdConffile_aLine, "ps.%f", &shader->byteCode.version))
	{
		stdConffile_Close();
		return 0;
	}

	rdShader_Assembler assembler;
	memset(&assembler, 0, sizeof(rdShader_Assembler));
	assembler.shader = shader;

	rdShader_pCurrentAssembler = &assembler;

	rdShader_InitAliasHash(&assembler);

	while (stdConffile_ReadLine())
	{
		//stdString_SafeStrCopy(tmp, curLine, curLineLen + 1);

		char* ln = stdConffile_aLine;
		if (ln && *ln)
		{
			if (strnicmp(ln, "alias", 5) == 0)
			{
				rdShader_ParseAlias(ln + 5);
			}
			else if (rdShader_IsLabelDefinition(ln))
			{
				// label definition: record the current PC for this name
				rdShader_DefineLabel(ln, (uint8_t)shader->byteCode.instructionCount);
			}
			else if (strnicmp(ln, "call", 4) == 0 && (ln[4] == '\0' || isspace((unsigned char)ln[4])))
			{
				// call <label>
				char* labelName = ln + 4;
				while (isspace((unsigned char)*labelName))
					++labelName;

				if (*labelName && shader->byteCode.instructionCount < (int)ARRAY_SIZE(shader->byteCode.instructions))
				{
					int idx = shader->byteCode.instructionCount;
					rdShader_EmitCall(labelName, &shader->byteCode.instructions[idx], idx);
					shader->byteCode.instructionCount++;

					++assembler.currentCallDepth;
					if (assembler.currentCallDepth > assembler.maxCallDepth)
						assembler.maxCallDepth = assembler.currentCallDepth;
				}
			}
			else if (strnicmp(ln, "ret", 3) == 0 && (ln[3] == '\0' || isspace((unsigned char)ln[3])))
			{
				// return from subroutine
				if (shader->byteCode.instructionCount < (int)ARRAY_SIZE(shader->byteCode.instructions))
				{
					rdShaderInstr* out = &shader->byteCode.instructions[shader->byteCode.instructionCount];
					memset(out, 0, sizeof(rdShaderInstr));
					out->op_dst = rdShader_AssembleOpAndDst(RD_SHADER_OP_RET, 0, 0,
															RD_SWIZZLE_XYZW, 0, RD_WRITE_RGBA, 0, 0, 0, 0);
					shader->byteCode.instructionCount++;

					if (assembler.currentCallDepth > 0)
						--assembler.currentCallDepth;
				}
			}
			else
			{
				int remaining = (int)ARRAY_SIZE(shader->byteCode.instructions) - (int)shader->byteCode.instructionCount;
				int emitted = rdShader_ParseInstruction(stdConffile_aLine, &shader->byteCode.instructions[shader->byteCode.instructionCount], remaining);
				shader->byteCode.instructionCount += emitted;
			}
		}

		if (shader->byteCode.instructionCount >= ARRAY_SIZE(shader->byteCode.instructions))
			break;
	}

	shader->callDepth = assembler.maxCallDepth;

	// resolve any forward-referenced call targets
	rdShader_ResolveFixups(shader);

	if (shader->regcount >= 8)
		//todo error
		goto cleanup;

	std3D_UploadShader(shader);

cleanup:
	rdShader_Alias* alias = assembler.firstAlias;
	while (alias)
	{
		rdShader_Alias* next = alias->next;
		free(alias);
		alias = next;
	}

	rdShader_pCurrentAssembler = NULL;
	stdHashTable_Free(assembler.aliasHash);

	stdConffile_Close();

	return 1;
}

#endif