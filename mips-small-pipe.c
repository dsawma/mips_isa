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

    /* only used in EX stage */
    int i, rs_ex, rt_ex;
    int de_mem, de_wb, de_end;
    int stage_ex[3];
    int dests[3];

    memset(&new, 0, sizeof(state_t));

    while (1)
    {

        printState(state);

        /* copy everything so all we have to do is make changes.
           (this is primarily for the memory and reg arrays) */
        memcpy(&new, state, sizeof(state_t));

        new.cycles++;

        /* --------------------- IF stage --------------------- */

        /* There is a halt from previous IFID-> new IFID should stay the same */
        if (state->pc == state->IFID.pcPlus1 - 4)
        {
            new.IFID.instr = state->IFID.instr;
            new.pc = state->pc;
            new.IFID.pcPlus1 = state->IFID.pcPlus1;
        }

        /* fetch next instruction */
        new.IFID.instr = state->instrMem[state->pc / 4];

        /* If fetched instr was a BRANCH with negative offset, we set new pc to the target address */
        if ((opcode(state->instrMem[state->pc / 4]) == BEQZ_OP) && (offset(state->instrMem[state->pc / 4]) < 0))
        {
            new.pc = (state->IFID.pcPlus1 + offset(state->instrMem[state->pc / 4])) + 4;
            new.IFID.pcPlus1 = state->pc + 4;
        }

        /* fetch sequential instr or if BRANCH positive offset */
        else
        {
            new.pc = state->pc + 4;
            new.IFID.pcPlus1 = state->pc + 4;
        }

        /* --------------------- ID stage --------------------- */

        /* if previous instr was LW -> check for true dependency, if so stall */
        if (opcode(state->IDEX.instr) == LW_OP)
        {
            /* if the instr is REG_REG and either rs or rt is dependent on LW's rt -> stall */
            if (opcode(state->IFID.instr) == REG_REG_OP && (field_r1(state->IFID.instr) == field_r2(state->IDEX.instr) || field_r2(state->IFID.instr) == field_r2(state->IDEX.instr)))
            {
                new.pc = state->pc;
                new.IFID = state->IFID;
                new.IDEX.instr = NOPINSTRUCTION;
                new.IDEX.pcPlus1 = 0;
                new.IDEX.readRegA = 0;
                new.IDEX.readRegB = 0;
                new.IDEX.offset = 0;
            }

            /* else if the instr rs is dependent on LW's rt -> stall */
            else if (field_r1(state->IFID.instr) == field_r2(state->IDEX.instr))
            {
                new.pc = state->pc;
                new.IFID = state->IFID;
                new.IDEX.instr = NOPINSTRUCTION;
                new.IDEX.pcPlus1 = 0;
                new.IDEX.readRegA = 0;
                new.IDEX.readRegB = 0;
                new.IDEX.offset = 0;
            }

            /* not dependent */
            else
            {
                new.IDEX.instr = state->IFID.instr;
                new.IDEX.pcPlus1 = state->IFID.pcPlus1;
                new.IDEX.readRegA = state->reg[field_r1(state->IFID.instr)];
                new.IDEX.readRegB = state->reg[field_r2(state->IFID.instr)];
                if (opcode(state->IFID.instr) == LW_OP || opcode(state->IFID.instr) == SW_OP || opcode(state->IFID.instr) == BEQZ_OP || opcode(state->IFID.instr) == ADDI_OP)
                {
                    new.IDEX.offset = offset(state->IFID.instr);
                }
                else
                {
                    new.IDEX.offset = field_imm(state->IFID.instr);
                }
            }
        }

        /* when the previous instr is not LW */
        else
        {
            new.IDEX.instr = state->IFID.instr;
            new.IDEX.pcPlus1 = state->IFID.pcPlus1;
            new.IDEX.readRegA = state->reg[field_r1(state->IFID.instr)];
            new.IDEX.readRegB = state->reg[field_r2(state->IFID.instr)];

            if (opcode(state->IFID.instr) == LW_OP || opcode(state->IFID.instr) == SW_OP || opcode(state->IFID.instr) == BEQZ_OP || opcode(state->IFID.instr) == ADDI_OP)
            {
                new.IDEX.offset = offset(state->IFID.instr);
            }
            else
            {
                new.IDEX.offset = field_imm(state->IFID.instr);
            }
        }

        /* --------------------- EX stage --------------------- */
        rs_ex = 0;
        rt_ex = 0;

        stage_ex[0] = state->EXMEM.instr; /* stage_ex[0] will hold instr in EXMEM reg*/
        stage_ex[1] = state->MEMWB.instr; /* stage_ex[1] will hold instr in MEMWB reg*/
        stage_ex[2] = state->WBEND.instr; /* stage_ex[2] will hold instr in WBEND reg*/

        /* loops through each pipelipe reg from stage_ex, then identifies the destination reg */
        for (i = 0; i < 3; i++)
        {
            switch (opcode(stage_ex[i]))
            {
            case REG_REG_OP:
                dests[i] = field_r3(stage_ex[i]);
                break;
            case ADDI_OP:
            case LW_OP:
                dests[i] = field_r2(stage_ex[i]);
                break;
            default:
                dests[i] = 0;
                break;
            }
        }

        de_mem = dests[0]; /* de_mem holds destination reg from instr EXMEM */
        de_wb = dests[1];  /* de_wb holds destination reg from instr MEMWB */
        de_end = dests[2]; /* de_end holds destination reg from instr WBEND */

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

        /* performs computation based on opcode */
        /* if the destination reg is r0 the aluResult is 0 */
        switch (opcode(state->IDEX.instr))
        {
        case REG_REG_OP:
            switch (func(state->IDEX.instr))
            {
            case ADD_FUNC:
                new.EXMEM.aluResult = rs_ex + rt_ex;
                if (field_r3(state->IDEX.instr) == 0)
                {
                    new.EXMEM.aluResult = 0;
                }
                break;
            case SUB_FUNC:
                new.EXMEM.aluResult = rs_ex - rt_ex;
                if (field_r3(state->IDEX.instr) == 0)
                {
                    new.EXMEM.aluResult = 0;
                }
                break;
            case SLL_FUNC:
                new.EXMEM.aluResult = rs_ex << rt_ex;
                if (field_r3(state->IDEX.instr) == 0)
                {
                    new.EXMEM.aluResult = 0;
                }
                break;
            case SRL_FUNC:
                new.EXMEM.aluResult = rs_ex >> rt_ex;
                if (field_r3(state->IDEX.instr) == 0)
                {
                    new.EXMEM.aluResult = 0;
                }
                break;
            case AND_FUNC:
                new.EXMEM.aluResult = rs_ex & rt_ex;
                if (field_r3(state->IDEX.instr) == 0)
                {
                    new.EXMEM.aluResult = 0;
                }
                break;
            case OR_FUNC:
                new.EXMEM.aluResult = rs_ex | rt_ex;
                if (field_r3(state->IDEX.instr) == 0)
                {
                    new.EXMEM.aluResult = 0;
                }
                break;
            default:
                new.EXMEM.aluResult = 0;
            }

            new.EXMEM.instr = state->IDEX.instr;
            new.EXMEM.readRegB = rt_ex;
            if (field_r2(state->IDEX.instr) == 0)
            {
                new.EXMEM.readRegB = 0;
            }
            break;
        case ADDI_OP:
            if (field_r1(state->IDEX.instr) == 0)
            {
                rs_ex = 0;
            }

            new.EXMEM.aluResult = rs_ex + state->IDEX.offset;
            if (field_r2(state->IDEX.instr) == 0)
            {
                new.EXMEM.aluResult = 0;
            }
            new.EXMEM.instr = state->IDEX.instr;
            new.EXMEM.readRegB = state->IDEX.readRegB;
            break;
        case LW_OP:
            if (field_r1(state->IDEX.instr) == 0)
            {
                rs_ex = 0;
            }

            new.EXMEM.aluResult = rs_ex + state->IDEX.offset;
            if (field_r2(state->IDEX.instr) == 0)
            {
                new.EXMEM.aluResult = 0;
            }
            new.EXMEM.instr = state->IDEX.instr;
            ;
            new.EXMEM.readRegB = state->IDEX.readRegB;
            break;

        case SW_OP:
            /* we check if its s reg is from the destination of a lw instr in EXMEM  */
            /* if it is we stall */
            if (field_r1(state->IDEX.instr) == 0)
            {
                rs_ex = 0;
            }

            new.EXMEM.aluResult = rs_ex + state->IDEX.offset;

            if (field_r2(state->IDEX.instr) == 0)
            {
                new.EXMEM.aluResult = 0;
            }

            new.EXMEM.instr = state->IDEX.instr;
            new.EXMEM.readRegB = rt_ex;
            break;
        case BEQZ_OP:

            /* the offset is positive, branch taken OR offset is negative, branch is not-taken */
            /* If rs is r0, rs_ex should stay 0 */
            if (field_r1(state->IDEX.instr) == 0)
            {
                rs_ex = 0;
            }
            if ((state->IDEX.offset > -1 && rs_ex == 0) || (state->IDEX.offset < 0 && rs_ex != 0))
            {
                new.EXMEM.aluResult = state->IDEX.pcPlus1 + state->IDEX.offset;
                new.pc = state->IDEX.pcPlus1 + state->IDEX.offset;

                new.IFID.instr = NOPINSTRUCTION;
                new.IFID.pcPlus1 = 0;

                new.IDEX.instr = NOPINSTRUCTION;
                new.IDEX.pcPlus1 = 0;
                new.IDEX.readRegA = 0;
                new.IDEX.readRegB = 0;
                new.IDEX.offset = 0;
            }
            else
            {
                new.EXMEM.aluResult = state->IDEX.pcPlus1 + state->IDEX.offset;
            }
            new.EXMEM.instr = state->IDEX.instr;
            new.EXMEM.readRegB = state->IDEX.readRegB;

            break;
        case HALT_OP:
            new.EXMEM.instr = state->IDEX.instr;
            new.EXMEM.aluResult = 0;
            new.EXMEM.readRegB = 0;
            break;
        }

        /* --------------------- MEM stage --------------------- */

        /* if its LW instr -> writeData is dataMem of the aluResult */
        /* if its SW instr -> readRegB is saved into dataMem of aluResult*/
        switch (opcode(state->EXMEM.instr))
        {
        case LW_OP:
            if (0 == field_r2(state->EXMEM.instr))
            {
                new.MEMWB.writeData = 0;
            }
            else
            {
                new.MEMWB.writeData = state->dataMem[state->EXMEM.aluResult / 4];
            }
            break;
        case SW_OP:
            new.dataMem[state->EXMEM.aluResult / 4] = state->EXMEM.readRegB;
            new.MEMWB.writeData = state->EXMEM.readRegB;
            break;
        default:
            new.MEMWB.writeData = state->EXMEM.aluResult;
        }
        new.MEMWB.instr = state->EXMEM.instr;

        /* --------------------- WB stage --------------------- */

        /* update register file only if the destination is not r0 */
        switch (opcode(state->MEMWB.instr))
        {
        case REG_REG_OP:
            if (field_r3(state->MEMWB.instr) != 0)
            {
                new.reg[field_r3(state->MEMWB.instr)] = state->MEMWB.writeData;
            }
            new.WBEND.instr = state->MEMWB.instr;
            new.WBEND.writeData = state->MEMWB.writeData;
            break;
        case ADDI_OP:
        case LW_OP:
            if (field_r2(state->MEMWB.instr) != 0)
            {
                new.reg[field_r2(state->MEMWB.instr)] = state->MEMWB.writeData;
            }
            new.WBEND.instr = state->MEMWB.instr;
            new.WBEND.writeData = state->MEMWB.writeData;
            break;
        default:
            new.WBEND.instr = state->MEMWB.instr;
            new.WBEND.writeData = state->MEMWB.writeData;
        }

        /* --------------------- end stage --------------------- */

        if (opcode(state->MEMWB.instr) == HALT_OP)
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
