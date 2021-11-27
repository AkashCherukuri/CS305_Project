// CVP1 Trace Reader
// Author: Arthur Perais (arthur.perais@gmail.com)

/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org>

This project relies on https://www.cs.unc.edu/Research/compgeom/gzstream/ (LGPL) to stream from compressed traces.
For ease of use, gzstream.h and gzstream.C are provided with minor modifications (include paths) with the reader.
*/

// Compilation : Don't forget to add gzstream.C in the source list and to link with zlib (-lz).
//
// Usage : CVPTraceReader reader("./my_trace.tar.gz")
//         while(reader.readInstr())
//           reader.mInstr.printInstr();
//            ...
// Note that this is an exemple file (that was used for CVP).
// Given the trace format below, you can write your own trace reader that
// populates the instruction format in the way you like.
//
// Trace Format :
// Inst PC 				- 8 bytes
// Inst Type 				- 1 byte
// If load/storeInst
//   Effective Address 			- 8 bytes
//   Access Size (one reg)		- 1 byte
// If branch
//   Taken 				- 1 byte
//   If Taken
//     Target				- 8 bytes
// Num Input Regs 			- 1 byte
// Input Reg Names 			- 1 byte each
// Num Output Regs 			- 1 byte
// Output Reg Names 			- 1 byte each
// Output Reg Values
//   If INT (0 to 31) or FLAG (64) 	- 8 bytes each
//   If SIMD (32 to 63)			- 16 bytes each


/* I seriously hate these extremely long lines. Given the time and
chance, all these lines should be restricted to 80 chars and all the
ridiculously excessive white spaces should be removed. */

#include <fstream>
#include <iostream>
#include <vector>
#include <cassert>
#include "lib/gzstream.h"

#define CHAMPSIM_FORMAT     1 // take it as make argument later
#define INSTR_WRITE_TEXT    0
#define INSTR_MEMORY        0

// special registers that help us identify branches
#define REG_STACK_POINTER 102
#define REG_FLAGS 64
#define REG_INSTRUCTION_POINTER 103
#define REG_OTHER 101               // can be any non-zero value

// branch types
#define NOT_BRANCH           0
#define BRANCH_DIRECT_JUMP   1
#define BRANCH_INDIRECT      2
#define BRANCH_CONDITIONAL   3
#define BRANCH_DIRECT_CALL   4
#define BRANCH_INDIRECT_CALL 5
#define BRANCH_RETURN        6
#define BRANCH_OTHER         7

#if 1
enum InstClass : uint8_t
{
  aluInstClass = 0,
  loadInstClass = 1,
  storeInstClass = 2,
  condBranchInstClass = 3,
  uncondDirectBranchInstClass = 4,
  uncondIndirectBranchInstClass = 5,
  fpInstClass = 6,
  slowAluInstClass = 7,
  undefInstClass = 8
};
#endif

// This structure is used by CVP1's simulator.
// Adapt for your own needs.
struct db_operand_t
{
    bool valid;
    bool is_int;
    uint64_t log_reg;
    uint64_t value;

    void print() const
    {
        std::cout << " (int: " << is_int << ", idx: " << log_reg << " val: " << std::hex << value << std::dec << ") ";
    }
};

// This structure is used by CVP1's simulator.
// Adapt for your own needs.
struct db_t
{
    uint8_t insn;
    uint64_t pc;
    uint64_t next_pc;

    db_operand_t A;
    db_operand_t B;
    db_operand_t C;
    db_operand_t D;

    bool is_load;
    bool is_store;
    uint64_t addr;
    uint64_t size;

    void printInst() const
    {
        static constexpr const char *cInfo[] = {"aluOp", "loadOp", "stOp", "condBrOp", "uncondDirBrOp", "uncondIndBrOp", "fpOp", "slowAluOp"};

        std::cout << "[PC: 0x" << std::hex << pc << std::dec << " type: " << cInfo[insn];
        if (insn == InstClass::loadInstClass || insn == InstClass::storeInstClass)
        {
            assert(is_load || is_store);
            std::cout << " ea: 0x" << std::hex << addr << std::dec << " size: " << size;
        }
        if (insn == InstClass::condBranchInstClass || insn == InstClass::uncondDirectBranchInstClass || insn == InstClass::uncondIndirectBranchInstClass)
            std::cout << " ( tkn:" << (next_pc != pc + 4) << " tar: 0x" << std::hex << next_pc << ") " << std::dec;

        if (A.valid)
        {
            std::cout << " 1st input: ";
            A.print();
        }

        if (B.valid)
        {
            std::cout << "2nd input: ";
            B.print();
        }

        if (C.valid)
        {
            std::cout << "3rd input: ";
            C.print();
        }

        if (D.valid)
        {
            std::cout << " output: ";
            D.print();
        }

        std::cout << " ]" << std::endl;
        std::fflush(stdout);
    }
};

// INT registers are registers 0 to 31. SIMD/FP registers are registers 32 to 63. Flag register is register 64
enum Offset
{
    vecOffset = 32,
    ccOffset = 64
};

// Trace reader class.
// Format assumes that instructions have at most three inputs and at most one input.
// If the trace contains an instruction that has more than three inputs, they are ignored.
// If the trace contains an instruction that has more than one outputs, one instruction object will be created for each output,
// For instance, for a multiply, two objects with same PC will be created through two subsequent calls to get_inst(). Each will have the same
// inputs, but one will have the low part of the product as output, and one will have the high part of the product as output.
// Other instructions subject to this are (list not exhaustive): load pair and vector inststructions.
struct CVPTraceReader
{
    struct Instr
    {
        uint64_t mPc;
        uint8_t mType; // Type is InstClass
        bool mTaken;
        uint64_t mTarget;
        uint64_t mEffAddr;
        uint8_t mMemSize; // In bytes
        uint8_t mNumInRegs;
        std::vector<uint8_t> mInRegs;
        uint8_t mNumOutRegs;
        std::vector<uint8_t> mOutRegs;
        std::vector<uint64_t> mOutRegsValues;

        Instr()
        {
            reset();
        }

        void reset()
        {
            mPc = mTarget = 0xdeadbeef;
            mEffAddr = 0xdeadbeef;
            mMemSize = 0;
            mType = undefInstClass;
            mTaken = false;
            mNumInRegs = mNumOutRegs = 0;
            mInRegs.clear();
            mOutRegs.clear();
            mOutRegsValues.clear();
        }

        void printInstr()
        {
            static constexpr const char *cInfo[] = {"aluOp", "loadOp", "stOp", "condBrOp", "uncondDirBrOp", "uncondIndBrOp", "fpOp", "slowAluOp"};

            std::cout << "[PC: 0x" << std::hex << mPc << std::dec << " type: " << cInfo[mType];
            if (mType == InstClass::loadInstClass || mType == InstClass::storeInstClass)
                std::cout << " ea: 0x" << std::hex << mEffAddr << std::dec << " size: " << (uint64_t)mMemSize;

            if (mType == InstClass::condBranchInstClass || mType == InstClass::uncondDirectBranchInstClass || mType == InstClass::uncondIndirectBranchInstClass)
                std::cout << " ( tkn:" << mTaken << " tar: 0x" << std::hex << mTarget << ") ";

            std::cout << std::dec << " InRegs : { ";
            for (unsigned elt : mInRegs)
            {
                std::cout << elt << " ";
            }
            std::cout << " } OutRegs : { ";
            for (unsigned i = 0, j = 0; i < mOutRegs.size(); i++)
            {
                if (mOutRegs[i] >= Offset::vecOffset && mOutRegs[i] != Offset::ccOffset)
                {
                    assert(j + 1 < mOutRegsValues.size());
                    std::cout << std::dec << (unsigned)mOutRegs[i] << std::hex << " (hi:" << mOutRegsValues[j + 1] << " lo:" << mOutRegsValues[j] << ") ";
                    j += 2;
                }
                else
                {
                    assert(j < mOutRegsValues.size());
                    std::cout << std::dec << (unsigned)mOutRegs[i] << std::hex << " (" << mOutRegsValues[j++] << ") ";
                }
            }
            std::cout << ") ]" << std::endl;
            std::fflush(stdout);
        }
    };

    // use this if "using namespace GZSTREAM_NAMESPACE bla bla"
    //gz::igzstream *dpressed_input;
    igzstream *dpressed_input;

    // Buffer to hold trace instruction information
    Instr mInstr;

    // There are bookkeeping variables for trace instructions that necessitate the creation of several objects.
    // Specifically, load pair yields two objects with different register outputs, but SIMD instructions also yield
    // multiple objects but they have the same register output, although one has the low 64-bit and one has the high 64-bit.
    // Thus, for load pair, first piece will have mCrackRegIdx 0 (first output of trace instruction) and second will have mCrackRegIdx 1
    // (second output of trace instruction). However, in both cases, since outputs are 64-bit, mCrackValIdx will remain 0.
    // Conversely, both pieces of a SIMD instruction will have mCrackRegIdx 0 (since SIMD instruction has a single 128-bit output), but first piece
    // will have mCrackValIdx 0 (low 64-bit) and second piece will have mCrackValIdx 1 (high 64-bit).
    uint8_t mCrackRegIdx;
    uint8_t mCrackValIdx;

    // How many pieces left to generate for the current trace instruction being processed.
    uint8_t mRemainingPieces;

    // Inverse of memory size multiplier for each piece. E.g., a 128-bit access will have size 16, and each piece will have size 8 (16 * 1/2).
    uint8_t mSizeFactor;

    // Number of instructions processed so far.
    uint64_t nInstr;

    // This simply tracks how many lanes one SIMD register have been processed.
    // In this case, since SIMD is 128 bits and pieces output 64 bits, if it is pair and we are creating an instruction object from a trace instruction, this means that
    // the output of the instruction object will contain the low order bits of the SIMD register.
    // If it is odd, it means that it will contain the high order bits of the SIMD register.
    uint8_t start_fp_reg;

    // Note that there is no check for trace existence, so modify to suit your needs.
    CVPTraceReader(const char *trace_name)
    {
        dpressed_input = new igzstream();
        dpressed_input->open(trace_name, std::ios_base::in | std::ios_base::binary);

        mCrackRegIdx = mCrackValIdx = mRemainingPieces = mSizeFactor = nInstr = start_fp_reg = 0;
    }

    ~CVPTraceReader()
    {
        if (dpressed_input)
            delete dpressed_input;

        std::cout << " Read " << nInstr << " instrs " << std::endl;
    }

    // This is the main API function
    // There is no specific reason to call the other functions from without this file.
    // Idiom is : while(instr = get_inst())
    //              ... process instr
    db_t *get_inst()
    {
        // If we are creating several pieces from a single trace instructions and some are left to create,
        // mRemainingPieces will be != 0
        if (mRemainingPieces)
            return populateNewInstr();
        // If there is a single piece to create
        else if (readInstr())
            return populateNewInstr();
        // If the trace is done
        else
            return nullptr;
    }

    // Creates a new object and populate it with trace information.
    // Subsequent calls to populateNewInstr() will take care of creating multiple pieces for a trace instruction
    // that has several outputs or 128-bit output.
    // Number of calls is decided by mRemainingPieces from get_inst().
    db_t *populateNewInstr()
    {
        db_t *inst = new db_t();

        inst->insn = mInstr.mType;
        inst->pc = mInstr.mPc;
        inst->next_pc = mInstr.mTarget;

        if (mInstr.mNumInRegs >= 1)
        {
            inst->A.valid = true;
            inst->A.is_int = mInstr.mInRegs.at(0) < Offset::vecOffset || mInstr.mInRegs[0] == Offset::ccOffset;
            inst->A.log_reg = mInstr.mInRegs[0];
            inst->A.value = 0xdeadbeef;
        }
        else
            inst->A.valid = false;

        if (mInstr.mNumInRegs >= 2)
        {
            inst->B.valid = true;
            inst->B.is_int = mInstr.mInRegs.at(1) < Offset::vecOffset || mInstr.mInRegs[1] == Offset::ccOffset;
            inst->B.log_reg = mInstr.mInRegs[1];
            inst->B.value = 0xdeadbeef;
        }
        else
            inst->B.valid = false;

        if (mInstr.mNumInRegs >= 3)
        {
            inst->C.valid = true;
            inst->C.is_int = mInstr.mInRegs.at(2) < Offset::vecOffset || mInstr.mInRegs[2] == Offset::ccOffset;
            inst->C.log_reg = mInstr.mInRegs[2];
            inst->C.value = 0xdeadbeef;
        }
        else
            inst->C.valid = false;

        // We'll ignore that some ARM instructions have more than 3 inputs
        // assert(mInstr.mNumInRegs <= 3);

        if (mInstr.mNumOutRegs >= 1)
        {
            inst->D.valid = true;
            // Flag register is considered to be INT
            inst->D.is_int = mInstr.mOutRegs.at(mCrackRegIdx) < Offset::vecOffset || mInstr.mOutRegs.at(mCrackRegIdx) == Offset::ccOffset;
            inst->D.log_reg = mInstr.mOutRegs[mCrackRegIdx];
            inst->D.value = mInstr.mOutRegsValues.at(mCrackValIdx);
            // if SIMD register, we processed one more 64-bit lane.
            if (!inst->D.is_int)
                start_fp_reg++;
            else
                start_fp_reg = 0;
        }
        else
        {
            inst->D.valid = false;
            start_fp_reg = 0;
        }

        inst->is_load = mInstr.mType == InstClass::loadInstClass;
        inst->is_store = mInstr.mType == InstClass::storeInstClass;
        inst->addr = mInstr.mEffAddr + ((mSizeFactor - mRemainingPieces) * 4);
        inst->size = std::max(1, mInstr.mMemSize / mSizeFactor);

        assert(inst->size || !(inst->is_load || inst->is_store));
        assert(mRemainingPieces != 0);

        // At this point, if mRemainingPieces is 0, the next statements will have no effect.
        mRemainingPieces--;

        // If there are more output registers to be processed and they are SIMD
        if (mInstr.mNumOutRegs > mCrackRegIdx && mInstr.mOutRegs.at(mCrackRegIdx) >= Offset::vecOffset && mInstr.mOutRegs[mCrackRegIdx] != Offset::ccOffset)
        {
            // Next output value is in the next 64-bit lane
            mCrackValIdx++;
            // If we processed the high-order bits of the current SIMD register, the next output is a different register
            if (start_fp_reg % 2 == 0)
                mCrackRegIdx++;
        }
        // If there are more output INT registers, go to next value and next register name
        else
        {
            mCrackValIdx++;
            mCrackRegIdx++;
        }

        return inst;
    }

    // Read bytes from the trace and populate a buffer object.
    // Returns true if something was read from the trace, false if we the trace is over.
    bool readInstr()
    {
        // Trace Format :
        // Inst PC 				- 8 bytes
        // Inst Type			- 1 byte
        // If load/storeInst
        //   Effective Address 		- 8 bytes
        //   Access Size (one reg)		- 1 byte
        // If branch
        //   Taken 				- 1 byte
        //   If Taken
        //     Target			- 8 bytes
        // Num Input Regs 			- 1 byte
        // Input Reg Names 			- 1 byte each
        // Num Output Regs 			- 1 byte
        // Output Reg Names			- 1 byte each
        // Output Reg Values
        //   If INT (0 to 31) or FLAG (64) 	- 8 bytes each
        //   If SIMD (32 to 63)		- 16 bytes each
        mInstr.reset();
        start_fp_reg = 0;
        dpressed_input->read((char *)&mInstr.mPc, sizeof(mInstr.mPc));

        if (dpressed_input->eof())
            return false;

        mRemainingPieces = 1;
        mSizeFactor = 1;
        mCrackRegIdx = 0;
        mCrackValIdx = 0;

        mInstr.mTarget = mInstr.mPc + 4;
        dpressed_input->read((char *)&mInstr.mType, sizeof(mInstr.mType));

        assert(mInstr.mType != undefInstClass);

        if (mInstr.mType == InstClass::loadInstClass || mInstr.mType == InstClass::storeInstClass)
        {
            dpressed_input->read((char *)&mInstr.mEffAddr, sizeof(mInstr.mEffAddr));
            dpressed_input->read((char *)&mInstr.mMemSize, sizeof(mInstr.mMemSize));
        }
        if (mInstr.mType == InstClass::condBranchInstClass || mInstr.mType == InstClass::uncondDirectBranchInstClass || mInstr.mType == InstClass::uncondIndirectBranchInstClass)
        {
            dpressed_input->read((char *)&mInstr.mTaken, sizeof(mInstr.mTaken));
            if (mInstr.mTaken)
                dpressed_input->read((char *)&mInstr.mTarget, sizeof(mInstr.mTarget));
        }

        dpressed_input->read((char *)&mInstr.mNumInRegs, sizeof(mInstr.mNumInRegs));

        for (auto i = 0; i != mInstr.mNumInRegs; i++)
        {
            uint8_t inReg;
            dpressed_input->read((char *)&inReg, sizeof(inReg));
            mInstr.mInRegs.push_back(inReg);
        }

        dpressed_input->read((char *)&mInstr.mNumOutRegs, sizeof(mInstr.mNumOutRegs));

        mRemainingPieces = std::max(mRemainingPieces, mInstr.mNumOutRegs);

        for (auto i = 0; i != mInstr.mNumOutRegs; i++)
        {
            uint8_t outReg;
            dpressed_input->read((char *)&outReg, sizeof(outReg));
            mInstr.mOutRegs.push_back(outReg);
        }

        for (auto i = 0; i != mInstr.mNumOutRegs; i++)
        {
            uint64_t val;
            dpressed_input->read((char *)&val, sizeof(val));
            mInstr.mOutRegsValues.push_back(val);
            if (mInstr.mOutRegs[i] >= Offset::vecOffset && mInstr.mOutRegs[i] != Offset::ccOffset)
            {
                dpressed_input->read((char *)&val, sizeof(val));
                mInstr.mOutRegsValues.push_back(val);
                if (val != 0)
                    mRemainingPieces++;
            }
        }

        // Memsize has to be adjusted as it is giving only the access size for one register.
        mInstr.mMemSize = mInstr.mMemSize * std::max(1lu, (long unsigned)mInstr.mNumOutRegs);
        mSizeFactor = mRemainingPieces;

        // Trace INT instructions with 0 outputs are generally CMP, so we mark them as producing the flag register
        // The trace does not have the value of the flag register, though
        if (mInstr.mType == aluInstClass && mInstr.mOutRegs.size() == 0)
        {
            mInstr.mOutRegs.push_back(Offset::ccOffset);
            mInstr.mOutRegsValues.push_back(0xdeadbeef);
            mInstr.mNumOutRegs++;
        }
        else if (mInstr.mType == condBranchInstClass && mInstr.mInRegs.size() == 0)
        {
            mInstr.mInRegs.push_back(Offset::ccOffset);
            mInstr.mNumInRegs++;
        }

        nInstr++;

        if (nInstr % 100000 == 0)
            std::cout << nInstr << " instrs " << std::endl;

        return true;
    }
};

#ifdef CHAMPSIM_FORMAT

#define NUM_INSTR_DESTINATIONS      2
#define NUM_INSTR_SOURCES           4

/* compile with c++14 or above and we should be fine with not initializing
the struct members as they'll be initialized to 0. For lower versions that's
not true
*/
typedef struct trace_instr_format {
    unsigned long long int ip;  // instruction pointer (program counter) value

    uint8_t is_branch;    // is this branch
    uint8_t branch_taken; // if so, is this taken

    uint8_t destination_registers[NUM_INSTR_DESTINATIONS];
    uint8_t source_registers[NUM_INSTR_SOURCES];

    unsigned long long int destination_memory[NUM_INSTR_DESTINATIONS];
    unsigned long long int source_memory[NUM_INSTR_SOURCES];
} trace_instr_format_t;

void formatToChampSim(CVPTraceReader::Instr mInstr, FILE *out, FILE *mem_fp,                           FILE *values_fp) {
    trace_instr_format_t instr_champsim;

    instr_champsim.ip = mInstr.mPc;

    /* Where does ChampSim expect branch target address?
    * Champsim doesn't have any explicit branch_target field. Instead,
    * it looks to me that it reads the next instruction before operating on
    * the current one and then, if the current is instruction is a taken branch
    * it assigns to the internal branch_target field the IP (PC) of the next
    * instruction in the trace file, i.e. it assumes that whatever instr is
    * next in the trace file that should be the next instruction in the flow -
    * 
    * So, if that is not the case in the CVP trace file, we should not write
    * to the champsim trace until we see the instruction with the
    *           IP (PC) == mInstr.mTarget ?
    */
    uint8_t isBranch = (mInstr.mType == InstClass::condBranchInstClass) ||
                    (mInstr.mType == InstClass::uncondDirectBranchInstClass) ||
                    (mInstr.mType == InstClass::uncondIndirectBranchInstClass);
    instr_champsim.is_branch = isBranch;
    instr_champsim.branch_taken = ((mInstr.mType == InstClass::uncondDirectBranchInstClass) ||
                    (mInstr.mType == InstClass::uncondIndirectBranchInstClass)) ||
                    (mInstr.mType == InstClass::condBranchInstClass & mInstr.mTaken == 1) ? 0x1 : 0x0;
 
	if(mInstr.mType == InstClass::condBranchInstClass)
	{
		instr_champsim.source_registers[0] = REG_INSTRUCTION_POINTER;
		instr_champsim.source_registers[1] = REG_FLAGS;
		instr_champsim.destination_registers[0] = REG_INSTRUCTION_POINTER;
	}
	else if(mInstr.mType == InstClass::uncondIndirectBranchInstClass)
	{
		if(mInstr.mNumOutRegs == 1 && (mInstr.mOutRegs[0] == 30) && (mInstr.mOutRegsValues[0] == (mInstr.mPc + 4))) //Indirect Call Instruction
		{
			instr_champsim.source_registers[0] = REG_STACK_POINTER;
			instr_champsim.source_registers[1] = REG_INSTRUCTION_POINTER;
			if(mInstr.mNumInRegs > 0)
				instr_champsim.source_registers[2] = mInstr.mInRegs[0];
			else
				instr_champsim.source_registers[2] = 104; //Dummy Register
			instr_champsim.destination_registers[0] = REG_STACK_POINTER;
			instr_champsim.destination_registers[1] = REG_INSTRUCTION_POINTER;
		}
		else if(mInstr.mNumInRegs == 1 && (mInstr.mInRegs[0] == 30)) //Return Instruction
		{
			instr_champsim.source_registers[0] = REG_STACK_POINTER;
			instr_champsim.destination_registers[0] = REG_STACK_POINTER;
			instr_champsim.destination_registers[1] = REG_INSTRUCTION_POINTER;	
		}
		else //Indirect Branch
		{
			instr_champsim.destination_registers[0] = REG_INSTRUCTION_POINTER;
			for(int i = 0; i < NUM_INSTR_SOURCES; i++)
			{
				instr_champsim.source_registers[i] = 0;
				if(i < mInstr.mNumInRegs)
					instr_champsim.source_registers[i] = mInstr.mInRegs[i];
			}
		}
	}
	else if(mInstr.mType == InstClass::uncondDirectBranchInstClass)
	{
		if(mInstr.mNumOutRegs == 1 && (mInstr.mOutRegs[0] == 30) && (mInstr.mOutRegsValues[0] == (mInstr.mPc + 4))) //Direct Call Instruction
		{
			instr_champsim.source_registers[0] = REG_STACK_POINTER;
			instr_champsim.source_registers[1] = REG_INSTRUCTION_POINTER;
			instr_champsim.destination_registers[0] = REG_STACK_POINTER;
			instr_champsim.destination_registers[1] = REG_INSTRUCTION_POINTER;
		}
		else if(mInstr.mNumInRegs == 1 && (mInstr.mInRegs[0] == 30)) //Return Instruction
		{
			instr_champsim.source_registers[0] = REG_STACK_POINTER;
			instr_champsim.destination_registers[0] = REG_STACK_POINTER;
			instr_champsim.destination_registers[1] = REG_INSTRUCTION_POINTER;	
		}
		else //Direct Jump
		{
			instr_champsim.destination_registers[0] = REG_INSTRUCTION_POINTER;
		}
	}
	else
	{
			for (int i=0; i<NUM_INSTR_SOURCES; i++) {
				instr_champsim.source_registers[i] = 0;
				if (i < mInstr.mNumInRegs) {
					instr_champsim.source_registers[i] = mInstr.mInRegs[i];
				}
			}

			for (int i=0; i<NUM_INSTR_DESTINATIONS; i++) {
				instr_champsim.destination_registers[i] = 0;

				if (i < mInstr.mNumOutRegs) {
                	instr_champsim.destination_registers[i] = mInstr.mOutRegs[i];
        			}
    		}
	}

    /* only one source/destination memory value is required, right? 
    * Since we can have only one memory operation per instruction?
    */
    for (int i=0; i<NUM_INSTR_SOURCES; i++) {
        instr_champsim.source_memory[i] = (i == 0 &&
            mInstr.mType == InstClass::loadInstClass) ? mInstr.mEffAddr : 0;
    }

    for (int i=0; i<NUM_INSTR_DESTINATIONS; i++) {
        instr_champsim.destination_memory[i] = (i == 0 &&
            mInstr.mType == InstClass::storeInstClass) ? mInstr.mEffAddr : 0;
    }

    /* Write register values as well. - skip branch instructions */
    /*if (!isBranch) {
        fwrite(&mInstr.mPc, sizeof(mInstr.mPc), 1, values_fp);
        fwrite(&numOutRegs, sizeof(numOutRegs), 1, values_fp);
        if (numOutRegs > 1) { // are there any other instructions? - no, only load pairs
            std::cout << mInstr.mPc << std::endl;
        }
        if (numOutRegs > 0) {
            for (int i=0; i<numOutRegs; i++) {
                fwrite(&mInstr.mOutRegs[i], sizeof(mInstr.mOutRegs[i]), 1, values_fp);
                fwrite(&mInstr.mOutRegsValues[i], sizeof(mInstr.mOutRegsValues[i]),
                                                                        1, values_fp);
            }
        }
    }*/
    

#if INSTR_MEMORY == 1
    if (mInstr.mType == InstClass::loadInstClass ||
                mInstr.mType == InstClass::storeInstClass) {
        fprintf(mem_fp, "%#x\n", mInstr.mPc);
    }
#endif

#if INSTR_WRITE_TEXT == 0
    fwrite(&instr_champsim, sizeof(trace_instr_format_t), 1, out);
#else
    fprintf(out, "0x%llx\n", instr_champsim.ip);
    fprintf(out, "0x%#x\n", instr_champsim.is_branch);
    fprintf(out, "0x%#x\n", instr_champsim.branch_taken);
    for (int i=0; i<NUM_INSTR_DESTINATIONS; i++) {
        fprintf(out, "0x%#x\n", instr_champsim.destination_registers[i]);
    }

    for (int i=0; i<NUM_INSTR_SOURCES; i++) {
        fprintf(out, "0x%#x\n", instr_champsim.source_registers[i]);
    }

    for (int i=0; i<NUM_INSTR_DESTINATIONS; i++) {
        fprintf(out, "0x%llx\n", instr_champsim.destination_memory[i]);
    }

    for (int i=0; i<NUM_INSTR_SOURCES; i++) {
        fprintf(out, "0x%llx\n", instr_champsim.source_memory[i]);
    }
#endif

}

#endif


int main(int argc, char **argv) {
    std::cout << "CVP --> ChampSim trace converter" << std::endl;
    char *name, *values_name;
    FILE *out, *mem_fp, *values_fp;

    if (argc < 2) {
        std::cout << "insufficient arguments - where's your trace?" << std::endl;
        exit(1);
    }
    if (argc == 3) {
        name = argv[1];
    } else {
        name = "trace.out";
    }
    if (argc == 4) {
        values_name = argv[2];
    } else {
        values_name = "trace_values.out";
    }

#if INSTR_WRITE_TEXT == 0
        out = fopen(name, "wb"); //write binary
        values_fp = fopen(values_name, "wb");
#else
        out = fopen(name, "w"); //write text
#endif

#if INSTR_MEMORY == 1
    mem_fp = fopen("CVP_Memory_PC.txt","w");
#endif

    std::cout << argv[1] << std::endl;
    CVPTraceReader reader(argv[1]);
    
    while(reader.readInstr()) {
     //   reader.mInstr.printInstr();
#ifdef CHAMPSIM_FORMAT
        formatToChampSim(reader.mInstr, out, mem_fp, values_fp);
#endif
    }

    fclose(out);
    fclose(values_fp);

#if INSTR_MEMORY == 1
    fclose(mem_fp);
#endif
}
