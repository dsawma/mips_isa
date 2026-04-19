# mips_isa
A simplified simulation of a MIPS32 instruction set architecture

## Motivation 
In conjunction with my computer architecture class, I decided to build a MIPS32 ISA to solidify my understanding of CPUs. This was a class project, but I plan to incorporate some new add on instructions. 

### Assembler 
An assembler is provided as well as sample mips assembly language files in the tests folder. The assembler simply translates mips files into machine files. 

### MIPS32 Simulator
The MIPS32 simulator utilizes pipelining where forwarding and stalling is possible when control and data hazards occur. The output prints each cycle and what instructions are present in each pipeline register. It also displays an array of data memory and an array of registers. Stalls are displayed with NOPInstruction. 

Pipeline registers:
- IFID
- IDEX
- EXMEM
- MEMWB
- WBEND

Instructions:
- LW 
- SW
- BEQZ
- ADDI 
- ADD
- SUB
- SLL 
- SRL
- AND
- OR 
- HALT

## Setup 
### Create Assembler(asm.c) and Simulator(mips-small-pipe.c) executable file with Makefile
make asm
make sim-pipe

### Transalte mips file into machine file with asm
./asm [.mips file] [.machine file]

Sample mips files are in tests folder 

### Run the Simulator
./sim-pipe [.machine file]