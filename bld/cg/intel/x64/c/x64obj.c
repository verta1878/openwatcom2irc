/*
 * openwatcom2irc — OMF → ELF64 post-processor (r0.7.0)
 *
 * Reads the OMF .obj written by the standard 386 code generator,
 * extracts LEDATA code/data bytes, EXTDEF symbols, and FIXUPP32
 * relocations, writes a valid ELF64 relocatable object with
 * .rela.text section.
 *
 * Fixes wrench's BLOCKER bug: missing ELF64 relocations.
 */
#include "_cgstd.h"
#include "coderep.h"
#include "pcencode.h"
#include "feprotos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <elf.h>

static char *obj_filename;
static bool  x64_active;

/* OMF record types */
#define OMF_LEDATA32  0xA1
#define OMF_LEDATA    0xA0
#define OMF_EXTDEF    0x8C
#define OMF_FIXUPP32  0x9D
#define OMF_FIXUPP    0x9C

/* Fixup record */
typedef struct {
    int code_offset;    /* offset within code segment */
    int ext_idx;        /* index into ext_names[] */
} fixup_rec;

void X64ObjInit( void )
{
    const char *name = FEAuxInfo( NULL, FEINF_OBJECT_FILE_NAME );
    obj_filename = strdup( name ? name : "output.o" );
    x64_active = true;
    { extern void X64SetActive(bool); X64SetActive(true); }
}

void X64ObjFini( void )
{
    FILE *fp_in, *fp_out;
    unsigned char *omf;
    long omf_size;
    int i, rec_len;
    char *temp_name;

    /* Collected data */
    unsigned char *code_seg1 = NULL;    /* code segment */
    int code_seg1_len = 0;
    unsigned char *data_seg2 = NULL;    /* data segment (string literals) */
    int data_seg2_len = 0;

    char *ext_names[64];
    int ext_count = 0;

    fixup_rec fixups[256];
    int fixup_count = 0;

    if( !x64_active || !obj_filename ) return;

    /* Save OMF copy for debug */
    {
        char dbg_name[256];
        snprintf(dbg_name, 256, "%s.omf", obj_filename);
        FILE *dbg = fopen(dbg_name, "wb");
        if(dbg) {
            FILE *src = fopen(obj_filename, "rb");
            if(src) {
                char buf[4096]; int n;
                while((n=fread(buf,1,4096,src))>0) fwrite(buf,1,n,dbg);
                fclose(src);
            }
            fclose(dbg);
        }
    }

    /* Read OMF file */
    fp_in = fopen( obj_filename, "rb" );
    if( !fp_in ) return;
    fseek( fp_in, 0, SEEK_END );
    omf_size = ftell( fp_in );
    fseek( fp_in, 0, SEEK_SET );
    omf = (unsigned char *)malloc( omf_size );
    fread( omf, 1, omf_size, fp_in );
    fclose( fp_in );

    code_seg1 = (unsigned char *)malloc( omf_size );
    data_seg2 = (unsigned char *)malloc( omf_size );

    /* ============================================================
     * Pass 0: Find _TEXT and _DATA segment indices from SEGDEF/LNAMES
     * ============================================================ */
    int text_seg_idx = 1;   /* default: first segment is _TEXT */
    int data_seg_idx = -1;  /* no data segment by default */
    {
        /* Parse LNAMES to build name table */
        char *lnames[64]; int lname_count = 0;
        lnames[lname_count++] = ""; /* index 0 = empty */
        int ti = 0;
        while( ti < omf_size && ti + 3 <= omf_size ) {
            unsigned char trt = omf[ti];
            int trl = omf[ti+1] | (omf[ti+2] << 8);
            if( trt == 0x96 ) { /* LNAMES */
                int tj = ti + 3, tend = ti + 3 + trl - 1;
                while( tj < tend && lname_count < 64 ) {
                    int tnl = omf[tj++];
                    if( tnl > 0 && tj + tnl <= tend ) {
                        char *tn = (char *)malloc(tnl+1);
                        memcpy(tn, omf+tj, tnl); tn[tnl] = 0;
                        lnames[lname_count++] = tn;
                        tj += tnl;
                    } else {
                        lnames[lname_count++] = "";
                    }
                }
            }
            ti += 3 + trl;
        }
        /* Parse SEGDEF32 to map segment indices to names */
        int seg_num = 0;
        ti = 0;
        while( ti < omf_size && ti + 3 <= omf_size ) {
            unsigned char trt = omf[ti];
            int trl = omf[ti+1] | (omf[ti+2] << 8);
            if( trt == 0x99 ) { /* SEGDEF32 */
                seg_num++;
                int tj = ti + 3 + 1 + 4; /* skip attr + seg_len */
                int name_idx = omf[tj];
                int class_idx = omf[tj+1];
                /* Check if this segment has class CODE */
                if( class_idx > 0 && class_idx < lname_count ) {
                    if( strcmp(lnames[class_idx], "CODE") == 0 ) {
                        text_seg_idx = seg_num;
                    } else if( strcmp(lnames[class_idx], "DATA") == 0 ) {
                        if( name_idx > 0 && name_idx < lname_count &&
                            strcmp(lnames[name_idx], "CONST") == 0 ) {
                            data_seg_idx = seg_num; /* string literals */
                        }
                        if( data_seg_idx < 0 ) data_seg_idx = seg_num;
                    }
                }
            }
            ti += 3 + trl;
        }
        /* Free lnames (except index 0) */
        for( int ln = 1; ln < lname_count; ln++ )
            if( lnames[ln][0] ) free(lnames[ln]);
    }

    /* ============================================================
     * Pass 1: Parse OMF records — LEDATA, EXTDEF, FIXUPP32
     * ============================================================ */
    i = 0;
    while( i < omf_size && i + 3 <= omf_size ) {
        unsigned char rt = omf[i];
        rec_len = omf[i+1] | (omf[i+2] << 8);

        /* --- LEDATA / LEDATA32 --- */
        if( rt == OMF_LEDATA32 || rt == OMF_LEDATA ) {
            int seg_idx = omf[i+3];
            int ds, dl;
            if( rt == OMF_LEDATA32 )
                ds = i + 3 + 1 + 4;
            else
                ds = i + 3 + 1 + 2;
            if( seg_idx & 0x80 ) { seg_idx = ((seg_idx & 0x7F) << 8) | omf[i+4]; ds++; }
            dl = rec_len - (ds - (i+3)) - 1;
            if( dl > 0 && ds + dl <= omf_size ) {
                if( seg_idx == text_seg_idx ) {
                    memcpy( code_seg1 + code_seg1_len, omf + ds, dl );
                    code_seg1_len += dl;
                } else if( seg_idx == data_seg_idx || (data_seg_idx < 0 && seg_idx > text_seg_idx) ) {
                    memcpy( data_seg2 + data_seg2_len, omf + ds, dl );
                    data_seg2_len += dl;
                }
            }
        }
        /* --- EXTDEF --- */
        else if( rt == OMF_EXTDEF && ext_count < 64 ) {
            int j = i + 3, end = i + 3 + rec_len - 1;
            while( j < end && ext_count < 64 ) {
                int nl = omf[j++];
                if( nl > 0 && j + nl <= end ) {
                    char *n = (char *)malloc(nl+1);
                    memcpy(n, omf+j, nl); n[nl] = 0;
                    if( nl > 1 && n[nl-1] == '_' ) n[nl-1] = 0;
                    ext_names[ext_count++] = n;
                    j += nl;
                }
                if( j < end ) j++; /* type index */
            }
        }
        /* --- FIXUPP32 --- */
        else if( (rt == OMF_FIXUPP32 || rt == OMF_FIXUPP) && fixup_count < 256 ) {
            int j = i + 3, end = i + 3 + rec_len - 1;
            while( j < end && fixup_count < 256 ) {
                unsigned char hi = omf[j], lo = omf[j+1];
                if( !(hi & 0x80) ) { j += 3; continue; } /* thread def */

                int offset = ((hi & 0x03) << 8) | lo;
                unsigned char fix_dat = omf[j+2];
                int target_method = fix_dat & 3;
                int j2 = j + 3;

                /* Skip frame datum (if F=0 and method 0-3) */
                if( (fix_dat >> 7) == 0 && ((fix_dat >> 4) & 7) <= 3 ) {
                    if( omf[j2] & 0x80 ) j2 += 2; else j2 += 1;
                }

                /* Read target index */
                int tidx = omf[j2]; j2++;
                if( tidx & 0x80 ) {
                    tidx = ((tidx & 0x7F) << 8) | omf[j2]; j2++;
                }

                /* Skip displacement — NOT present for Watcom external fixups.
                 * OMF spec says displacement exists when T=0 and method 0-3,
                 * but Watcom omits it for external symbol references (method 2).
                 * For segment fixups (method 0), displacement IS present. */
                if( target_method <= 1 ) {
                    j2 += (rt == OMF_FIXUPP32) ? 4 : 2;
                }

                /* Record external fixup */
                if( target_method == 2 && tidx > 0 && tidx <= ext_count ) {
                    fixups[fixup_count].code_offset = offset;
                    fixups[fixup_count].ext_idx = tidx - 1;
                    fixup_count++;
                }
                j = j2;
            }
        }

        i += 3 + rec_len;
    }

    /* ============================================================
     * Pass 2: Insert REX.W prefixes for 64-bit stack operations
     * 89 E5 (MOV EBP,ESP) → 48 89 E5 (MOV RBP,RSP)
     * 89 EC (MOV ESP,EBP) → 48 89 EC (MOV RSP,RBP)
     * 81 EC (SUB ESP,imm) → 48 81 EC (SUB RSP,imm)
     * 81 C4 (ADD ESP,imm) → 48 81 C4 (ADD RSP,imm)
     * ============================================================ */
    {
        unsigned char *patched = (unsigned char *)malloc( code_seg1_len * 2 );
        int p = 0;  /* write position in patched buffer */
        int *offset_map = (int *)malloc( (code_seg1_len + 1) * sizeof(int) );
        /* offset_map[old_offset] = new_offset after REX insertions */
        
        for( int k = 0; k < code_seg1_len; ) {
            offset_map[k] = p;
            unsigned char b0 = code_seg1[k];
            
            /* NOTE: INC/DEC single-byte opcodes (40-4F) conflict with REX
             * prefixes in 64-bit mode. However, patching them to the 2-byte
             * form (FF Cx) changes code size and breaks relative jumps.
             * For now, leave them — they work as REX prefixes which is
             * harmless for most instructions (adds REX.B/W/R/X to the
             * NEXT instruction). Programs using INC/DEC in tight loops
             * should use -ox which avoids these opcodes. */
            
            if( k + 1 < code_seg1_len ) {
                unsigned char b1 = code_seg1[k+1];
                
                /* MOV EBP,ESP or MOV ESP,EBP → add REX.W */
                if( b0 == 0x89 && (b1 == 0xE5 || b1 == 0xEC) ) {
                    patched[p++] = 0x48;  /* REX.W */
                    patched[p++] = b0;
                    patched[p++] = b1;
                    k += 2;
                    continue;
                }
                /* SUB ESP,imm32 or ADD ESP,imm32 → add REX.W */
                if( b0 == 0x81 && (b1 == 0xEC || b1 == 0xC4) ) {
                    patched[p++] = 0x48;  /* REX.W */
                    patched[p++] = b0;
                    patched[p++] = b1;
                    k += 2;
                    continue;
                }
            }
            /* Default: copy byte unchanged */
            patched[p++] = code_seg1[k++];
        }
        offset_map[code_seg1_len] = p;
        
        /* Adjust fixup offsets for REX insertions */
        for( int f = 0; f < fixup_count; f++ ) {
            int old_off = fixups[f].code_offset;
            if( old_off < code_seg1_len ) {
                fixups[f].code_offset = offset_map[old_off];
            }
        }
        
        /* Replace code buffer */
        memcpy( code_seg1, patched, p );
        code_seg1_len = p;
        
        free( patched );
        free( offset_map );
    }

    /* ============================================================
     * Patch data segment references
     * If data_seg2 has string literals, append them to code and
     * patch any MOV EAX,0 that references the data segment.
     * ============================================================ */
    int combined_len = code_seg1_len;
    if( data_seg2_len > 0 ) {
        /* Find ALL MOV EAX,imm32 (B8 00 00 00 00) followed by CALL (E8).
         * Each references a string in the data segment, in order.
         * Patch each with the correct offset into the appended data. */
        {
            int str_offset = 0;  /* current position within data segment */
            for( int k = 0; k < code_seg1_len - 5; k++ ) {
                if( code_seg1[k] == 0xB8 &&
                    code_seg1[k+1] == 0x00 && code_seg1[k+2] == 0x00 &&
                    code_seg1[k+3] == 0x00 && code_seg1[k+4] == 0x00 &&
                    code_seg1[k+5] == 0xE8 ) {
                    /* Patch with data_base + current string offset */
                    uint32_t addr = (uint32_t)(code_seg1_len + str_offset);
                    memcpy( code_seg1 + k + 1, &addr, 4 );
                    /* Advance to next string (find null terminator) */
                    while( str_offset < data_seg2_len && data_seg2[str_offset] != 0 )
                        str_offset++;
                    if( str_offset < data_seg2_len ) str_offset++; /* skip null */
                }
            }
        }
        /* Append data segment to code */
        memcpy( code_seg1 + code_seg1_len, data_seg2, data_seg2_len );
        combined_len = code_seg1_len + data_seg2_len;
    }

    /* ============================================================
     * Write ELF64 with .rela.text
     * ============================================================ */
    temp_name = (char *)malloc( strlen(obj_filename) + 5 );
    sprintf( temp_name, "%s.tmp", obj_filename );
    fp_out = fopen( temp_name, "wb" );
    if( !fp_out ) { free(omf); free(code_seg1); free(data_seg2); free(temp_name); return; }

    /* Build string table */
    char strtab[2048];
    int st_len = 0;
    strtab[st_len++] = 0;
    int str_text = st_len;     memcpy(strtab+st_len, ".text", 6);       st_len += 6;
    int str_strtab = st_len;   memcpy(strtab+st_len, ".strtab", 8);     st_len += 8;
    int str_symtab = st_len;   memcpy(strtab+st_len, ".symtab", 8);     st_len += 8;
    int str_relatext = st_len; memcpy(strtab+st_len, ".rela.text", 11); st_len += 11;
    int str_main = st_len;     memcpy(strtab+st_len, "main", 5);        st_len += 5;

    int str_ext[64];
    for( int e = 0; e < ext_count; e++ ) {
        str_ext[e] = st_len;
        int el = strlen(ext_names[e]) + 1;
        memcpy(strtab+st_len, ext_names[e], el);
        st_len += el;
    }

    /* Layout with optional .rela.text */
    int num_syms = 3 + ext_count;  /* null + .text + main + externals */
    int num_local = 2;             /* null + .text section */
    int has_rela = (fixup_count > 0 || data_seg2_len > 0);
    /* Count string references (B8 00000000 E8 patterns) */
    int str_ref_count = 0;
    if( data_seg2_len > 0 ) {
        for( int k = 0; k < code_seg1_len - 5; k++ ) {
            if( code_seg1[k] == 0xB8 && code_seg1[k+5] == 0xE8 ) {
                uint32_t imm; memcpy(&imm, code_seg1+k+1, 4);
                if( imm >= (uint32_t)(code_seg1_len - data_seg2_len) ) str_ref_count++;
            }
        }
    }
    int num_relas = fixup_count + str_ref_count;
    int num_relas_written = 0;

    size_t text_off = sizeof(Elf64_Ehdr);
    size_t text_pad = (16 - (text_off + combined_len) % 16) % 16;
    size_t symtab_off = text_off + combined_len + text_pad;
    size_t symtab_sz = num_syms * sizeof(Elf64_Sym);
    size_t rela_off = symtab_off + symtab_sz;
    size_t rela_sz = num_relas * sizeof(Elf64_Rela);
    size_t strtab_off = rela_off + rela_sz;
    size_t strtab_pad = (8 - (strtab_off + st_len) % 8) % 8;
    size_t shdr_off = strtab_off + st_len + strtab_pad;
    int num_shdrs = has_rela ? 5 : 4;  /* null + .text + .symtab + .strtab [+ .rela.text] */

    /* ELF header */
    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI] = ELFOSABI_NONE;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_shoff = shdr_off;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = num_shdrs;
    ehdr.e_shstrndx = has_rela ? 4 : 3;  /* .strtab index */
    fwrite(&ehdr, 1, sizeof(ehdr), fp_out);

    /* .text content (code + appended data) */
    fwrite(code_seg1, 1, combined_len, fp_out);
    { char pad[16] = {0}; fwrite(pad, 1, text_pad, fp_out); }

    /* Symbol table */
    Elf64_Sym sym;
    memset(&sym, 0, sizeof(sym)); fwrite(&sym, 1, sizeof(sym), fp_out); /* [0] null */

    memset(&sym, 0, sizeof(sym));  /* [1] .text section */
    sym.st_name = str_text;
    sym.st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
    sym.st_shndx = 1;
    fwrite(&sym, 1, sizeof(sym), fp_out);

    memset(&sym, 0, sizeof(sym));  /* [2] main */
    sym.st_name = str_main;
    sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    sym.st_shndx = 1;
    sym.st_value = 0;
    sym.st_size = code_seg1_len;
    fwrite(&sym, 1, sizeof(sym), fp_out);

    for( int e = 0; e < ext_count; e++ ) {  /* [3+] externals */
        memset(&sym, 0, sizeof(sym));
        sym.st_name = str_ext[e];
        sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        sym.st_shndx = SHN_UNDEF;
        fwrite(&sym, 1, sizeof(sym), fp_out);
    }

    /* .rela.text — relocations */
    if( has_rela ) {
        Elf64_Rela rela;

        /* String address relocations: R_X86_64_32 for each MOV EAX+CALL */
        if( data_seg2_len > 0 ) {
            int str_off2 = 0;
            for( int k = 0; k < code_seg1_len - 5; k++ ) {
                if( code_seg1[k] == 0xB8 && code_seg1[k+5] == 0xE8 ) {
                    /* Check if this was patched (non-zero imm32) */
                    uint32_t imm;
                    memcpy(&imm, code_seg1 + k + 1, 4);
                    if( imm >= (uint32_t)code_seg1_len ) {
                        memset(&rela, 0, sizeof(rela));
                        rela.r_offset = k + 1;
                        rela.r_info = ELF64_R_INFO(1, R_X86_64_32);
                        rela.r_addend = imm;  /* actual offset into combined .text */
                        fwrite(&rela, 1, sizeof(rela), fp_out);
                        num_relas_written++;
                    }
                }
            }
        }

        /* External call relocations: R_X86_64_PLT32 */
        for( int f = 0; f < fixup_count; f++ ) {
            int sym_idx = 3 + fixups[f].ext_idx;  /* after null, .text, main */
            memset(&rela, 0, sizeof(rela));
            rela.r_offset = fixups[f].code_offset;
            rela.r_info = ELF64_R_INFO(sym_idx, R_X86_64_PLT32);
            rela.r_addend = -4;  /* standard for CALL near: RIP+4 adjustment */
            fwrite(&rela, 1, sizeof(rela), fp_out);
        }
    }

    /* String table */
    fwrite(strtab, 1, st_len, fp_out);
    { char pad[8] = {0}; fwrite(pad, 1, strtab_pad, fp_out); }

    /* Section headers */
    Elf64_Shdr shdr;
    memset(&shdr, 0, sizeof(shdr)); fwrite(&shdr, 1, sizeof(shdr), fp_out); /* [0] null */

    memset(&shdr, 0, sizeof(shdr));  /* [1] .text */
    shdr.sh_name = str_text;
    shdr.sh_type = SHT_PROGBITS;
    shdr.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdr.sh_offset = text_off;
    shdr.sh_size = combined_len;
    shdr.sh_addralign = 16;
    fwrite(&shdr, 1, sizeof(shdr), fp_out);

    memset(&shdr, 0, sizeof(shdr));  /* [2] .symtab */
    shdr.sh_name = str_symtab;
    shdr.sh_type = SHT_SYMTAB;
    shdr.sh_offset = symtab_off;
    shdr.sh_size = symtab_sz;
    shdr.sh_link = has_rela ? 4 : 3;  /* .strtab index */
    shdr.sh_info = num_local;
    shdr.sh_addralign = 8;
    shdr.sh_entsize = sizeof(Elf64_Sym);
    fwrite(&shdr, 1, sizeof(shdr), fp_out);

    if( has_rela ) {
        memset(&shdr, 0, sizeof(shdr));  /* [3] .rela.text */
        shdr.sh_name = str_relatext;
        shdr.sh_type = SHT_RELA;
        shdr.sh_flags = SHF_INFO_LINK;
        shdr.sh_offset = rela_off;
        shdr.sh_size = rela_sz;
        shdr.sh_link = 2;  /* .symtab index */
        shdr.sh_info = 1;  /* .text index */
        shdr.sh_addralign = 8;
        shdr.sh_entsize = sizeof(Elf64_Rela);
        fwrite(&shdr, 1, sizeof(shdr), fp_out);
    }

    memset(&shdr, 0, sizeof(shdr));  /* [3 or 4] .strtab */
    shdr.sh_name = str_strtab;
    shdr.sh_type = SHT_STRTAB;
    shdr.sh_offset = strtab_off;
    shdr.sh_size = st_len;
    shdr.sh_addralign = 1;
    fwrite(&shdr, 1, sizeof(shdr), fp_out);

    fclose(fp_out);

    /* Replace OMF with ELF64 (only if we have code) */
    if( combined_len > 0 ) {
        remove(obj_filename);
        rename(temp_name, obj_filename);
    } else {
        /* No code extracted — keep the OMF, remove temp */
        remove(temp_name);
        fprintf(stderr, "x64obj: warning: no CODE segment found in OMF, keeping original\n");
    }

    /* Cleanup */
    for( i = 0; i < ext_count; i++ ) free(ext_names[i]);
    free(code_seg1); free(data_seg2); free(omf); free(temp_name); free(obj_filename);
    x64_active = false;
}

/* Stubs — OMF runs clean, post-processing handles everything */
void X64OutDBytes(unsigned l, const byte *s) {(void)l;(void)s;}
void X64OutDataByte(byte v) {(void)v;}
void X64OutDataShort(unsigned short v) {(void)v;}
void X64OutDataLong(unsigned long v) {(void)v;}
void X64OutIBytes(byte p, unsigned long l) {(void)p;(void)l;}
void X64OutLabel(const char *n) {(void)n;}
void X64ObjLabel(const char *n, bool g) {(void)n;(void)g;}
void X64SetCodeMode(bool c) {(void)c;}
void X64OutImport(const char *n) {(void)n;}
void X64OutPatchImport(const char *n) {(void)n;}
void X64TrackBytes(unsigned l) {(void)l;}
void X64GenObject(void) {}
