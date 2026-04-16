#include "mips-small-pipe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/************************************************************/
int main(int argc, char *argv[])
{
    short i;
    char line[MAXLINELENGTH];
    state_t state;
    FILE *filePtr;

    if (argc != 2)
    {
        printf("error: usage: %s <machine-code file>\n", argv[0]);
        return 1;
    }

    memset(&state, 0, sizeof(state_t));

    state.pc = state.cycles = 0;
    state.IFID.instr = state.IDEX.instr = state.EXMEM.instr = state.MEMWB.instr =
        state.WBEND.instr = NOPINSTRUCTION; /* nop */

    /* read machine-code file into instruction/data memory (starting at address 0)
     */

    filePtr = fopen(argv[1], "r");
    if (filePtr == NULL)
    {
        printf("error: can't open file %s\n", argv[1]);
        perror("fopen");
        exit(1);
    }

    for (state.numMemory = 0; fgets(line, MAXLINELENGTH, filePtr) != NULL;
         state.numMemory++)
    {
        if (sscanf(line, "%x", &state.dataMem[state.numMemory]) != 1)
        {
            printf("error in reading address %d\n", state.numMemory);
            exit(1);
        }
        state.instrMem[state.numMemory] = state.dataMem[state.numMemory];
        printf("memory[%d]=%x\n", state.numMemory, state.dataMem[state.numMemory]);
    }

    printf("%d memory words\n", state.numMemory);

    printf("\tinstruction memory:\n");
    for (i = 0; i < state.numMemory; i++)
    {
        printf("\t\tinstrMem[ %d ] = ", i);
        printInstruction(state.instrMem[i]);
    }

    run(&state);

    return 0;
}
/************************************************************/

/************************************************************/
void run(Pstate state)
{
    state_t new;
    int op_ex, op_mem, op_wb, rs, rt_wb, rt_id, rd, func_ex, result, haz, op_id;
    int i, rs_ex, rt_ex;
    int de_mem, de_wb, de_end;
    int stage_ex[3];
    int dests2[3];

    memset(&new, 0, sizeof(state_t));

    while (1)
    {

        printState(state);
        printInstruction(state->EXMEM.instr);
        printInstruction(state->MEMWB.instr);
        printInstruction(state->WBEND.instr);
        printf("cycle: %d, EXMEM.aluResult =%d, MEMWB.writeData = %d, WBEND.writeData = %d\n", state->cycles, state->EXMEM.aluResult, state->MEMWB.writeData, state->WBEND.writeData);

        /* copy everything so all we have to do is make changes.
           (this is primarily for the memory and reg arrays) */
        memcpy(&new, state, sizeof(state_t));

        new.cycles++;

        /* --------------------- IF stage --------------------- */
        if (state->pc == state->IFID.pcPlus1 - 4)
        {
            new.IFID.instr = state->IFID.instr;
            new.pc = state->pc;
            new.IFID.pcPlus1 = state->IFID.pcPlus1;
        }
        else
        {
            new.IFID.instr = state->instrMem[state->pc / 4];
            new.pc = state->pc + 4;
            new.IFID.pcPlus1 = state->pc + 4;
        }

        /* --------------------- ID stage --------------------- */
        haz = 0;
        rs = field_r1(state->IFID.instr);
        rt_id = field_r2(state->IFID.instr);
        op_id = opcode(state->IFID.instr);

        new.IDEX.instr = state->IFID.instr;
        new.IDEX.pcPlus1 = state->IFID.pcPlus1;
        new.IDEX.readRegA = state->reg[rs];
        new.IDEX.readRegB = state->reg[rt_id];
        if (op_id == LW_OP || op_id == SW_OP || op_id == BEQZ_OP)
        {
            new.IDEX.offset = offset(state->IFID.instr);
        }
        else
        {
            new.IDEX.offset = field_imm(state->IFID.instr);
        }

        /* --------------------- EX stage --------------------- */
        result = 0;
        op_ex = opcode(state->IDEX.instr);
        func_ex = func(state->IDEX.instr);
        rs_ex = 0;
        rt_ex = 0;
        haz = 0;

        stage_ex[0] = state->EXMEM.instr; /* stage_ex[0] will hold isntr in EXMEM reg*/
        stage_ex[1] = state->MEMWB.instr; /* stage_ex[1] will hold isntr in MEMWB reg*/
        stage_ex[2] = state->WBEND.instr; /* stage_ex[2] will hold isntr in WBEND reg*/

        /* loops through each pipelipe reg from stage_ex, then identifies the destination reg */
        for (i = 0; i < 3; i++)
        {
            switch (opcode(stage_ex[i]))
            {
            case REG_REG_OP:
                dests2[i] = field_r3(stage_ex[i]);
                break;
            case ADDI_OP:
            case LW_OP:
                dests2[i] = field_r2(stage_ex[i]);
                break;
            default:
                dests2[i] = 0;
                break;
            }
        }

        de_mem = dests2[0]; /* de_mem holds destination reg from instr EXMEM */
        de_wb = dests2[1];  /* de_wb holds destination reg from instr MEMWB */
        de_end = dests2[2]; /* de_end holds destination reg from instr WBEND */

        /* rs value is forwarded if its in the pipeline regs, else its from its own readRegA */

        if (field_r1(state->IDEX.instr) == de_mem)
        {
            rs_ex = state->EXMEM.aluResult;
        }
        else if (field_r1(state->IDEX.instr) == de_wb)
        {
            rs_ex = state->MEMWB.writeData;
        }
        else if (field_r1(state->IDEX.instr) == de_end)
        {
            rs_ex = state->WBEND.writeData;
        }
        else
        {
            rs_ex = state->IDEX.readRegA;
        }

        /* rt value is forwarded if its in the pipeline regs, else its from its own readRegB */

        if (field_r2(state->IDEX.instr) == de_mem)
        {
            rt_ex = state->EXMEM.aluResult;
        }
        else if (field_r2(state->IDEX.instr) == de_wb)
        {
            rt_ex = state->MEMWB.writeData;
        }
        else if (field_r2(state->IDEX.instr) == de_end)
        {
            rt_ex = state->WBEND.writeData;
        }
        else
        {
            rt_ex = state->IDEX.readRegB;
        }

        if (op_ex == REG_REG_OP)
        {
            /* if the instr is a reg_reg_op then we check if its s or t reg is from the destination of a lw instr in EXMEM  */
            /* if it is we stall */
            if ((opcode(stage_ex[0]) == LW_OP && de_mem == field_r1(state->IDEX.instr)) || (opcode(stage_ex[0]) == LW_OP && de_mem == field_r2(state->IDEX.instr)))
            {
                haz = 1;
            }
            else
            {

                if (func_ex == ADD_FUNC)
                {
                    result = rs_ex + rt_ex;
                    if (field_r3(state->IDEX.instr) == 0)
                    {
                        result = 0;
                    }
                }
                else if (func_ex == SUB_FUNC)
                {
                    result = rs_ex - rt_ex;
                    if (field_r3(state->IDEX.instr) == 0)
                    {
                        result = 0;
                    }
                }
                else if (func_ex == SLL_FUNC)
                {
                    result = rt_ex << ((state->IDEX.instr >> 6) & 0x1F);
                    if (field_r3(state->IDEX.instr) == 0)
                    {
                        result = 0;
                    }
                }
                else if (func_ex == SRL_FUNC)
                {
                    result = rt_ex >> ((state->IDEX.instr >> 6) & 0x1F);
                    if (field_r3(state->IDEX.instr) == 0)
                    {
                        result = 0;
                    }
                }
                else if (func_ex == AND_FUNC)
                {
                    result = rs_ex & rt_ex;
                    if (field_r3(state->IDEX.instr) == 0)
                    {
                        result = 0;
                    }
                }
                else if (func_ex == OR_FUNC)
                {
                    result = rs_ex | rt_ex;
                    if (field_r3(state->IDEX.instr) == 0)
                    {
                        result = 0;
                    }
                }
            }
        }
        else if (op_ex == ADDI_OP || op_ex == LW_OP || op_ex == SW_OP)
        {
            /* we check if its s reg is from the destination of a lw instr in EXMEM  */
            /* if it is we stall */
            if ((opcode(stage_ex[0]) == LW_OP && de_mem == field_r1(state->IDEX.instr)))
            {
                haz = 1;
            }
            else
            {
                result = rs_ex + state->IDEX.offset;
            }
        }
        else if (op_ex == BEQZ_OP)
        {
            /* we check if its s reg is from the destination of a lw instr in EXMEM */
            /* if it is we stall */
            if ((opcode(stage_ex[0]) == LW_OP && de_mem == field_r1(state->IDEX.instr)))
            {
                haz = 1;
            }
            else
            {
                result = state->IDEX.pcPlus1 + (state->IDEX.offset);
                new.pc = result;
                if (state->IDEX.readRegA != 0)
                {
                    result = state->IDEX.pcPlus1;
                }
            }
        }
        else
        {
            result = 0;
        }
        if (haz)
        {
            new.IDEX = state->IDEX;
            new.EXMEM.instr = NOPINSTRUCTION;
            new.EXMEM.aluResult = 0;
            new.EXMEM.readRegB = 0;
        }
        else
        {
            if (opcode(state->IDEX.instr) == HALT_OP)
            {
                new.EXMEM.instr = state->IDEX.instr;
                new.EXMEM.aluResult = result;
                new.EXMEM.readRegB = 0;
            }
            else if (opcode(state->IDEX.instr == SW_OP))
            {
                new.EXMEM.instr = state->IDEX.instr;
                new.EXMEM.aluResult = result;
                new.EXMEM.readRegB = rs;
            }
            else
            {
                new.EXMEM.instr = state->IDEX.instr;
                new.EXMEM.aluResult = result;
                new.EXMEM.readRegB = rt_ex;
            }
        }

        /* --------------------- MEM stage --------------------- */
        op_mem = opcode(state->EXMEM.instr);
        if (op_mem == REG_REG_OP)
        {
            if (0 == field_r3(state->EXMEM.instr))
            {
                new.MEMWB.writeData = 0;
            }
            else
            {
                new.MEMWB.writeData = state->EXMEM.aluResult;
            }
        }
        else if (op_mem == ADDI_OP)
        {
            if (0 == field_r2(state->EXMEM.instr))
            {
                new.MEMWB.writeData = 0;
            }
            else
            {
                new.MEMWB.writeData = state->EXMEM.aluResult;
            }
        }
        else if (op_mem == LW_OP)
        {
            if (0 == field_r2(state->EXMEM.instr))
            {
                new.MEMWB.writeData = 0;
            }
            else
            {
                new.MEMWB.writeData = state->dataMem[state->EXMEM.aluResult / 4];
            }
        }
        else if (op_mem == SW_OP)
        {
            new.dataMem[state->EXMEM.aluResult / 4] = state->EXMEM.readRegB;
            new.MEMWB.writeData = state->EXMEM.readRegB;
        }
        else if (op_mem == BEQZ_OP)
        {
            new.MEMWB.writeData = 0;
        }
        else
        {
            new.MEMWB.writeData = state->EXMEM.aluResult;
        }
        new.MEMWB.instr = state->EXMEM.instr;

        /* --------------------- WB stage --------------------- */
        op_wb = opcode(state->MEMWB.instr);

        if (op_wb == REG_REG_OP)
        {
            rd = field_r3(state->MEMWB.instr);
            if (rd != 0)
            {
                new.reg[rd] = state->MEMWB.writeData;
            }
        }
        else if (op_wb == ADDI_OP || op_wb == LW_OP)
        {
            rt_wb = field_r2(state->MEMWB.instr);
            if (rt_wb != 0)
            {
                new.reg[rt_wb] = state->MEMWB.writeData;
            }
        }
        new.WBEND.instr = state->MEMWB.instr;
        new.WBEND.writeData = state->MEMWB.writeData;
        /* --------------------- end stage --------------------- */

        if (opcode(new.WBEND.instr) == HALT_OP)
        {
            printf("machine halted\n");
            printf("total of %d cycles executed\n", new.cycles - 1);
            exit(0);
        }
        /* transfer new state into current state */
        memcpy(state, &new, sizeof(state_t));
    }
}
/************************************************************/

/************************************************************/
int opcode(int instruction) { return (instruction >> OP_SHIFT) & OP_MASK; }
/************************************************************/

/************************************************************/
int func(int instruction) { return (instruction & FUNC_MASK); }
/************************************************************/

/************************************************************/
int field_r1(int instruction) { return (instruction >> R1_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r2(int instruction) { return (instruction >> R2_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_r3(int instruction) { return (instruction >> R3_SHIFT) & REG_MASK; }
/************************************************************/

/************************************************************/
int field_imm(int instruction) { return (instruction & IMMEDIATE_MASK); }
/************************************************************/

/************************************************************/
int offset(int instruction)
{
    /* only used for lw, sw, beqz */
    return convertNum(field_imm(instruction));
}
/************************************************************/

/************************************************************/
int convertNum(int num)
{
    /* convert a 16 bit number into a 32-bit Sun number */
    if (num & 0x8000)
    {
        num -= 65536;
    }
    return (num);
}
/************************************************************/

/************************************************************/
void printState(Pstate state)
{
    short i;
    printf("@@@\nstate before cycle %d starts\n", state->cycles);
    printf("\tpc %d\n", state->pc);

    printf("\tdata memory:\n");
    for (i = 0; i < state->numMemory; i++)
    {
        printf("\t\tdataMem[ %d ] %d\n", i, state->dataMem[i]);
    }
    printf("\tregisters:\n");
    for (i = 0; i < NUMREGS; i++)
    {
        printf("\t\treg[ %d ] %d\n", i, state->reg[i]);
    }
    printf("\tIFID:\n");
    printf("\t\tinstruction ");
    printInstruction(state->IFID.instr);
    printf("\t\tpcPlus1 %d\n", state->IFID.pcPlus1);
    printf("\tIDEX:\n");
    printf("\t\tinstruction ");
    printInstruction(state->IDEX.instr);
    printf("\t\tpcPlus1 %d\n", state->IDEX.pcPlus1);
    printf("\t\treadRegA %d\n", state->IDEX.readRegA);
    printf("\t\treadRegB %d\n", state->IDEX.readRegB);
    printf("\t\toffset %d\n", state->IDEX.offset);
    printf("\tEXMEM:\n");
    printf("\t\tinstruction ");
    printInstruction(state->EXMEM.instr);
    printf("\t\taluResult %d\n", state->EXMEM.aluResult);
    printf("\t\treadRegB %d\n", state->EXMEM.readRegB);
    printf("\tMEMWB:\n");
    printf("\t\tinstruction ");
    printInstruction(state->MEMWB.instr);
    printf("\t\twriteData %d\n", state->MEMWB.writeData);
    printf("\tWBEND:\n");
    printf("\t\tinstruction ");
    printInstruction(state->WBEND.instr);
    printf("\t\twriteData %d\n", state->WBEND.writeData);
}
/************************************************************/

/************************************************************/
void printInstruction(int instr)
{

    if (opcode(instr) == REG_REG_OP)
    {

        if (func(instr) == ADD_FUNC)
        {
            print_rtype(instr, "add");
        }
        else if (func(instr) == SLL_FUNC)
        {
            print_rtype(instr, "sll");
        }
        else if (func(instr) == SRL_FUNC)
        {
            print_rtype(instr, "srl");
        }
        else if (func(instr) == SUB_FUNC)
        {
            print_rtype(instr, "sub");
        }
        else if (func(instr) == AND_FUNC)
        {
            print_rtype(instr, "and");
        }
        else if (func(instr) == OR_FUNC)
        {
            print_rtype(instr, "or");
        }
        else
        {
            printf("data: %d\n", instr);
        }
    }
    else if (opcode(instr) == ADDI_OP)
    {
        print_itype(instr, "addi");
    }
    else if (opcode(instr) == LW_OP)
    {
        print_itype(instr, "lw");
    }
    else if (opcode(instr) == SW_OP)
    {
        print_itype(instr, "sw");
    }
    else if (opcode(instr) == BEQZ_OP)
    {
        print_itype(instr, "beqz");
    }
    else if (opcode(instr) == HALT_OP)
    {
        printf("halt\n");
    }
    else
    {
        printf("data: %d\n", instr);
    }
}
/************************************************************/

/************************************************************/
void print_rtype(int instr, const char *name)
{
    printf("%s %d %d %d\n", name, field_r3(instr), field_r1(instr),
           field_r2(instr));
}
/************************************************************/

/************************************************************/
void print_itype(int instr, const char *name)
{
    printf("%s %d %d %d\n", name, field_r2(instr), field_r1(instr),
           offset(instr));
}
