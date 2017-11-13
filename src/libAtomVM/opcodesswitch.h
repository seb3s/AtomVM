/***************************************************************************
 *   Copyright 2017 by Davide Bettio <davide@uninstall.it>                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#include "Module.h"

#include <assert.h>

#include "debug.h"
#include "utils.h"

#ifndef TRACE
    #ifdef ENABLE_TRACE
        #define TRACE printf
    #else
        #define TRACE(...)
    #endif
#endif

#define COMPACT_SMALLINT4 1
#define COMPACT_ATOM 2
#define COMPACT_XREG 3
#define COMPACT_YREG 4
#define COMPACT_LARGE_INTEGER 9

#define COMPACT_LARGE_IMM_MASK 0x18
#define COMPACT_11BITS_VALUE 0x8
#define COMPACT_NBITS_VALUE 0x18

#ifdef IMPL_CODE_LOADER
#define DECODE_COMPACT_TERM(dest_term, code_chunk, index, next_operand_offset) \
{                                                                                       \
    uint8_t first_byte = (code_chunk[(index)]);                                         \
    switch (first_byte & 0xF) {                                                         \
        case COMPACT_SMALLINT4:                                                         \
            next_operand_offset = 1;                                                    \
            break;                                                                      \
                                                                                        \
        case COMPACT_ATOM:                                                              \
            next_operand_offset = 1;                                                    \
            break;                                                                      \
                                                                                        \
        case COMPACT_XREG:                                                              \
            next_operand_offset = 1;                                                    \
            break;                                                                      \
                                                                                        \
        case COMPACT_YREG:                                                              \
            next_operand_offset = 1;                                                    \
            break;                                                                      \
                                                                                        \
        case COMPACT_LARGE_INTEGER:                                                     \
            switch (first_byte & COMPACT_LARGE_IMM_MASK) {                              \
                case COMPACT_11BITS_VALUE:                                              \
                    next_operand_offset = 2;                                            \
                    break;                                                              \
                                                                                        \
                default:                                                                \
                    assert((first_byte & 0x30) != COMPACT_LARGE_INTEGER);               \
                    break;                                                              \
            }                                                                           \
            break;                                                                      \
                                                                                        \
        default:                                                                        \
            fprintf(stderr, "unknown compect term type: %i\n", ((first_byte) & 0xF));   \
            abort();                                                                    \
            break;                                                                      \
    }                                                                                   \
}
#endif

#ifdef IMPL_EXECUTE_LOOP
#define DECODE_COMPACT_TERM(dest_term, code_chunk, index, next_operand_offset) \
{                                                                                                                       \
    uint8_t first_byte = (code_chunk[(index)]);                                                                         \
    switch (first_byte & 0xF) {                                                                                         \
        case COMPACT_SMALLINT4:                                                                                         \
            dest_term = term_from_int4(first_byte >> 4);                                                                \
            next_operand_offset = 1;                                                                                    \
            break;                                                                                                      \
                                                                                                                        \
        case COMPACT_ATOM:                                                                                              \
            dest_term = 0;                                                                                              \
            next_operand_offset = 1;                                                                                    \
            break;                                                                                                      \
                                                                                                                        \
        case COMPACT_XREG:                                                                                              \
            dest_term = ctx->x[first_byte >> 4];                                                                        \
            next_operand_offset = 1;                                                                                    \
            break;                                                                                                      \
                                                                                                                        \
        case COMPACT_YREG:                                                                                              \
            dest_term = ctx->stack_frame[first_byte >> 4];                                                              \
            next_operand_offset = 1;                                                                                    \
            break;                                                                                                      \
                                                                                                                        \
        case COMPACT_LARGE_INTEGER:                                                                                     \
            switch (first_byte & COMPACT_LARGE_IMM_MASK) {                                                              \
                case COMPACT_11BITS_VALUE:                                                                              \
                    next_operand_offset = 2;                                                                            \
                    dest_term = term_from_int11(((first_byte & 0xE0) << 3) | code_chunk[(index + 1)]);                  \
                    break;                                                                                              \
                                                                                                                        \
                default:                                                                                                \
                    abort();                                                                                            \
                    break;                                                                                              \
            }                                                                                                           \
            break;                                                                                                      \
                                                                                                                        \
        default:                                                                                                        \
            abort();                                                                                                    \
    }                                                                                                                   \
}
#endif

#define DECODE_LABEL(code_chunk, index, next_operand_offset, label) \
{                                                                                                   \
    uint8_t first_byte = (code_chunk[(index)]);                                                     \
    switch (((first_byte) >> 3) & 0x3) {                                                            \
        case 0:                                                                                     \
        case 2:                                                                                     \
            next_operand_offset = 1;                                                                \
            label = first_byte >> 4;                                                                \
            break;                                                                                  \
                                                                                                    \
        case 1:                                                                                     \
            next_operand_offset = 2;                                                                \
            label = ((first_byte & 0xE0) << 3) | code_chunk[(index + 1)];                           \
            break;                                                                                  \
                                                                                                    \
        default:                                                                                    \
            fprintf(stderr, "Operand not a label: %x, or unsupported encoding\n", (first_byte));    \
            abort();                                                                                \
            break;                                                                                  \
    }                                                                                               \
}

#define NEXT_INSTRUCTION(operands_size) \
    i += 1 + operands_size

#define JUMP_TO_ADDRESS(address) \
    i = ((uint8_t *) (address)) - chunk->code

#define INSTRUCTION_POINTER() \
    ((unsigned long) &chunk->code[i])

#ifdef IMPL_CODE_LOADER
    int read_core_chunk(Module *mod)
#else
    #ifdef IMPL_EXECUTE_LOOP
        int context_execute_loop(Context *ctx, Module *mod, uint8_t *beam_file)
    #else
        #error Need implementation type
    #endif
#endif
{
    CodeChunk *chunk = mod->code;

    unsigned int i = 0;

    while(1) {

        switch (chunk->code[i]) {
            case 1: {
                int label;
                int next_offset;
                DECODE_LABEL(chunk->code, i + 1, next_offset, label)

                TRACE("label/1 label=%i\n", label);

                #ifdef IMPL_CODE_LOADER
                    TRACE("Mark label %i here at %i\n", label, i);
                    module_add_label(mod, label, &chunk->code[i]);
                #endif

                #ifdef EXECUTE_LOOP
                    UNUSED(label)
                #endif

                NEXT_INSTRUCTION(next_offset);
                break;
            }

            case 2: {
                int func_name = chunk->code[i + 1] & 0xF;
                int arity = chunk->code[i + 2] & 0xF;
                int func_id = chunk->code[i + 3] & 0xF;
                int a = chunk->code[i + 1] >> 4;
                int b = chunk->code[i + 2] >> 4;
                int c = chunk->code[i + 3] >> 4;

                TRACE("func_info/3 name=%i (%x), arity=%i (%x), func_id=%i (%x)\n", func_name, a, arity, b, func_id, c);

                NEXT_INSTRUCTION(3);
                break;
            }

            case 3: {
                TRACE("int_call_end!\n");
                return 1;
            }

            case 4: {
                int a_type = chunk->code[i + 1] & 0xF;
                int b_type = chunk->code[i + 2] & 0xF;
                int arity = chunk->code[i + 1] >> 4;
                int label = chunk->code[i + 2] >> 4;

                TRACE("call/2, arity=%i (%x), label=%i (%x)\n", arity, a_type, label, b_type);

                #ifdef IMPL_EXECUTE_LOOP
                    NEXT_INSTRUCTION(2);
                    ctx->cp = INSTRUCTION_POINTER();

                    JUMP_TO_ADDRESS(mod->labels[label]);
                #endif

                #ifdef IMPL_CODE_LOADER
                    NEXT_INSTRUCTION(2);
                #endif

                break;
            }

            case 6: {
                int a_type = chunk->code[i + 1] & 0xF;
                int b_type = chunk->code[i + 2] & 0xF;
                int arity = chunk->code[i + 1] >> 4;
                int label = chunk->code[i + 2] >> 4;

                TRACE("call_only/2, arity=%i (%x), label=%i (%x)\n", arity, a_type, label, b_type);

                #ifdef IMPL_EXECUTE_LOOP
                    NEXT_INSTRUCTION(2);
                    JUMP_TO_ADDRESS(mod->labels[label]);
                #endif

                #ifdef IMPL_CODE_LOADER
                    NEXT_INSTRUCTION(2);
                #endif

                break;
            }

            case 12: {
                int a_type = chunk->code[i + 1] & 0xF;
                int b_type = chunk->code[i + 2] & 0xF;
                int stack_need = chunk->code[i + 1] >> 4;
                int live = chunk->code[i + 2] >> 4;
                TRACE("allocate/2 stack_need=%i (%x), live=%i (%x)\n" , stack_need, a_type, live, b_type);

                #ifdef IMPL_EXECUTE_LOOP
                    if (live > 16) {
                        fprintf(stderr, "Cannot use more than 16 registers.");
                        abort();
                    }

                    if ((ctx->e + (stack_need + 1)) - ctx->stack > ctx->stack_size) {
                        fprintf(stderr, "Need to allocate more stack space.");
                        abort();
                    }
                    ctx->stack_frame = ctx->e + 1;
                    ctx->e = ctx->stack_frame + stack_need;
                    *(ctx->e) = ctx->cp;
                #endif

                NEXT_INSTRUCTION(2);
                break;
            }

            case 14: {
                int a_type = chunk->code[i + 1] & 0xF;
                int b_type = chunk->code[i + 2] & 0xF;
                int stack_need = chunk->code[i + 1] >> 4;
                int live = chunk->code[i + 2] >> 4;
                TRACE("allocate_zero/2 stack_need=%i (%x), live=%i (%x)\n" , stack_need, a_type, live, b_type);

                #ifdef IMPL_EXECUTE_LOOP
                    if (live > 16) {
                        fprintf(stderr, "Cannot use more than 16 registers.");
                        abort();
                    }

                    if ((ctx->e + (stack_need + 1)) - ctx->stack > ctx->stack_size) {
                        fprintf(stderr, "Need to allocate more stack space.");
                        abort();
                    }
                    ctx->stack_frame = ctx->e + 1;
                    ctx->e = ctx->stack_frame + stack_need;
                    *(ctx->e) = ctx->cp;
                #endif

                //TODO: bzero/memset

                NEXT_INSTRUCTION(2);
                break;
            }

            case 18: {
                int n_words = chunk->code[i + 1] >> 4;

                TRACE("deallocate/1 n_words=%i\n", n_words);

                #ifdef IMPL_EXECUTE_LOOP
                    ctx->cp = *(ctx->e);
                    ctx->e = ctx->stack_frame - n_words;
                    ctx->stack_frame = ctx->e - 1;

                    DEBUG_DUMP_STACK(ctx);
                #endif

                NEXT_INSTRUCTION(1);
                break;
            }

            case 19: {
                TRACE("return/0\n");

                #ifdef IMPL_EXECUTE_LOOP
                    if ((long) ctx->cp == -1) {
                        return 0;
                    }

                    JUMP_TO_ADDRESS(ctx->cp);
                #endif

                #ifdef IMPL_CODE_LOADER
                    NEXT_INSTRUCTION(0);
                #endif
                break;
            }

            case 43: {
                int label = chunk->code[i + 1] >> 4;
                int next_off;
                term arg1;
                term arg2;
                DECODE_COMPACT_TERM(arg1, chunk->code, i + 2, next_off)
                DECODE_COMPACT_TERM(arg2, chunk->code, i + 2 + next_off, next_off)

                #ifdef IMPL_EXECUTE_LOOP
                    TRACE("is_eq_exact/2, label=%i, arg1=%lx, arg2=%lx\n", label, arg1, arg2);

                    if (arg1 == arg2) {
                        NEXT_INSTRUCTION(3);
                    } else {
                        i = (uint8_t *) mod->labels[label] - chunk->code;
                    }
                #endif

                #ifdef IMPL_CODE_LOADER
                    UNUSED(arg1)
                    UNUSED(arg2)
                    NEXT_INSTRUCTION(3);
                #endif

                break;
            }

            case 64: {
                int next_off;
                term src_value;
                DECODE_COMPACT_TERM(src_value, chunk->code, i + 1, next_off)

                #ifdef IMPL_EXECUTE_LOOP
                    int reg_b_type = reg_type_c(chunk->code[i + 1 + next_off] & 0xF);
                    int dest = chunk->code[i + 1 + next_off] >> 4;

                    TRACE("move/2 %lx, %c%i\n", src_value, reg_b_type, dest);

                    UNUSED(next_off)

                    if (reg_b_type == 'x') {
                        ctx->x[dest] = src_value;
                    } else if (reg_b_type == 'y') {
                        ctx->stack_frame[dest] = src_value;
                    } else {
                        abort();
                    }
                #endif

                #ifdef IMPL_CODE_LOADER
                    TRACE("move/2\n");

                    UNUSED(next_off)
                    UNUSED(src_value)
                #endif

                NEXT_INSTRUCTION(1 + next_off);
                break;
            }

            case 78: {
                int a_type = chunk->code[i + 1] & 0xF;
                int b_type = chunk->code[i + 2] & 0xF;
                int arity = chunk->code[i + 1] >> 4;
                int label = chunk->code[i + 2] >> 4;

                TRACE("call_ext_only/2, arity=%i (%x), label=%i (%x)\n", arity, a_type, label, b_type);

                #ifdef IMPL_EXECUTE_LOOP
                    NEXT_INSTRUCTION(2);
                    ctx->cp = INSTRUCTION_POINTER();

                    JUMP_TO_ADDRESS(mod->labels[label]);
                    break;
                #endif

                #ifdef IMPL_CODE_LOADER
                    NEXT_INSTRUCTION(2);
                    break;
                #endif
            }

            case 125: {
                int f_label = chunk->code[i + 1] >> 4;
                int live = chunk->code[i + 2] >> 4;
                int bif = chunk->code[i + 3] >> 4;
                int dreg = chunk->code[i + 6] >> 4;

                int next_off;
                term arg1;
                term arg2;
                DECODE_COMPACT_TERM(arg1, chunk->code, i + 4, next_off)
                DECODE_COMPACT_TERM(arg2, chunk->code, i + 4 + next_off, next_off)

                #ifdef IMPL_EXECUTE_LOOP
                    TRACE("gc_bif2/6 fail_lbl=%i, live=%i, bif=%i, arg1=0x%lx, arg2=0x%lx, dest=r%i\n", f_label, live, bif, arg1, arg2, dreg);

                    BifImpl2 func = (BifImpl2) mod->imported_bifs[bif];
                    func(ctx, f_label, live, arg1, arg2, dreg);
                #endif

                #ifdef IMPL_CODE_LOADER
                    UNUSED(f_label)
                    UNUSED(live)
                    UNUSED(bif)
                    UNUSED(arg1)
                    UNUSED(arg2)
                    UNUSED(dreg)
                #endif

                NEXT_INSTRUCTION(6);
                break;
            }

            case 153: {
                TRACE("line/1\n");

                NEXT_INSTRUCTION(1);
                break;
            }

            default:
                printf("Undecoded opcode: %i\n", chunk->code[i]);
                #ifdef IMPL_EXECUTE_LOOP
                    fprintf(stderr, "failed: %li\n", &chunk->code[i] - beam_file);
                #endif

                return 1;
        }
    }
}

#undef DECODE_COMPACT_TERM